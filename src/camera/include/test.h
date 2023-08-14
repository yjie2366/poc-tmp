#ifndef _TEST_H_
#define _TEST_H_

#include "tsc.h"

struct audit_syscall {
	int number;
	int npkt;
	char *name;
	uint64_t *tm_percall;
};

#define UNIT	"usec"
#define SCALE	1000000

#define SYSCALL_NO(name) __NR_##name
#define SYSCALL_COUNT(list, dlmtr) ({ \
        int j = 0; \
        size_t len = strlen(list);\
        for (int i = 0; i < len; i++) \
                if ((list[i]==dlmtr) && (i!=0) && (i!=(len-1))) j++; \
        ++j;})

#define TIME_MAX(array, hz) ({ \
	uint64_t max = array[0];\
	for (int i = 1; i < iter; i++) { if (max < array[i]) max = array[i]; }\
	(double)max/(double)(hz/SCALE);})

#define TIME_MIN(array, hz) ({ \
	uint64_t min = array[0];\
	for (int i = 1; i < iter; i++) { if (min > array[i]) min = array[i]; }\
	(double)min/(double)(hz/SCALE);})

#define TIME_AVG(array, hz) ({\
	uint64_t total = 0, avg = 0;\
	for (int i = 0; i < iter; i++) total += array[i]; \
	avg = total / iter; \
	(double)avg/(double)(hz/SCALE);})

#define TIMEPC_INIT	uint64_t tp_st = 0, tp_end = 0
#define TIMEPC_START	{ tp_st = tick_time(); }
#define TIMEPC_END	{ tp_end = tick_time(); }

#ifdef NO_USLEEP
#define TIMEPC_RECORD(name) do { if (sysc->number == __NR_##name) \
			{sysc->tm_percall[i] = tp_end - tp_st; } \
			} while (0)
#else
#define TIMEPC_RECORD(name) do { if (sysc->number == __NR_##name) \
			{sysc->tm_percall[i] = tp_end - tp_st; } \
			usleep(50);} while (0)
#endif

#endif
