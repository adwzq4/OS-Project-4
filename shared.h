// Author: Adam Wilson
// Date: 10/2/2020

#include <stdlib.h>
#include <limits.h>
#include <stdio.h> 

#define BILLION 1000000000

// process classes
enum procClass { user, realTime };

// process states
enum procState { run, ready, blocked, terminated };

// time struct
struct mtime {
	int sec;
	long int ns;
};

// holds cumulative statistics
struct statistics {
	struct mtime lifetime;
	struct mtime active;
	struct mtime timeBlocked;
	struct mtime OSactive;
	int numComplete;
};

// holds message contents/info
struct msgbuf {
    long type;
    int pid;
    int time;
	char text[100];
};

// process control block
struct PCB {
	int PID;
	int PPID;
	int priority;
	enum procState pState;
	enum procClass pClass;
	struct mtime cpuUsage;
	struct mtime lifetime;
	struct mtime timeBlocked;
	long int burst;
};

// shared memory segment
struct shmseg {
	struct mtime currentTime;
	struct PCB processTable[18];
	int PIDmap[19];
};

// queue struct
struct Queue {
    int front, rear, size;
    int capacity;
    int* array;
};

void mWait(int*);
struct mtime addTime(struct mtime, int, long);
int compareTimes(struct mtime, struct mtime);
double timeToDouble(struct mtime);
struct Queue* createQueue();
int isFull(struct Queue*);
int isEmpty(struct Queue*);
void enqueue(struct Queue*, int);
int dequeue(struct Queue*);
int front(struct Queue*);
int rear(struct Queue*);