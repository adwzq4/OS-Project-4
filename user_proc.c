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
	int msqid, shmid;
	key_t shmkey, msqkey;
	int i;

	int pid = atoi(argv[0]);


	// create shared memory segment the same size as struct shmseg and get its shmid
	shmkey = ftok("oss", 137);
	shmid = shmget(shmkey, sizeof(struct shmseg), 0666 | IPC_CREAT);
	if (shmid == -1) {
		perror("oss: Error");
		exit(-1);
	}

	// attach struct pointer to shared memory segment
	struct shmseg* shmptr = shmat(shmid, (void*)0, 0);
	if (shmptr == (void*)-1) {
		perror("oss: Error");
		exit(-1);
	}

	//printf("\npri: %d\n", shmptr->processTable[pid - 1].priority);

	msqkey = ftok("oss", 731);
	msqid = msgget(msqkey, 0666 | IPC_CREAT);
	if (msqid == -1) {
		perror("oss: Error");
		exit(-1);
	}

	buf.type = 0;


	// detaches shmseg from shared memory
	if (shmdt(shmptr) == -1) {
		perror("oss: Error");
		exit(-1);
	}
}
