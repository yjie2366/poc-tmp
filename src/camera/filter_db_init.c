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

#include "filter.h"

#define PIPE_SIZE 65536

int stop_reading = 0;
int num_exp = 0;

syscall_set file_syscall_set;
syscall_set fd_syscall_set;

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
	int stderr_pipe[2] = { 0 };
	struct sigaction sa;

	if (argc < 2) {
		fprintf(stderr, "Specify application path\n");
		return -1;
	}

	char *log_buf = NULL;
	char *strace_bin = "strace";
	char **strace_cmd = { 0 }; // { strace_bin, "-f", "-v", argv[1], NULL };

#define STRACE_NUM_ARGS 4
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
	strace_cmd[i++] = "--decode-fds";

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
		alarm(10);

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
			else if (ret < 0 && errno != EINTR) {
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

#define NUM_ARGS 7
static int extract_arguments(char *str, struct strace_line *trace)
{
	int i, num_args = 0; 
	char *args_tmp[NUM_ARGS] = { 0 };
	char *tmp_str = strdup(str);
	int offset = 0, ret = 0;

	char *end_args = NULL, *cur_arg = NULL;
	char *start_args = strchr(tmp_str, '(');
	if (start_args) {
		offset = find_last_char(start_args, ')');
		if (offset < 0) {
			// unfinished
			if (strstr(start_args, "unfinished")) {
				struct strace_unfinished *node = create_unfinished_node(trace);
				if (!node) {
					perror("Failed to create node: ");
					return -1;
				}

				enqueue_unfinished(node);
				trace->unfinished = 1;

				/*TODO: process unfinished arguments */
			}
			else {
				fprintf(stderr, "Error parsing line: %s\n", tmp_str);
				return -1;
			}
		}
		else {
			end_args = start_args + offset;
		}
		cur_arg = start_args;
	}
	else {
		// a line with no argument; two possibilities:
		// 1. +++ exited with 0 +++, +++ killed by <signal> +++
		// 2. <... <syscall> resume > = retval
		//  2.1 first half is before "<unfinished ...>"
		if (strstr(tmp_str, "resumed")) {
			/*TODO: record the rest of part */
		}
		else if (strstr(tmp_str, "+++")) { 
			// skip for now
			return 0; 
		}
		else {
			fprintf(stderr, "Error parsing line: %s\n", tmp_str);
			return -1;
		}
	}

	for (i = 0; i < NUM_ARGS; i++) {
		unsigned int tmp_len = 0;

		while (cur_arg != end_args) {
			if (*cur_arg == '[') {
				/* find its matching bracket */
				char *end_bracket = strchr(cur_arg, ']');
				tmp_len = end_bracket - cur_arg + 1; 

				args_tmp[i] = malloc(tmp_len + 1);
				if (!args_tmp[i]) {
					perror("Unable to extract path: malloc():");
					return -1;
				}
	
				memcpy(args_tmp[i], cur_arg, tmp_len);
				args_tmp[i][tmp_len] = '\0'; // replace the ',' with '\0'
				break;
			}
			else if (*cur_arg == '{') {
				/* find its matching brace */
				char *end_brace = strchr(cur_arg, '}');
				tmp_len = end_brace - cur_arg + 1; 

				args_tmp[i] = malloc(tmp_len + 1);
				if (!args_tmp[i]) {
					perror("Unable to extract path: malloc():");
					return -1;
				}
	
				memcpy(args_tmp[i], cur_arg, tmp_len);
				args_tmp[i][tmp_len] = '\0'; // replace the ',' with '\0'

				break;
			}
			else if (*cur_arg == ' ' || *cur_arg == ',') { 
				offset++;
				cur_arg = start_args + offset;
//				printf("cur_arg: %c\n", *cur_arg); 
			}
			else {
				char *end_arg = strchr(cur_arg, ',');
				if (!end_arg) {
					// last argument
					end_arg =  end_args;
				}
				tmp_len = end_arg - cur_arg; 

				args_tmp[i] = malloc(tmp_len + 1);
				if (!args_tmp[i]) {
					perror("Unable to extract path: malloc():");
					return -1;
				}
				memcpy(args_tmp[i], cur_arg, tmp_len); // ignore the trailing ',' or ')'
				args_tmp[i][tmp_len] = '\0'; 
				break;
			}
		} 
		offset += tmp_len;
		cur_arg = start_args + offset;
	}


	for (i = 0; i < NUM_ARGS; i++) {
		if (args_tmp[i]) { 
			free(args_tmp[i]);
		}
	}
	
	free(tmp_str);

	return 0;
}

static int extract_syscall_number(char *str, unsigned int *sysno)
{
	int ret = 0, _sysno = -1;
	char sysno_str[8] = { 0 };
	int offset_sysno_st, len_sysno;
	char *pattern_sysno =
//		"^\\[\\s*([0-9]+)\\]\\s(\\w+)\\(", // extract syscall no. & name
		"\\[\\s*([0-9]+)\\]" // extract syscall number only 
	;
	regex_t regex_sysno;
	regmatch_t matches[2] = { 0 }; // idx 0 is for matching the whole regex
	
	// extract sysno and syscall name from log
	ret = regcomp(&regex_sysno, pattern_sysno, REG_EXTENDED);
	if (ret) {
		regex_printerr(&regex_sysno, ret);
		goto out;
	}

	ret = regexec(&regex_sysno, str, 2, matches, 0);
	if (ret) {
//		if (ret == REG_NOMATCH) {
//			line = strtok(NULL, delim);
//			continue;
//		}
		regex_printerr(&regex_sysno, ret);
		goto out;
	}

	// matches[0] is for the overall regex match
	offset_sysno_st = matches[1].rm_so;
	len_sysno = matches[1].rm_eo - matches[1].rm_so; 
	memcpy(sysno_str, str+offset_sysno_st, len_sysno);
	sysno_str[len_sysno] = '\0';
	_sysno = atoi(sysno_str);

out:
	regfree(&regex_sysno);
	*sysno = _sysno;
//	printf("line: %s Sysno: %.*s\n", line, len_sysno, line+offset_sysno_st); //printf specifiers
//	printf("Sysno: %d sysname: %s\n", sysno, systable[sysno].name);

	return ret;
}

/* use inode to identify file */
int log_process(char *log)
{
	int ret = 0;
	char *delim = "\n";

	// preallocate 8192 lines
	int num_line = 0;
	struct strace_line *traces = calloc(8192, sizeof(struct strace_line));
	if (!traces) {
		perror("calloc failed: ");
		return -1;
	}

	char *line = strtok(log, delim);

	while (line) {
		unsigned int type = 0, sysno = -1;

		ret = extract_syscall_number(line, &sysno);
		if (ret == REG_NOMATCH) {
			line = strtok(NULL, delim);
			continue;
		}
		else if (ret) {
			fprintf(stderr, "Unable to extract syscall number!\n");
			return ret;
		}

		traces[num_line].sysno = sysno;

		ret = extract_arguments(line, traces + num_line);

//		type = systable[sysno].type;
//		if (type & SYSCALL_PATH) {
//		}
//		if (type & SYSCALL_FD) {
//
//		}
//		if (type & SYSCALL_ADDR) {
//
//		}
		
		line = strtok(NULL, delim);
	}

	free(traces);

	return 0;
}

int initialize_syscall_set()
{
	int i;

	SYSCALL_SET_ZERO(&file_syscall_set);
	SYSCALL_SET_ZERO(&fd_syscall_set);
	
	for (i = 0; i < SYSCALL_MAX; i++) {
		if (systable[i].type & SYSCALL_PATH) {
			SYSCALL_SET(i, &file_syscall_set);
		}
	}
//	printf("%lx\n", file_syscall_set.__fds_bits[0]);

	return 0;
}

int main(int argc, char **argv)
{
	int ret = 0;
	char *log = NULL;

	// 60s profiling
	ret = get_strace_log(argc, argv, &log);
	if (ret == -1) {
		fprintf(stderr, "get_strace_log error\n");
		return -1;
	}
	
	initialize_syscall_set();
//	fprintf(stderr, "%s", log);
	log_process(log);
//	create_json();
	
	// finalize profiling
	if (log) free(log);

	return 0;
}
