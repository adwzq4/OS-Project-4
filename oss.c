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
int msqid, shmid, currentChildren, totalProcs, lastPID;
key_t shmkey, msqkey;
struct shmseg* shmptr;
struct statistics stats;
struct Queue* active[4];
struct Queue* expired[4];
struct Queue* RTqueue;
struct Queue* blockedQueue;

// creates a shared memory segment and a message queue
void createMemory() {
	shmkey = ftok("oss", 137);
	shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
	if (shmid == -1) {
		perror("oss: Error");
		exit(-1);
	}

	shmptr = shmat(shmid, (void*)0, 0);
	if (shmptr == (void*)-1) { perror("oss: Error"); }

	msqkey = ftok("oss", 731);
	msqid = msgget(msqkey, 0666 | IPC_CREAT);
	if (msqid == -1) { perror("oss: Error"); }
}

// outputs stats, waits for children, destroys message queue, and detaches and destroys shared memory
void terminateOSS() {
	int i, status;
	fprintf(fp, "\n\n\nOSS ran for %f s, completing %d jobs\n", timeToDouble(shmptr->currentTime), stats.numComplete);
	fprintf(fp, "CPU was active %.2f%% of the time, and idle %.2f%% of the time\n", timeToDouble(stats.OSactive) / timeToDouble(shmptr->currentTime) * 100,
		100 - timeToDouble(stats.OSactive) / timeToDouble(shmptr->currentTime) * 100);
	fprintf(fp, "Average CPU usage: %f s / process \n", timeToDouble(stats.active) / stats.numComplete);
	fprintf(fp, "Average time blocked : %f s / process \n", timeToDouble(stats.timeBlocked) / stats.numComplete);
	fprintf(fp, "Average time ready/idle: %f s / process\n", (timeToDouble(stats.lifetime) - timeToDouble(stats.active) - timeToDouble(stats.timeBlocked)) / stats.numComplete);
	fprintf(fp, "Throughput: %f jobs / minute \n", stats.numComplete * 60 / timeToDouble(shmptr->currentTime));
	fclose(fp);
	for (i = 0; i < currentChildren; i++) { mWait(&status); }
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
	exit(0);
}

// deletes output.log if it exists, then creates it in append mode
void setupFile() {
	fp = fopen("output.log", "w+");
	if (fp == NULL) { perror("oss: Error"); }
	fclose(fp);
	fp = fopen("output.log", "a");
	if (fp == NULL) { perror("oss: Error"); }
}

// sends message to stderr, kills all processes in this process group, which is 
// ignored by parent, then zeros out procMax so no new processes are spawned
static void interruptHandler(int s) {
	fprintf(stderr, "\nInterrupt recieved.\n");
	signal(SIGQUIT, SIG_IGN);
	kill(-getpid(), SIGQUIT);
	terminateOSS();
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

// handle process termination, adding process's cpu usage, lifetime, and blocked time to cumalative totals, setting PIDmap to 0, and updating counters
void terminateProc(int pid, long int time){
	int status;
	shmptr->processTable[pid-1].pState = terminated;
	shmptr->processTable[pid-1].cpuUsage = addTime(shmptr->processTable[pid-1].cpuUsage, 0, time);
	stats.OSactive = addTime(stats.OSactive, 0, time);
	shmptr->processTable[pid-1].lifetime = addTime(shmptr->processTable[pid-1].lifetime, 0, time);
	fprintf(fp, "OSS: process with PID %d terminated after running for %d ns, its lifetime was %f s\n", pid, time, timeToDouble(shmptr->processTable[pid - 1].lifetime));
	stats.active = addTime(stats.active, shmptr->processTable[pid-1].cpuUsage.sec, shmptr->processTable[pid-1].cpuUsage.ns);
	stats.lifetime = addTime(stats.lifetime, shmptr->processTable[pid-1].lifetime.sec, shmptr->processTable[pid-1].lifetime.ns);
	stats.timeBlocked = addTime(stats.timeBlocked, shmptr->processTable[pid-1].timeBlocked.sec, shmptr->processTable[pid-1].timeBlocked.ns);
	shmptr->PIDmap[pid] = 0;
	stats.numComplete++;
	mWait(&status);
	currentChildren--;
}

// updates lifetimes of all living child processes by adding the argument, in nanoseconds
void updateLifetimes(long int time){
	int i;
	for (i = 1; i < 19; i++) { if (shmptr->PIDmap[i] == 1) { shmptr->processTable[i - 1].lifetime = addTime(shmptr->processTable[i - 1].lifetime, 0, time); } }
}

// spawn a new child and place it in the correct queue
void spawnChildProc() {
	const int USERRATIO = 8;
	int i;
	pid_t pid;

	// finds available pid for new process, sets corresponding index of PIDmap to 1, and increments totalProcs and currentChildren
	for (i = lastPID + 1; i < 19; i++) { if (shmptr->PIDmap[i] == 0) { break; } }
	if (i == 19) { for (i = 1; i < lastPID; i++) { if (shmptr->PIDmap[i] == 0) { break; } } }
	shmptr->PIDmap[i] = 1;
	lastPID = i;
	currentChildren++;
	totalProcs++;

	// initializes process control block with pid=i, ppid=0, user class 90% of the time, in which case priority
	// is random value [1-4], and real time class 10% of the time, everything else initialized to 0
	shmptr->processTable[i - 1] = (struct PCB) { i, 0, rand() % 4 + 1, ready, user, { 0, 0 }, { 0, 0 }, { 0, 0 }, 0 };
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
			fprintf(fp, "OSS: generating process with PID %d and putting it in real time queue at time %f s\n", i, timeToDouble(shmptr->currentTime));
			enqueue(RTqueue, i);
		}
		else {
			int x = shmptr->processTable[i - 1].priority;
			fprintf(fp, "OSS: generating process with PID %d and putting it in active queue %d at time %f s\n", i, x, timeToDouble(shmptr->currentTime));
			enqueue(active[x - 1], i);
		}
	}
}

