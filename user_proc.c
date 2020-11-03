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

// simulates an instance of either a user or real time process spawned by OSS
int main(int argc, char* argv[]) {
	struct msgbuf buf;
	struct mtime waitTil;
	int i, q, r, s, pid, shmid, msqid, term = 0;
	const int TERMRATIO = 5;

	// get pid from execl parameter
	pid = atoi(argv[0]);

	// create shared memory segment the same size as struct shmseg and get its shmid
	key_t shmkey = ftok("oss", 137);
	shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
	if (shmid == -1) {
		perror("user_proc: Error");
		exit(-1);
	}

	// attach struct pointer to shared memory segment and seed rand() with pid * time
	struct shmseg* shmptr = shmat(shmid, (void*)0, 0);
	if (shmptr == (void*)-1) { perror("user_proc: Error"); }
	srand(pid * shmptr->currentTime.sec);

	// attach to same message queue as parent
	key_t msqkey = ftok("oss", 731);
	msqid = msgget(msqkey, 0666 | IPC_CREAT);
	if (msqid == -1) { perror("user_proc: Error"); }

	// check to see if process has been scheduled until process terminates
	while (1) {
		// wait til a message is received giving a timeslice
		if (msgrcv(msqid, &buf, sizeof(struct msgbuf), pid, 0) == -1) { perror("user_proc: Error"); }
		q = buf.time;
		buf.type = 20;
		buf.pid = pid;

		// 20% chance of terminating each time a process is scheduled
		term = rand() % TERMRATIO + 1;
		// change state to terminated, send message back informing of termination, then break loop
		if (term == TERMRATIO) {
			shmptr->processTable[pid-1].pState = terminated;
			strcpy(buf.text, "terminated");
			buf.time = rand() % q;
			if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) { perror("user_proc: Error"); }
			break;
		}

		// if this is a user process, it will be either blocked or preempted 50% of the time
		if (shmptr->processTable[pid-1].pClass == user && rand() % 2 == 1) {
			r = rand() % 4;
			s = rand() % 1001;

			// gets preempted 12.5% of the time, running for [1-99]% of its quantum
			if (r == 3) {
				strcpy(buf.text, "part");
				buf.time = q * (rand() % 100 + 1) / 100;
			}
			// gets blocked 37.5% of the time, changing state to blocked, and waiting til current time + 0-2.999 s
			else {
				strcpy(buf.text, "blocked");
				buf.time = 1000;
				shmptr->processTable[pid-1].pState = blocked;
				waitTil = addTime(shmptr->currentTime, r, BILLION / s);
			}
		}

		// otherwise, the process uses its whole timeslice
		else { strcpy(buf.text, "all"); }

		// send a message back to OSS telling what happened
		if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) { perror("user_proc: Error"); }

		// if in blocked state, enter while loop until wait period is up, update process's timeBlocked, and change state to ready
		if (shmptr->processTable[pid-1].pState == blocked) {
			while (!compareTimes(shmptr->currentTime, waitTil));
			shmptr->processTable[pid-1].timeBlocked = addTime(shmptr->processTable[pid-1].timeBlocked, r, BILLION / s);
			shmptr->processTable[pid-1].pState = ready;
		}
	}

	// detaches shmseg from shared memory
	if (shmdt(shmptr) == -1) {
		perror("user_proc: Error");
		exit(-1);
	}
}
