#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <json-c/json.h>
#include <regex.h>
#include <openssl/md5.h>

#include "filter.h"

#define PIPE_SIZE 65536

int stop_reading = 0;
int num_exp = 0;

static void sigalarm_handler(int sig)
{
	stop_reading = 1;
}

static int poll_event(short events)
{
	int i;
	short filtered_event[6] = { 
		POLLIN, POLLPRI, POLLOUT,
		POLLERR, POLLHUP, POLLNVAL
	}; 

	for (i = 0; i < 6; i++) {
		switch (events & filtered_event[i]) {
			case POLLIN:
				printf("POLLIN\n");
				break;
			case POLLPRI:
				printf("POLLPRI\n");
				break;
			case POLLOUT:
				printf("POLLOUT\n");
				break;
			case POLLERR:
				printf("POLLERR\n");
				break;
			case POLLHUP:
				printf("POLLHUP\n");
				break;
			case POLLNVAL:
				printf("POLLNVAL\n");
				break;
			case 0:
				break;
			default:
				fprintf(stderr, "Wrong poll event\n");
				return -1;	
		}
	}

	return 0;
}

static char *expand_buffer(char *old_buf, size_t *buf_size)
{
	char *buf = NULL;
	size_t old_buf_size = *buf_size;
	size_t _buf_size = old_buf_size;

	_buf_size += PIPE_SIZE;
	num_exp++;

	buf = realloc(old_buf, _buf_size);
	if (!buf) {
		perror("Unable to realloc:");
		return NULL;
	}

	memset(buf+old_buf_size, 0, PIPE_SIZE);
	*buf_size = _buf_size;

	return buf;
}

int get_strace_log(int argc, char **argv, char **buffer)
{
	int ret = 0, i = 0, num_arg;
	pid_t pid = 0;
	int stderr_pipe[2];
	struct sigaction sa;

	if (argc < 2) {
		fprintf(stderr, "Specify application path\n");
		return -1;
	}

	char *log_buf = NULL;
	char *strace_bin = "strace";
	char **strace_cmd = { 0 }; // { strace_bin, "-f", "-v", argv[1], NULL };

#define STRACE_NUM_ARGS 3
	num_arg = argc + STRACE_NUM_ARGS; 
	strace_cmd = malloc(sizeof(char *) * (num_arg + 1));
	if (!strace_cmd) {
		perror("malloc for strace_cmd failed:");
		return -1;
	}
	memset(strace_cmd, 0, sizeof(char *) * (num_arg + 1));

	strace_cmd[i++] = strace_bin;
	strace_cmd[i++] = "-f";
	strace_cmd[i++] = "-v";
	strace_cmd[i++] = "-n";

	// add application and its arguments
	for (; i < num_arg; i++) {
		strace_cmd[i] = argv[i-STRACE_NUM_ARGS];
	}
	strace_cmd[i] = NULL;
//DEBUG
/*
{
	int i;
	printf("number of args: %d\n", num_arg);
	for (i = 0; i < num_arg; i++) {
		if (!strace_cmd[i]) break;
		printf("%s ", strace_cmd[i]);
	}
	printf("\n");
}
*/

	sa.sa_flags = 0;
	sa.sa_handler = sigalarm_handler;
	sigemptyset(&sa.sa_mask);

	ret = sigaction(SIGALRM, &sa, NULL);
	if (ret == -1) {
		perror("sigaction failed:");
		return -1;
	}

	ret = pipe(stderr_pipe);
	if (ret == -1) {
		perror("Failed to create pipe\n");
		return -1;
	}

	pid = fork();
	// child
	if (!pid) {
		close(stderr_pipe[0]);
		fclose(stderr);
		dup(stderr_pipe[1]);
		close(stderr_pipe[1]);
		
		execvp(strace_bin, strace_cmd);
		perror("Failed to execute client application: ");
	}
	// parent
	else if (pid > 0) {
		struct pollfd pipe = { 0 };
		size_t buf_size = PIPE_SIZE;		
		size_t offset = 0;
		ssize_t len = 0;

		pipe.fd = stderr_pipe[0];
		pipe.events = POLLIN;

		close(stderr_pipe[1]);

		log_buf = malloc(buf_size);
		if (!log_buf) {
			perror("malloc failed:");
			return -1;
		}
		memset(log_buf, 0, buf_size);
		// record for 5 seconds
		alarm(5);

		while (!stop_reading) {
			ret = poll(&pipe, 1, 5000);
			if ((ret > 0) && (pipe.revents & POLLIN)) {
				char buf[PIPE_SIZE] = { 0 };

  				// read from pipe
				len = read(stderr_pipe[0], buf, PIPE_SIZE);
				if (len <= 0) {
					kill(pid, SIGKILL);
					perror("Read from pipe failed");
					return -1;
				}
				else {
					// if preallocated buffer space is not enough, expand
					if ((offset+len) > buf_size) {
						log_buf = expand_buffer(log_buf, &buf_size);
						if (!log_buf) {
							fprintf(stderr, "Failed to realloc buffer\n");
							kill(pid, SIGKILL);
							return -1;
						}
					}
					
					memcpy(log_buf + offset, buf, len);
					offset += len;
				}
			}
			else if ((ret > 0) && (pipe.revents & POLLHUP)) {
				char buf[PIPE_SIZE] = { 0 };
				len = read(stderr_pipe[0], buf, PIPE_SIZE);
				if (len > 0) {
					if ((offset+len) > buf_size) {
						log_buf = expand_buffer(log_buf, &buf_size);
						if (!log_buf) {
							fprintf(stderr, "Failed to realloc buffer\n");
							kill(pid, SIGKILL);
							return -1;
						}
					}
					
					memcpy(log_buf + offset, buf, len);
					offset += len;
				}

				break;
			}
			else if (ret < 0) {
				kill(pid, SIGKILL);
				perror("Poll failed:");
				return -1;
			}
		} 
		
		kill(pid, SIGKILL);
	}
	else {
		fprintf(stderr, "Failed to create child process\n");
		return -1;
	}

	free(strace_cmd);
	// reset signal handler of SIGALRM
	signal(SIGALRM, SIG_DFL);

	*buffer = log_buf;

	return 0;
}
/*
int create_json(char *filename, int syscalls[])
{
	json_object *test = json_object_new_object();
	json_object *jstring = json_object_new_string(filename);
	json_object *jint = json_object_new_int(5);

	json_object_object_add(test, "string", jstring);
	json_object_object_add(test, "int", jint);

	printf("test object:\n%s\n", json_object_to_json_string(test));

	return 0;
}
*/

