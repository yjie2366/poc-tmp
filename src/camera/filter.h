#ifndef _FILTER_H_
#define _FILTER_H_

#include <sys/select.h>
#include <stdlib.h>
#include "filter_sysname.h"

typedef fd_set syscall_set;

struct strace_line {
	int sysno;
	int unfinished;
	char *args[7];
};

struct strace_unfinished {
	struct strace_line *line;
	struct strace_unfinished *next, *prev;
};
struct strace_unfinished *head = NULL;
struct strace_unfinished *q_unfinished = NULL;

static inline struct strace_unfinished *create_unfinished_node(struct strace_line *line) {
	struct strace_unfinished *node = NULL;
	node=malloc(sizeof(struct strace_unfinished));
	if (!node) { perror("malloc failed: "); return NULL; }

	node->line = (struct strace_line *)line;
	node->next = NULL;
	node->prev = NULL;

	return node; 
}

#define enqueue_unfinished(node) do {\
if (!head) {head=node; q_unfinished=node;} \
else { head->next=node; node->prev=head; head=node;}\
} while (0)

#define dequeue_unfinished(node) do {\
struct strace_unfinished *prev_node = node->prev, *next_node = node->next;\
if (prev_node) {\
	if (!next_node) { head = prev_node; prev_node->next = NULL; }\
	else { prev_node->next = next_node; next_node->prev = prev_node; }\
} \
else { q_unfinished = next_node; if (next_node) next_node->prev = NULL; } \
free(node);\
} while (0)

#define SYSCALL_SET_ZERO(set) FD_ZERO(set)
#define SYSCALL_SET(number, set) FD_SET(number, set)
#define SYSCALL_ISSET(number, set) FD_ISSET(number, set)
#define SYSCALL_CLR(number, set) FD_CLR(number, set)

/* Regex */
#define regex_printerr(regex_addr, ret) do {\
	char msg[256] = { 0 }; \
	regerror(ret, regex_addr, msg, 256); \
	fprintf(stderr, "regexec failed: %s\n", msg); \
} while(0)

#define find_last_char(str, chr) ({\
int len = strlen(str), i = len-1;\
for (; (str[i]!=chr) && (i>=0);i--) continue;\
i;})

#define NUM_ARGS 7

#endif
