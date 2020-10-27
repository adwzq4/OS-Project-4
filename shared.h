// Author: Adam Wilson
// Date: 10/2/2020

#include <stdlib.h>
#include <limits.h>
#include <stdio.h> 

#define BILLION 1000000000

enum processClass { user, realTime };

struct mtime {
	int sec;
	long int ns;
};

struct msgbuf {
    long type;
    int pid;
    int time;
	char text[100];
};

struct PCB {
	int PID;
	int PPID;
	int priority;
	int state;
	enum processClass pClass;
	struct mtime cpuUsage;
	struct mtime lifetime;
	long int lastBurst;
};

struct shmseg {
	struct mtime currentTime;
	struct PCB processTable[18];
	int PIDmap[19];
};

struct Queue {
    int front, rear, size;
    int capacity;
    int* array;
};

struct mtime addTime(struct mtime, int, long);
int compareTimes(struct mtime, struct mtime);
struct Queue* createQueue();
int isFull(struct Queue*);
int isEmpty(struct Queue*);
void enqueue(struct Queue*, int);
int dequeue(struct Queue*);
int front(struct Queue*);
int rear(struct Queue*);