// select a process to dispatch
void scheduler(){
	int i, x;
	const long baseQuantum = BILLION / 100;
	struct msgbuf buf;

	// check blocked queue for ready processes, put any active processes into active queue 1 and all blocked queues back
	for (i = 0; i < blockedQueue->size; i++) {
		x = dequeue(blockedQueue);
		if (shmptr->processTable[x-1].pState == ready) {
			shmptr->processTable[x-1].priority = 1;
			enqueue(active[0], x);
			// add 250000-750000 ns to times
			int unblockTime = rand() % 250000 + 500000;
			shmptr->currentTime = addTime(shmptr->currentTime, 0, unblockTime);
			updateLifetimes(unblockTime);
			stats.OSactive = addTime(stats.OSactive, 0, buf.time);
			fprintf(fp, "OSS: process with PID %d woke up: putting it into active queue 1, which took %d ns\n", x, unblockTime);
		}
		else { enqueue(blockedQueue, x); }
	}

	// if there is a real time process ready, schedule it for the full base quantum by sending a message to it with that time
	if (!isEmpty(RTqueue)) {
		buf.type = dequeue(RTqueue);
		fprintf(fp, "\nOSS: dispatching process with PID %d from real time queue at time %f s\n", buf.type, 0, timeToDouble(shmptr->currentTime));
		shmptr->processTable[buf.type - 1].pState == run;
		strcpy(buf.text, "");
		buf.time = baseQuantum;
		buf.pid = 0;
		if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) { perror("msgsnd"); }

		// simulate scheduling time
		int scheduleTime = rand() % 10000 + 100;
		shmptr->currentTime = addTime(shmptr->currentTime, 0, scheduleTime);
		updateLifetimes(scheduleTime);
		stats.OSactive = addTime(stats.OSactive, 0, buf.time);
		fprintf(fp, "OSS: dispatch took %d ns\n", scheduleTime);

		// send message to scheduled process telling it how much time it should run
		if (msgrcv(msqid, &buf, sizeof(struct msgbuf), 20, 0) == -1) { perror("msgrcv"); }

		// handle process termination
		if (strcmp(buf.text, "terminated\0") == 0) {
			terminateProc(buf.pid, buf.time);
		}
		// otherwise put process back in real time queue
		else {
			fprintf(fp, "OSS: process with PID %d ran for %d ns, %s of its quantum\n", buf.pid, buf.time, buf.text);
			shmptr->processTable[buf.pid-1].cpuUsage = addTime(shmptr->processTable[buf.pid-1].cpuUsage, 0, buf.time);
			stats.OSactive = addTime(stats.OSactive, 0, buf.time);
			enqueue(RTqueue, buf.pid);
			shmptr->processTable[buf.pid - 1].pState == ready;
		}

		// update times
		shmptr->currentTime = addTime(shmptr->currentTime, 0, buf.time);
		stats.OSactive = addTime(stats.OSactive, 0, buf.time);
		updateLifetimes(buf.time);
	}

	// otherwise, if there is a process in an active queue, schedule the one at the head of the highest priority queue
	else {
		for (i = 0; i < 4; i++) { if (!isEmpty(active[i])) { break; } }
		// if no processes are active, swap active and expired queue arrays and re-search active queues for highest priority
		if (i == 4) {
			// add 5000-15000 ns to times
			int swapTime = rand() % 10000 + 5000;
			fprintf(fp, "OSS: No active processes: swapping active and expired queues, which took %d ns\n", swapTime);
			shmptr->currentTime = addTime(shmptr->currentTime, 0, swapTime);
			stats.OSactive = addTime(stats.OSactive, 0, buf.time);
			updateLifetimes(swapTime);
			for (i = 0; i < 4; i++) {
				struct Queue* tmp = active[i];
				active[i] = expired[i];
				expired[i] = tmp;
			}
			for (i = 0; i < 4; i++) { if (!isEmpty(active[i])) { break; } }
		}
		// if there is a process in an active queue, schedule the one at the head of the highest priority queue
		if (i != 4) {
			buf.type = dequeue(active[i]);
			fprintf(fp, "\nOSS: dispatching process with PID %d from active queue %d at time %f s\n", buf.type, i + 1, timeToDouble(shmptr->currentTime));
			shmptr->processTable[buf.type - 1].pState == run;
			strcpy(buf.text, "");
			buf.time = baseQuantum / pow(2, i);
			buf.pid = 0;
			if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) { perror("msgsnd"); }

			// simulate scheduling time
			int scheduleTime = rand() % 10000 + 100;
			shmptr->currentTime = addTime(shmptr->currentTime, 0, scheduleTime);
			stats.OSactive = addTime(stats.OSactive, 0, buf.time);
			updateLifetimes(scheduleTime);
			fprintf(fp, "OSS: dispatch took %d ns\n", scheduleTime);

			// send message to scheduled process telling it how much time it should run
			if (msgrcv(msqid, &buf, sizeof(struct msgbuf), 20, 0) == -1) { perror("msgrcv"); }

			// handle process termination
			if (strcmp(buf.text, "terminated\0") == 0) {
				terminateProc(buf.pid, buf.time);
			}
			// handle blocked process, adding it to blocked queue
			else if (strcmp(buf.text, "blocked\0") == 0){
				fprintf(fp, "OSS: process with PID %d became blocked, now adding to blocked queue\n", buf.pid);
				stats.OSactive = addTime(stats.OSactive, 0, buf.time);
				shmptr->processTable[buf.pid-1].cpuUsage = addTime(shmptr->processTable[buf.pid-1].cpuUsage, 0, buf.time);
				enqueue(blockedQueue, buf.pid);
			}
			// if a process used all its quantum, increment priority and put it in an expired queue, otherwise put it back in
			// the active queue where it came from
			else {
				fprintf(fp, "OSS: process with PID %d ran for %d ns, %s of its quantum\n", buf.pid, buf.time, buf.text);
				shmptr->processTable[buf.pid - 1].pState == ready;
				shmptr->processTable[buf.pid-1].cpuUsage = addTime(shmptr->processTable[buf.pid-1].cpuUsage, 0, buf.time);
				stats.OSactive = addTime(stats.OSactive, 0, buf.time);
				x = shmptr->processTable[buf.pid - 1].priority;
				if (strcmp(buf.text, "all\0") == 0) {
					if (x < 4) { x = ++shmptr->processTable[buf.pid - 1].priority; }
					enqueue(expired[x - 1], buf.pid);
					fprintf(fp, "OSS: putting process with PID %d in expired queue %d\n", buf.pid, x);
				}
				else {
					enqueue(active[x - 1], buf.pid);
					fprintf(fp, "OSS: putting process with PID %d back in active queue %d\n", buf.pid, x);
				}
			}

			// update times
			stats.OSactive = addTime(stats.OSactive, 0, buf.time);
			shmptr->currentTime = addTime(shmptr->currentTime, 0, buf.time);
			updateLifetimes(buf.time);
		}
	}
}