/* use inode to identify file */
int log_process(char *log)
{
	int ret, i;
	char *delim = "\n";
	regex_t regex[2] = { 0 };
	char *patterns[] = {
//		"^\\[\\s*([0-9]+)\\]\\s(\\w+)\\(", // extract syscall no. & name
		"\\[\\s*([0-9]+)\\]", // extract syscall number only 
		"\"([\\w|\\/]*)\"",
		NULL
	};

	// extract sysno and syscall name from log
	ret = regcomp(&regex[0], patterns[0], REG_EXTENDED);
	if (ret) {
		char msg[256] = { 0 };
		regerror(ret, &regex[0], msg, 256);
		fprintf(stderr, "regcomp failed: %s\n", msg);
		return -1;
	}

	char *line = strtok(log, delim);
	while (line) {
		int sysno = 0; char sysno_c[4] = { 0 };
		int offset_sysno_st, len_sysno;
		regmatch_t matches[2] = { 0 }; // idx 0 is for matching the whole regex

		ret = regexec(&regex[0], line, 2, matches, 0);
		if (ret) {
			if (ret == REG_NOMATCH) {
				line = strtok(NULL, delim);
				continue;
			}

			char msg[256] = { 0 };
			regerror(ret, &regex[0], msg, 256);
			fprintf(stderr, "regexec failed: %s\n", msg);
			return -1;
		}

		offset_sysno_st = matches[1].rm_so;
		len_sysno = matches[1].rm_eo - matches[1].rm_so; 
		//printf("Sysno: %.*s\n", len_sysno, line+offset_sysno_st); //printf specifiers

		memcpy(sysno_c, line+offset_sysno_st, len_sysno);
		sysno_c[len_sysno] = '\0';

		sysno = atoi(sysno_c);
		printf("Sysno: %d sysname: %s\n", sysno, sysname[sysno].name);

		line = strtok(NULL, delim);
	}

	regfree(&regex[0]);

	return 0;
}

int main(int argc, char **argv)
{
	int ret = 0;
	char *log = NULL;

	// Free log memory before ends
	ret = get_strace_log(argc, argv, &log);
	if (ret == -1) {
		fprintf(stderr, "get_strace_log error\n");
		return -1;
	}

//	fprintf(stderr, "%s", log);
	log_process(log);
//	create_json();
	
	// finalize profiling
	if (log) free(log);

	return 0;
}
