// Author: Adam Wilson
// Date: 10/2/2020

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/types.h>
#include <sys/msg.h>
#include "shared.h"

// intra-file globals
FILE* fp;
int msqid, shmid, currentChildren;
key_t shmkey, msqkey;
struct shmseg* shmptr;
struct statistics stats;

// creates a shared memory segment and a message queue
void createMemory() {
	shmkey = ftok("oss", 137);
	shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
	if (shmid == -1) {
		perror("oss: Error");
		exit(-1);
	}

	shmptr = shmat(shmid, (void*)0, 0);
	if (shmptr == (void*)-1) perror("oss: Error");

	msqkey = ftok("oss", 731);
	msqid = msgget(msqkey, 0666 | IPC_CREAT);
	if (msqid == -1) perror("oss: Error");
}

// destroys message queue, and detaches and destroys shared memory
void cleanup() {
	printf("\nTime idle: %f s\t\tTime active: %f s\n", timeToDouble(stats.idle), timeToDouble(stats.active));
	printf("Throughput: %f jobs/minute\n", stats.numComplete * 60 / timeToDouble(shmptr->currentTime));
	fclose(fp);
	if (msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("oss: msgctl");
		exit(1);
	}
	if (shmdt(shmptr) == -1) {
		perror("oss: Error");
		exit(-1);
	}
	if (shmctl(shmid, IPC_RMID, 0) == -1) {
		perror("oss: Error");
		exit(-1);
	}
}

// deletes any existing file output.log, then creates one in append mode
void setupFile() {
	fp = fopen("output.log", "w+");
	if (fp == NULL) {
		perror("oss: Error");
	}
	fclose(fp);
	fp = fopen("output.log", "a");
	if (fp == NULL) {
		perror("oss: Error");
	}
}

// outputs possible errors for wait() call
void mWait(int* status) {
	if (wait(&status) > 0) {
		if (WIFEXITED(status) && WEXITSTATUS(status)) {
			if (WEXITSTATUS(status) == 127) perror("oss: Error");
			else perror("oss: Error");
		}
		//else perror("oss: Error");
	}
}

// sends message to stderr, kills all processes in this process group, which is 
// ignored by parent, then zeros out procMax so no new processes are spawned
static void interruptHandler(int s) {
	int status, i;
	fprintf(stderr, "\nInterrupt recieved.\n");
	signal(SIGQUIT, SIG_IGN);
	kill(-getpid(), SIGQUIT);
	for (i = 0; i < currentChildren; i++) { mWait(&status); }
	cleanup();
	exit(0);
}

// sets up sigaction for SIGALRM
static int setupAlarmInterrupt(void) {
	struct sigaction sigAlrmAct;
	sigAlrmAct.sa_handler = interruptHandler;
	sigAlrmAct.sa_flags = 0;
	sigemptyset(&sigAlrmAct.sa_mask);
	return (sigaction(SIGALRM, &sigAlrmAct, NULL));
}

// sets up sigaction for SIGINT, using same handler as SIGALRM to avoid conflict
static int setupSIGINT(void) {
	struct sigaction sigIntAct;
	sigIntAct.sa_handler = interruptHandler;
	sigIntAct.sa_flags = 0;
	sigemptyset(&sigIntAct.sa_mask);
	return (sigaction(SIGINT, &sigIntAct, NULL));
}

// sets ups itimer with time of 3s and interval of 0s
static int setupitimer() {
	struct itimerval value = { {0, 0}, {3, 0} };
	return (setitimer(ITIMER_REAL, &value, NULL));
}

// sets up timer, and SIGALRM and SIGINT handlers
static int setupInterrupts() {
	if (setupitimer() == -1) {
		perror("oss: Error");
		exit(-1);
	}
	if (setupAlarmInterrupt() == -1) {
		perror("oss: Error");
		exit(-1);
	}
	if (setupSIGINT() == -1) {
		perror("oss: Error");
		exit(-1);
	}
}

