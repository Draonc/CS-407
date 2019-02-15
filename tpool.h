#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>


int tpool_init(void (*process_task)(int));

int tpool_add_task(int newtask);

