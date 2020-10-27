// Author: Adam Wilson
// Date: 10/2/2020

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/types.h>
#include <sys/msg.h>
#include "shared.h"

//enum class { user, realTime };
//struct PCB {
//	int PID;
//	int PPID;
//	int priority;
//	int state;
//	enum class;
//	timespec cpuUsage;
//	timespec lifetime;
//	long int lastBurst;
//};
//struct shmseg {
//	struct PCB processTable[19];
//	int PIDmap[20];
//};
FILE* fp;
int procMax = 100;
int msqid, shmid, currentChildren;
key_t shmkey, msqkey;
struct shmseg* shmptr;
struct statistics stats;

struct statistics {
	struct mtime idle;
	struct mtime active;
	int numComplete;
};

// creates a shared memory segment and a message queue
void createMemory() {
	shmkey = ftok("oss", 137);
	shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
	if (shmid == -1) {
		perror("oss: Error");
		exit(-1);
	}

	shmptr = shmat(shmid, (void*)0, 0);
	if (shmptr == (void*)-1) {
		perror("oss: Error");
	}

	msqkey = ftok("oss", 731);
	msqid = msgget(msqkey, 0666 | IPC_CREAT);
	if (msqid == -1) {
		perror("oss: Error");
	}
}

// destroys message queue, and detaches and destroys shared memory
void destroyMemory() {
	if (msgctl(msqid, IPC_RMID, NULL) == -1) {
		perror("msgctl");
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

	printf("Time idle: %f\t\tTime active: %f\n", stats.idle.sec + (double)stats.idle.ns / BILLION, stats.active.sec + (double)stats.active.ns / BILLION);
	fclose(fp);
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
	destroyMemory();
	for (i = 0; i < currentChildren; i++) {
		mWait(&status);
	}
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

int main(int argc, char* argv[]) {
	struct msgbuf buf;
	struct statistics s = { {0,0}, {0,0}, 0 };
	struct mtime timeToNextProc;
	int status, i, totalProcs;
	pid_t pid;
	const int USERRATIO = 8;
	const long MAXWAIT = BILLION;
	struct Queue* active[4];

	for (i = 0; i < 4; i++) {
		active[i] = createQueue();
	}

	stats = s;
	setupInterrupts();
	createMemory();
	setupFile();
	currentChildren = totalProcs = 0;
	srand(time(0));

	for (i = 0; i < 19; i++) {
		shmptr->PIDmap[i] = i != 0 ? 0 : 1;
	}
	shmptr->currentTime.sec = shmptr->currentTime.ns = 0;

	//int queuePos = 0;
	while (totalProcs < 10) {
		timeToNextProc = (struct mtime){ shmptr->currentTime.sec + rand() % 3, shmptr->currentTime.ns };
		int wait = rand() % 1001;
		shmptr->currentTime = addTime(shmptr->currentTime, 1, wait);
		stats.idle = addTime(stats.idle, 1, wait);

		if (currentChildren < 18 && compareTimes(shmptr->currentTime, timeToNextProc)) {
			for (i = 1; i < 19; i++) if (shmptr->PIDmap[i] == 0) break;
			shmptr->PIDmap[i] = 1;
			currentChildren++;
			totalProcs++;

			struct PCB p = { i, 1, rand() % 4 + 1, 0, user, { 0, 0 }, { 0, 0 }, 0 };
			shmptr->processTable[i - 1] = p;
			if (rand() % 10 > USERRATIO) {
				shmptr->processTable[i - 1].priority = 0;
					shmptr->processTable[i - 1].pClass = realTime;
			}

			fprintf(fp, "OSS: Generating process with PID %d and putting it in queue 0 at time %f s\n", i, shmptr->currentTime.sec + (double)shmptr->currentTime.ns / BILLION);

			pid = fork();

			if (pid == -1) {
				shmptr->PIDmap[i] = 0;
				currentChildren--;
				totalProcs--;
				perror("oss: Error");
			}

			else if (pid == 0) {
				char index[2];
				sprintf(index, "%d", i);
				execl("user_proc", index, (char*)NULL);
				exit(0);
			}

			else {
				//enqueue(active[shmptr->processTable[i - 1].priority], i);
				printf("OSS: putting process with PID %d in queue %d\n", i, 0);
				enqueue(active[0], i);
			}
		}

		//int j = 0;
		//while (isEmpty(active[queuePos]) && j < 4) {
		//	queuePos = (queuePos + 1) % 4;
		//	j++;
		//}
		if (!isEmpty(active[0])) {
			buf.type = dequeue(active[0]);
			printf("OSS: dispatching process with PID %d from queue %d at time %f s\n", buf.type, 0, shmptr->currentTime.sec + (double)shmptr->currentTime.ns/BILLION);
			strcpy(buf.text, "");
			buf.time = BILLION / 100;
			buf.pid = 0;
			if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) {
				perror("msgsnd");
			}

			int scheduleTime = rand() % 10000 + 100;
			shmptr->currentTime = addTime(shmptr->currentTime, 0, scheduleTime);
			stats.active = addTime(stats.active, 0, scheduleTime);
			printf("OSS: dispatch took %d ns\n", scheduleTime);

			if (msgrcv(msqid, &buf, sizeof(struct msgbuf), 20, 0) == -1) {
				perror("msgrcv");
			}
			//printf("oss: msqid: %d", msqid);

			if (strcmp(buf.text, "terminated\0") == 0)  {
				printf("OSS: process with PID %d %s after running for %d ns.\n", buf.pid, buf.text, buf.time);
				mWait(&status);
				currentChildren--;
			}
			else {
				printf("OSS: process with PID %d ran for %d ns, %s.\n", buf.pid, buf.time, buf.text);
				enqueue(active[0], buf.pid);
			}

			shmptr->currentTime = addTime(shmptr->currentTime, 0, buf.time);
			stats.active = addTime(stats.active, 0, buf.time);
		}
	}

	for (i = 0; i < currentChildren; i++) mWait(&status);
	destroyMemory();
}