// spawns and schedules children according to multi-level feedback algorithm, keeping track of statistics
int main(int argc, char* argv[]) {
	int wait, i;
	const int PROCMAX = 100;
	struct mtime timeToNextProc;

	// creates queues
	RTqueue = createQueue();
	blockedQueue = createQueue();
	for (i = 0; i < 4; i++) {
		active[i] = createQueue();
		expired[i] = createQueue();
	}

	// initialize globals, interrupts, file pointer, and shared memory
	stats = (struct statistics){ { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, 0 };
	currentChildren = totalProcs = 0;
	lastPID = 0;
	setupInterrupts();
	createMemory();
	setupFile();
	srand(time(0));

	// initialize PIDmap and currentTime
	for (i = 0; i < 19; i++) { shmptr->PIDmap[i] = i != 0 ? 0 : 1; }
	shmptr->currentTime.sec = shmptr->currentTime.ns = 0;

	// runs OSS until 100 processes have been spawned, and then until all children have terminated
	while (totalProcs < PROCMAX || currentChildren > 0) {
		// sets timeToNextProc to, on average, currentTime + 0.5s
		timeToNextProc = addTime(shmptr->currentTime, 0, rand() % BILLION);
		// waits for 0.2sec + 1-1000 nanoseconds and updates time
		wait = BILLION / 5 + rand() % 1001;
		shmptr->currentTime = addTime(shmptr->currentTime, 0, wait);
		updateLifetimes(wait);

		// spawns new process if process table isn't full, delay period has passed, and PROCMAX hasn't been reached
		if (currentChildren < 18 && compareTimes(shmptr->currentTime, timeToNextProc) && totalProcs < PROCMAX) {
			spawnChildProc();
		}

		// call scheduler each loop
		scheduler();
	}

	// finish up
	printf("OSS: 100 process have been spawned and run to completion, now terminating OSS.\n");
	terminateOSS();
}