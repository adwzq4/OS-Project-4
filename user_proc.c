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

int main(int argc, char* argv[]) {
	struct msgbuf buf;
	struct mtime waitTil;
	int i, q, r, s, pid, shmid, msqid, term;
	const int TERMRATIO = 5;

	// get pid from execl parameter and seed rand() with pid * time
	pid = atoi(argv[0]);
	srand(pid * time(0));

	// create shared memory segment the same size as struct shmseg and get its shmid
	key_t shmkey = ftok("oss", 137);
	shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
	if (shmid == -1) {
		perror("user_proc: Error");
		exit(-1);
	}

	// attach struct pointer to shared memory segment
	struct shmseg* shmptr = shmat(shmid, (void*)0, 0);
	if (shmptr == (void*)-1) { perror("user_proc: Error"); }

	// attach to same message queue as parent
	key_t msqkey = ftok("oss", 731);
	msqid = msgget(msqkey, 0666 | IPC_CREAT);
	if (msqid == -1) { perror("user_proc: Error"); }

	term = 0;
	while (1) {
		term = rand() % TERMRATIO + 1;

		if (msgrcv(msqid, &buf, sizeof(struct msgbuf), pid, 0) == -1) { perror("user_proc: Error"); }

		q = buf.time;
		buf.type = 20;
		buf.pid = pid;

		if (term == TERMRATIO) {
			shmptr->processTable[pid-1].pState = terminated;
			strcpy(buf.text, "terminated");
			buf.time = rand() % q;
			if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) { perror("user_proc: Error"); }
			break;
		}

		if (shmptr->processTable[pid-1].pClass == user && rand() % 2 == 1) {
			r = rand() % 4;
			s = rand() % 1001;

			if (r == 4) {
				strcpy(buf.text, "part");
				buf.time = q * (rand() % 100 + 1) / 100;
			}
			else {
				strcpy(buf.text, "blocked");
				buf.time = 10;
				shmptr->processTable[pid-1].pState = blocked;
				waitTil = addTime(shmptr->currentTime, r, BILLION / s);
			}
		}

		else { strcpy(buf.text, "all"); }

		if (msgsnd(msqid, &buf, sizeof(struct msgbuf), 0) == -1) { perror("user_proc: Error"); }

		if (shmptr->processTable[pid-1].pState == blocked) {
			while (!compareTimes(shmptr->currentTime, waitTil));
			shmptr->processTable[pid-1].pState = ready;
		}
	}

	// detaches shmseg from shared memory
	if (shmdt(shmptr) == -1) {
		perror("user_proc: Error");
		exit(-1);
	}
}
