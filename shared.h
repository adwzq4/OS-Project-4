// Author: Adam Wilson
// Date: 10/2/2020

#include <stdlib.h>

#define BILLION 1000000000

enum processClass { user, realTime };

struct mtime {
	int sec;
	long int ns;
};

struct msgbuf {
	long type;
	char text[200];
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
	struct PCB processTable[19];
	int PIDmap[20];
};