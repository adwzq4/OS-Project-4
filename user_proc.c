// Author: Adam Wilson
// Date: 10/2/2020

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h> 
#include <sys/shm.h> 
#include <sys/types.h>
#include <sys/msg.h>
#include "shared.h"

//#define BILLION 1000000000
//
//enum class { user, realTime };
//
//struct msgbuf {
//	long type;
//	char text[200];
//};
//
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
//
//struct shmseg {
//	struct PCB processTable[19];
//	int PIDmap[20];
//};

int main(int argc, char* argv[]) {
	struct msgbuf buf;
	//struct msgbuf buf2;
	int wholeQ, amountQ;
	int i, q;
	const int TERMRATIO = 5;

	int pid = atoi(argv[0]);
	srand(pid * time(0));

	// create shared memory segment the same size as struct shmseg and get its shmid
	key_t shmkey = ftok("oss", 137);
	int shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
	if (shmid == -1) {
		perror("user_proc: Error");
		exit(-1);
	}

	// attach struct pointer to shared memory segment
	struct shmseg* shmptr = shmat(shmid, (void*)0, 0);
	if (shmptr == (void*)-1) {
		perror("user_proc: Error");
	}

	key_t msqkey = ftok("oss", 731);

	int msqid = msgget(msqkey, 0666 | IPC_CREAT);
	int msqidcpy = msqid;
	if (msqid == -1) {
		perror("user_proc: Error");
	}

	int term = 0;
	while (1) {
		//printf("msqid: %d\n", msqid);
		term = rand() % TERMRATIO + 1;

		if (msgrcv(msqid, &buf, sizeof(struct msgbuf), pid, 0) == -1) {
			perror("user_proc: Error");
		}

		q = buf.time;
		//printf("user_proc received message: %s\n", buf.text);
		//printf("msqid now: %d\n", msqid);
		buf.type = 20;
		buf.pid = pid;

		if (term == TERMRATIO) {
			strcpy(buf.text, "terminated");
			buf.time = rand() % q;
			if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) {
				perror("user_proc: Error");
			}
			break;
		}


		wholeQ = rand() % 2;
		if (wholeQ == 0) {
			//amountQ = rand() % q;
			strcpy(buf.text, "only part of its quantum");
			buf.time = rand() % q;
			//sprintf(buf.text, "only part of its quantum.", amountQ);
		}
		else {
			strcpy(buf.text, "all of its quantum");
			//buf.time = q;
		}

		if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) {
			perror("user_proc: Error");
		}
	}

	shmptr->PIDmap[pid] = 0;

	// detaches shmseg from shared memory
	if (shmdt(shmptr) == -1) {
		perror("user_proc: Error");
		exit(-1);
	}
}