// spawns and schedules children according to multi-level feedback algorithm, keeping track of statistics
int main(int argc, char* argv[]) {
	int status, i, totalProcs;
	pid_t pid;
	const int USERRATIO = 8, PROCMAX = 100;;
	const long MAXWAIT = BILLION;
	const long baseQuantum = BILLION / 100;
	struct msgbuf buf;
	struct statistics s = { {0,0}, {0,0}, 0 };
	struct mtime timeToNextProc;
	struct Queue* active[4];
	struct Queue* expired[4];
	struct Queue* RTqueue;

	// creates queues
	RTqueue = createQueue();
	for (i = 0; i < 4; i++) {
		active[i] = createQueue();
		expired[i] = createQueue();
	}

	stats = s;
	setupInterrupts();
	createMemory();
	setupFile();
	currentChildren = totalProcs = 0;
	srand(time(0));

	// initialize PIDmap and currentTime
	for (i = 0; i < 19; i++) shmptr->PIDmap[i] = i != 0 ? 0 : 1;
	shmptr->currentTime.sec = shmptr->currentTime.ns = 0;

	// runs OSS until 100 processes have been spawned
	while (totalProcs < PROCMAX) {
		// sets timeToNextProc to, on average, currentTime + 0.75s
		timeToNextProc = addTime(shmptr->currentTime, rand() % 2, rand() % (BILLION / 2));
		// waits for 0.5sec + 1-1000 nanoseconds and updates time
		int wait = BILLION / 2 + rand() % 1001;
		shmptr->currentTime = addTime(shmptr->currentTime, 0, wait);
		stats.idle = addTime(stats.idle, 0, wait);

		// spawns new process if process table isn't full and delay period has passed
		if (currentChildren < 18 && compareTimes(shmptr->currentTime, timeToNextProc)) {
			// finds available pid for new process, sets corresponding index of PIDmap to 1, and increments totalProcs and currentChildren
			for (i = 1; i < 19; i++) if (shmptr->PIDmap[i] == 0) break;
			shmptr->PIDmap[i] = 1;
			currentChildren++;
			totalProcs++;

			// initializes process control block with pid=i, ppid=0, user class 90% of the time, in which case priority
			// is random value [1-4], and real time class 10% of the time, everything else initialized to 0
			struct PCB p = { i, 0, rand() % 4 + 1, 0, user, { 0, 0 }, { 0, 0 }, 0 };
			shmptr->processTable[i - 1] = p;
			if (rand() % 10 > USERRATIO) {
				shmptr->processTable[i - 1].priority = 0;
				shmptr->processTable[i - 1].pClass = realTime;
			}

			// fork
			pid = fork();

			// rolls values back if fork fails
			if (pid == -1) {
				shmptr->PIDmap[i] = 0;
				currentChildren--;
				totalProcs--;
				perror("oss: Error");
			}

			// exec child with its pid as parameter
			else if (pid == 0) {
				char index[2];
				sprintf(index, "%d", i);
				execl("user_proc", index, (char*)NULL);
				exit(0);
			}

			// if child was real time process, add to real time queue, otherwise add to an active queue according to priority level
			else {
				if (shmptr->processTable[i - 1].pClass == realTime) {
					printf("OSS: Generating process with PID %d and putting it in real time queue at time %f s\n", i, timeToDouble(shmptr->currentTime));
					enqueue(RTqueue, i);
				}
				else {
					int x = shmptr->processTable[i - 1].priority;
					printf("OSS: Generating process with PID %d and putting it in active queue %d at time %f s\n", i, x, timeToDouble(shmptr->currentTime));
					enqueue(active[x - 1], i);
				}
			}
		}

		// if there is a real time process ready, schedule it
		if (!isEmpty(RTqueue)) {
			buf.type = dequeue(RTqueue);
			printf("\nOSS: dispatching process with PID %d from real time queue at time %f s\n", buf.type, 0, timeToDouble(shmptr->currentTime));
			strcpy(buf.text, "");
			buf.time = baseQuantum;
			buf.pid = 0;
			if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) perror("msgsnd");

			int scheduleTime = rand() % 10000 + 100;
			shmptr->currentTime = addTime(shmptr->currentTime, 0, scheduleTime);
			stats.active = addTime(stats.active, 0, scheduleTime);
			printf("OSS: dispatch took %d ns\n", scheduleTime);

			// send message to scheduled process telling it how much time it should run
			if (msgrcv(msqid, &buf, sizeof(struct msgbuf), 20, 0) == -1) perror("msgrcv");

			// handle process termination
			if (strcmp(buf.text, "terminated\0") == 0) {
				printf("OSS: process with PID %d %s after running for %d ns.\n", buf.pid, buf.text, buf.time);
				shmptr->PIDmap[buf.pid] = 0;
				stats.numComplete++;
				//shmptr->processTable[buf.pid-1] = NULL;
				mWait(&status);
				currentChildren--;
			}
			else {
				printf("OSS: process with PID %d ran for %d ns, %s of its quantum.\n", buf.pid, buf.time, buf.text);
				enqueue(RTqueue, buf.pid);
			}

			shmptr->currentTime = addTime(shmptr->currentTime, 0, buf.time);
			stats.active = addTime(stats.active, 0, buf.time);
		}

		// otherwise, if there is a process in an active queue, schedule the one at the head of the highest priority queue
		else {
			for (i = 0; i < 4; i++) if (!isEmpty(active[i])) break;
			// if no processes are active, swap active and expired queue arrays and re-search active queues for highest priority
			if (i == 4) {
				printf("OSS: No active processes: swapping active and expired queues.\n");
				for (i = 0; i < 4; i++) {
					struct Queue* tmp = active[i];
					active[i] = expired[i];
					expired[i] = tmp;
				}
				for (i = 0; i < 4; i++) if (!isEmpty(active[i])) break;
			}
			// if there is a process in an active queue, schedule the one at the head of the highest priority queue
			if (i != 4) {
				buf.type = dequeue(active[i]);
				printf("\nOSS: dispatching process with PID %d from active queue %d at time %f s\n", buf.type, i + 1, timeToDouble(shmptr->currentTime));
				strcpy(buf.text, "");
				buf.time = baseQuantum / pow(2, i);
				buf.pid = 0;
				if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) perror("msgsnd");

				int scheduleTime = rand() % 10000 + 100;
				shmptr->currentTime = addTime(shmptr->currentTime, 0, scheduleTime);
				stats.active = addTime(stats.active, 0, scheduleTime);
				printf("OSS: dispatch took %d ns\n", scheduleTime);

				// send message to scheduled process telling it how much time it should run
				if (msgrcv(msqid, &buf, sizeof(struct msgbuf), 20, 0) == -1) perror("msgrcv");

				// handle process termination
				if (strcmp(buf.text, "terminated\0") == 0) {
					printf("OSS: process with PID %d %s after running for %d ns.\n", buf.pid, buf.text, buf.time);
					shmptr->PIDmap[buf.pid] = 0;
					stats.numComplete++;
					mWait(&status);
					currentChildren--;
				}
				else {
					printf("OSS: process with PID %d ran for %d ns, %s of its quantum.\n", buf.pid, buf.time, buf.text);
					int x = shmptr->processTable[buf.pid - 1].priority;
					if (strcmp(buf.text, "all\0") == 0) {
						if (x < 4) x = shmptr->processTable[buf.pid - 1].priority++;
						enqueue(expired[x - 1], buf.pid);
						printf("OSS: putting process with PID %d in expired queue %d.\n", buf.pid, x);
					}
					else {
						enqueue(active[x - 1], buf.pid);
						printf("OSS: putting process with PID %d back in active queue %d.\n", buf.pid, x);
					}
				}

				shmptr->currentTime = addTime(shmptr->currentTime, 0, buf.time);
				stats.active = addTime(stats.active, 0, buf.time);
			}
		}
	}
	for (i = 0; i < currentChildren; i++) { mWait(&status); }
	cleanup();
}