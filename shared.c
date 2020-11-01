// Author: Adam Wilson
// Date: 12/7/1941

#include <stdlib.h>
#include <limits.h>
#include <stdio.h> 
#include "shared.h"

// adds two mtime structs together
struct mtime addTime(struct mtime t1, int sec, long ns) {
    t1.sec += sec;
    t1.ns += ns;
    if (t1.ns >= BILLION) {
        t1.ns -= BILLION;
        t1.sec++;
    }

    return t1;
}

// compares two mtime structs
int compareTimes(struct mtime t1, struct mtime t2) {
    if (t1.sec < t2.sec || t1.sec == t2.sec && t1.ns < t2.ns) { return 0; }
    else { return 1; }
}

// converts an mtime struct to a double
double timeToDouble(struct mtime t){
    return t.sec + (double)t.ns / BILLION;
}

// function to create a queue 
// of given capacity. 
// It initializes size of queue as 0 
struct Queue* createQueue() {
    struct Queue* queue = (struct Queue*)malloc(
        sizeof(struct Queue));
    queue->capacity = 20;
    queue->front = queue->size = 0;

    queue->rear = 19;
    queue->array = (int*)malloc(
        queue->capacity * sizeof(int));
    return queue;
}

// Queue is full when size becomes 
// equal to the capacity 
int isFull(struct Queue* queue) {
    return (queue->size == queue->capacity);
}

// Queue is empty when size is 0 
int isEmpty(struct Queue* queue) {
    return (queue->size == 0);
}

// Function to add an item to the queue. 
// It changes rear and size 
void enqueue(struct Queue* queue, int item) {
    if (isFull(queue)) { return; }
    queue->rear = (queue->rear + 1)
        % queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
}

// Function to remove an item from queue. 
// It changes front and size 
int dequeue(struct Queue* queue) {
    if (isEmpty(queue)) { return INT_MIN; }
    int item = queue->array[queue->front];
    queue->front = (queue->front + 1)
        % queue->capacity;
    queue->size = queue->size - 1;
    return item;
}

// Function to get front of queue 
int front(struct Queue* queue) {
    if (isEmpty(queue)) { return INT_MIN; }
    return queue->array[queue->front];
}

// Function to get rear of queue 
int rear(struct Queue* queue) {
    if (isEmpty(queue)) { return INT_MIN; }
    return queue->array[queue->rear];
}
