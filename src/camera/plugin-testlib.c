#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <libaudit.h>
#include <auparse.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include "test.h"
#include "sysname.h"
#include "mqtt_testlib.h"

#define MEASURE_FINISH_SYSNAME	"gettid"
#define MEASURE_FINISH_SYSCALL	{ gettid(); }

#define STACK_SIZE	(2*1024*1024)
#define STACK_TOP(addr)	((addr) + STACK_SIZE)

int	num_sysc = 0;
volatile int stop = 0, hup = 0;
int mqtt_on = 0; // MQTT off by default
int iter = 1;
int mrkr = -1;
static char *mrkr_name = NULL;
volatile int finish = 0;

struct audit_syscall *sysc_list = NULL;
static uint64_t hz = 0;
static unsigned int num_rules = 0;
static struct audit_rule_data **rules = NULL;
void *stack = NULL;
static pthread_mutex_t mx1;
static pthread_mutex_t mx2;

/*
struct workload_args {
	int cpu;
} clone_args = { .cpu = -1 };
*/
static int clone_cpu = -1;
static int clone_pid = 0;

/* TODO: support flexible arguments for marker system call */
int invoke_marker(void)
{
	long rc = 0;

	if (mrkr != -1) {
		rc = syscall(mrkr);
		if (rc == -1) {
			perror("Marker syscall failed: ");
			goto out;
		}
	}
	else {
		fprintf(stderr, "[WARNING] No marker syscall specified\n");
	}

out:
	return rc;
}

int search_syscall(char *snam)
{
	int	snum;
	for (snum = 0; snum < SYSCALL_MAX; snum++) {
		if (!strcmp(snam, sysname[snum])) {
			goto find;
		}
	}
	/* not found */
	snum = -1;
find:
	return snum;
}

unsigned int core_bind(int _cpu, pid_t pid)
{
	int		rc = 0;
	cpu_set_t   nmask;
	unsigned int cpu = 0;

	if (_cpu < -1) {
		/* just return the current core */
		goto skip;
	}
	else if (_cpu == -1) { // set all the cores
		int i;

		CPU_ZERO(&nmask);
		for (i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++) {
			CPU_SET(i, &nmask);
		}
			
	}
	else {
		cpu = _cpu;

		CPU_ZERO(&nmask);
		CPU_SET(cpu, &nmask);
	}

	rc = sched_setaffinity(pid, sizeof(nmask), &nmask);
	if (rc < 0) {
		printf("Failed to bind to CPU %d\n", cpu);
		perror("sched_setaffinity: ");
	}

skip:
	getcpu(&cpu, NULL);
	return cpu;
}

unsigned int core_exclude(int *cpus, int num_cpus, pid_t pid)
{
	int rc = 0;
	cpu_set_t nmask;
	unsigned int cpu = 0;

	if (num_cpus <= 0) {
		goto skip;
	}
	else {
		int i;
		
		CPU_ZERO(&nmask);

		// set all the cores
		for (i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++) {
			CPU_SET(i, &nmask);
		}

		// clear the bit of the excluded cpus 
		for (i = 0; i < num_cpus; i++) {
				CPU_CLR(cpus[i], &nmask);
		}
	}

	rc = sched_setaffinity(pid, sizeof(nmask), &nmask);
	if (rc < 0) {
		printf("Failed to exclude CPU %d\n", cpu);
		perror("sched_setaffinity: ");
	}

skip:
	getcpu(&cpu, NULL);
	return cpu;
}

static void term_handler(int sig)
{
    stop = 1;
}

static void hup_handler(int sig)
{
    printf("SIGHUP received\n"); fflush(stdout);
    hup = 1;
}

static void child_handler(int sig)
{
    printf("Child exiting\n"); fflush(stdout);
    finish = 1;
}

/* Register sighandlers */
static void register_sighandlers(void)
{
	struct sigaction sa;

	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);

	sa.sa_handler = term_handler;
	sigaction(SIGTERM, &sa, NULL);

	sa.sa_handler = hup_handler;
	sigaction(SIGHUP, &sa, NULL);
	
	sa.sa_handler = child_handler;
	sigaction(SIGCHLD, &sa, NULL);
}

static int _parse_syscall_list(char *list, char delim)
{
	int num = 0;
	char *cur = list;
	char *next = NULL;
	char *pd = strchr(cur, delim);

	do {
		if (pd) {
			next = pd + 1;
			*pd = '\0';
		}
		else {
			next = NULL;
		}

		sysc_list[num].name = strdup(cur);
		if (strcmp(cur, "all")) {
			int no = search_syscall(cur);
			if (no < 0) {
				fprintf(stderr, "Syscall %s not supported.\n", cur);
				num = -1;
				goto out;
			}
			sysc_list[num].number = no;
		}
		else {
			// no system call number if 'all' is specified
			sysc_list[num].number = -1;
		}

		cur = next;
		if (cur != NULL)
			pd = strchr(cur, delim); // find next
		else 
			pd = NULL;

		num++;
	} while (next);

out:
	return num;
}

static int parse_syscall_list(char *list, char delim)
{
	int _num_sysc = 0, rc = 0;

	if (sysc_list) {
		fprintf(stderr, "System call list is not empty\n");
		exit(-1);
	}

	char *sysnames = strdup(list);
	if(!sysnames) {
		perror("strdup failed:");
		exit(-1);
	}

	_num_sysc = SYSCALL_COUNT(sysnames, delim);
	if (!_num_sysc) {
		fprintf(stderr, "Incorrect system call list: %s\n", list);
		exit(-1);
	}

	// Allocate space for specified system calls not including marker
	sysc_list = malloc(sizeof(struct audit_syscall) * _num_sysc);
	if (!sysc_list) {
		perror("malloc failed: ");
		rc = -1;
		goto out;
	}
	memset(sysc_list, 0, sizeof(struct audit_syscall) * _num_sysc);

	rc = _parse_syscall_list(list, delim);
	if (rc != _num_sysc) {
		fprintf(stderr, "Error occured while parsing syscall list\n");
		rc = -1;
		goto out;
	}

out:
	if (sysnames) free(sysnames);
	return rc;
}

static void init_args(char *argv1)
{
	char *arg_list = NULL,
	     *delim = NULL,
	     *next = NULL,
	     *start = NULL;
	
	// Get CPU frequency
	hz = tick_helz(0);
	if (!hz) {
		fprintf(stderr, "Cannot obtain CPU frequency\n");
		exit(-1);
	}

	// Parse plugin argument list
	arg_list = strdup(argv1);

	start = arg_list;
	
loop:
	delim = strchr(start, ',');
	if (delim) {
		next = delim + 1;
		*delim = '\0';
	}
	else {
		next = NULL;
	}

	{
		char *substr = strdup(start);
		char *subdelim = strchr(substr, '=');
		char *key = substr;
		char *value = subdelim + 1;

		*subdelim = '\0';
		
		// parse audit plugin args in config file
		// /usr/local/etc/audit/plugins.d/<PLUGIN-NAME>.conf
		if (!strcmp(key, "syscall")) {
			num_sysc = parse_syscall_list(value, ':');
			if (num_sysc < 0) {
				fprintf(stderr, "Parsing syslist failed\n");
				exit(-1);
			}
		}
		else if (!strcmp(key, "cpu")) {
			clone_cpu = atoi(value);
		}
		else if (!strcmp(key, "mqtt")) {
			mqtt_on = atoi(value);
		}
		/* For syscall test only ? */
		else if (!strcmp(key, "iter")) {
			iter = atoi(value);
		}
		else {
			fprintf(stderr, "Invalid arguments: %s=%s\n", key, value);
			exit(-1);
		}
		if (substr) free(substr);
	}

	if (next) {
		start = next;
		goto loop;
	}

	if (!sysc_list) {
		fprintf(stderr, "Must specify system calls or 'all'!\n");
		exit(-1);
	}
	
	if (arg_list) free(arg_list);
	
	// Initialize tm_percall buffer for each syscall
	for (int i = 0; i < num_sysc; i++) {
		struct audit_syscall *sysc = &sysc_list[i];
		size_t tm_size = sizeof(uint64_t) * iter;

		sysc->tm_percall = malloc(tm_size);
		if (!sysc->tm_percall) {
			perror("malloc: ");
			exit(-1);
		}
		memset(sysc->tm_percall, 0, tm_size);
	}
}


static int init_auparselib(auparse_state_t **au, auparse_callback_ptr auparse_callback)
{
	int rc = 0;

	auparse_state_t *_au = auparse_init(AUSOURCE_FEED, 0);
	if (!_au) {
		perror("auparse_init failed: ");
		rc = -1;
		goto out;
	}
	
	// Set the end of event timeout value
	rc = auparse_set_eoe_timeout(2);
	if (rc) {
		perror("auparse_set_eoe_timeout: ");
		goto out;
	}
	// Add a callback handler for notification
	auparse_add_callback(_au, auparse_callback, NULL, NULL);

out:
	*au = _au;
	return rc;
}

/* preset audit rules */
int init_audit_rules(int *audit_fd, pid_t *target_pids, int num_pids)
{
	int fd = 0, rc = 0;
	int flag = AUDIT_FILTER_EXIT;

	if (!audit_fd) {
		fprintf(stderr, "Invalid argument audit_fd\n");	
		return -1;
	}
	
	if (num_pids <= 0) num_rules = 1;
	else num_rules = num_pids;

	rules = malloc(sizeof(char *) * num_rules);
	if (!rules) {
		perror("malloc failed");
		goto err;
	}

	fd = audit_open();
	if (fd < 0) {
		perror("audit_open failed: ");
		goto err;
	}

	/* The man page of audit_rule_create_data() is not written.
	 * It allocates a memory area using malloc(), and thus, it should be
	 * deallocated by free() */
	for (int i = 0; i < num_rules; i++) {
		rules[i] = audit_rule_create_data();
		if (rules[i] == NULL) {
			perror("audit_rule_create_data: ");
			goto err;
		}
	
		// syscall rule
		if (sysc_list && (num_sysc > 0)) {
			for (int j = 0; j < num_sysc; j++) {
				rc = 0;
				// DEBUG
				//fprintf(stderr, "Add syscall: %s to the audit rule\n", sysc_list[i].name);
				rc = audit_rule_syscallbyname_data(rules[i], sysc_list[j].name);
				if (rc) {
					perror("audit_rule_syscallbyname_data: ");
					goto err;
				}
			}
		
			/* add marker if necessary */
			if (mrkr_name) {
				rc = audit_rule_syscallbyname_data(rules[i], mrkr_name);
				if (rc) {
					perror("audit_rule_syscallbyname_data (marker): ");
					goto err;
				}
			}
		}
	
		// pid & ppid rule
		if (target_pids) {
			int len = 0;
			char pid_rule[32] = { 0 };
	
			len = snprintf(pid_rule, 32, "pid=%d", target_pids[i]);
			pid_rule[len] = '\0';
	
			rc = audit_rule_fieldpair_data(&rules[i], pid_rule, flag);
			if (rc < 0) {
				fprintf(stderr, "Failed to add pid rules\n");
				perror("audit_rule_syscallbyname_data: ");
				goto err;
			}
		}

		rc = audit_add_rule_data(fd, rules[i], flag, AUDIT_ALWAYS);
		if (rc <= 0) {
			perror("audit_add_rule_data: ");
			goto err;
		}
	}

	*audit_fd = fd;
	return fd;

err:
	if (rules) {
		for (int i = 0; i < num_pids; i++)
				if (rules[i]) free(rules[i]);
		free(rules);
		rules = NULL;
	}

	*audit_fd = -1;

	return -1;
}

int init_audit_rules_exe(int *audit_fd, char **target_exes, int num_exes, pid_t *target_pids, int num_pids)
{
	int fd = 0, rc = 0, i;
	int iter_exe = 0, iter_pid = 0;
	int flag = AUDIT_FILTER_EXIT;

	if (!audit_fd) {
		fprintf(stderr, "Invalid argument audit_fd\n");	
		return -1;
	}
	if (!num_exes && !num_pids) {
		num_rules = 1;
	}
	else {
		num_rules = num_exes + num_pids;
	}

	rules = malloc(sizeof(char *) * num_rules);
	if (!rules) {
		perror("malloc failed");
		goto err;
	}

	fd = audit_open();
	if (fd < 0) {
		perror("audit_open failed: ");
		goto err;
	}

	/* The man page of audit_rule_create_data() is not written.
	 * It allocates a memory area using malloc(), and thus, it should be
	 * deallocated by free() */
	for (i = 0; i < num_rules; i++) {
		rules[i] = audit_rule_create_data();
		if (rules[i] == NULL) {
			perror("audit_rule_create_data: ");
			goto err;
		}
	
		// syscall rule
		if (sysc_list && (num_sysc > 0)) {
			for (int j = 0; j < num_sysc; j++) {
				rc = 0;
				// DEBUG
				//fprintf(stderr, "Add syscall: %s to the audit rule\n", sysc_list[i].name);
				rc = audit_rule_syscallbyname_data(rules[i], sysc_list[j].name);
				if (rc) {
					perror("audit_rule_syscallbyname_data: ");
					goto err;
				}
			}
		
			/* add marker if necessary */
			if (mrkr_name) {
				rc = audit_rule_syscallbyname_data(rules[i], mrkr_name);
				if (rc) {
					perror("audit_rule_syscallbyname_data (marker): ");
					goto err;
				}
			}
		}
	
		// add exe rules
		if (target_exes && (iter_exe < num_exes)) {
			int len = 0;
			char exe_rule[4096] = { 0 };
	
			len = snprintf(exe_rule, 4096, "exe=%s", target_exes[iter_exe]);
			exe_rule[len] = '\0';
			
			rc = audit_rule_fieldpair_data(&rules[i], exe_rule, flag);
			if (rc < 0) {
				fprintf(stderr, "Failed to add exe rules\n");
				perror("audit_rule_syscallbyname_data: ");
				goto err;
			}
			
			iter_exe++;
		}
		else if (target_pids && (iter_pid < num_pids)) {
			int len = 0;
			char pid_rule[4096] = { 0 };

			len = snprintf(pid_rule, 4096, "pid=%d", target_pids[iter_pid]);
			pid_rule[len] = '\0';

			rc = audit_rule_fieldpair_data(&rules[i], pid_rule, flag);
			if (rc < 0) {
				fprintf(stderr, "Failed to add pid rules\n");
				perror("audit_rule_syscallbyname_data: ");
				goto err;
			}

			iter_pid++;
		}

		rc = audit_add_rule_data(fd, rules[i], flag, AUDIT_ALWAYS);
		if (rc <= 0) {
			perror("audit_add_rule_data: ");
			goto err;
		}
	}

	*audit_fd = fd;
	return fd;

err:
	if (rules) {
		for (int i = 0; i < num_rules; i++)
				if (rules[i]) free(rules[i]);
		free(rules);
		rules = NULL;
	}

	*audit_fd = -1;

	return -1;
}

// strdup() need to be freed
static char *get_my_ip(void)
{
	struct ifreq ifr;
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);

	ioctl(sock, SIOCGIFADDR, &ifr);
	close(sock);

	return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

int test_init(char *argv1, char *marker, void **mqtt_s, auparse_state_t **p_au, auparse_callback_ptr auparse_callback)
{
	int rc = 0;

	register_sighandlers();
	
	if (argv1) init_args(argv1);
	if (marker) {
		mrkr = search_syscall(marker);
		if (mrkr < 0) {
			fprintf(stderr, "Syscall %s is ignored because its not supported on your system\n", marker);
		}
		mrkr_name = strdup(marker);
	}

	if (p_au) {
		// initialize auparse library
		rc = init_auparselib(p_au, auparse_callback);
		if (rc) {
			perror("Failed to initialize auparselib: ");
			goto out;
		}
	}

	// initialize MQTT if needed
	if (mqtt_on && mqtt_s) {
		char *host = get_my_ip();
//		char *host = "192.168.151.36"; //test
		int port = 1883;
		int keepalive = 60;
		void *tmp_mqtt = NULL;

		tmp_mqtt = mqtt_init(host, port, keepalive, NULL, NULL, NULL);
		if (!tmp_mqtt) {
			fprintf(stderr, "Error in MQTT initialization\n");
			*mqtt_s = NULL;
			rc = -1;
			goto out;
		}

		/* start MQTT thread to process network traffic */
		mqtt_loop_start(tmp_mqtt);
		//fprintf(stderr, "Broker: connected\n");

		*mqtt_s = tmp_mqtt;
	}

	// Initialize mutex
	pthread_mutex_init(&mx1, 0);
	pthread_mutex_init(&mx2, 0);
	pthread_mutex_lock(&mx1);
	pthread_mutex_lock(&mx2);

out:
	return rc;
}

// clone worker thread
int generate_worker_thread(void *workload, void *child_arg)
{
	int	flags = CLONE_VM | CLONE_IO | SIGCHLD
		// | CLONE_NEWPID
		// | CLONE_FS | CLONE_IO
		// | CLONE_NEWUTS
		;
	stack = malloc(STACK_SIZE);
	if (!stack) {
		perror("Cannot allocate stack memory: ");
		clone_pid = -1;
		goto out;
	}
	memset(stack, 0, STACK_SIZE);

	clone_pid = clone((int (*)(void *))workload, STACK_TOP(stack), flags, child_arg);
	if (clone_pid == -1) {
		perror("clone failed to create a new thread: ");
		goto out;
	}

	clone_cpu = core_bind(clone_cpu, clone_pid);

out:
	return clone_pid;
}

// called by plugin
void start_work(void)
{
	pthread_mutex_unlock(&mx1);
}

// called by worker thread
void wait_for_release(void)
{
	pthread_mutex_lock(&mx1);
}

// called by worker thread
void finish_work(void)
{
	pthread_mutex_unlock(&mx1);
}

// called by worker thread
void wait_for_exit(void)
{
	pthread_mutex_lock(&mx2);
}


void finalize_worker_thread(void)
{
	pthread_mutex_unlock(&mx2);

	wait(NULL);
	
	if (stack) free(stack);
}

static void finalize_audit(int fd)
{
	int rc = 0;
	if (fd < 0) return;

	/* delete the audit rule */
	for (int i = 0; i < num_rules; i++) {
		if (rules[i]) {
			rc = audit_delete_rule_data(fd, rules[i], AUDIT_FILTER_EXIT, AUDIT_ALWAYS);
			if (rc <= 0) {
				perror("audit_delete_rule_data fails: ");
			}
			audit_rule_free_data(rules[i]);
		}
	}

	if (rules) free(rules);
	audit_close(fd);
}

static void finalize_workarea(void)
{
	pthread_mutex_destroy(&mx1);
	pthread_mutex_destroy(&mx2);
	fclose(stdin);
}

static void free_syscall_list(void)
{
	if (sysc_list) {
		for (int i = 0; i < num_sysc; i++) {
			char *name_tmp = sysc_list[i].name;
			uint64_t *tm_tmp = sysc_list[i].tm_percall;

			if (name_tmp) free(name_tmp);
			if (tm_tmp) free(tm_tmp);
		}
		free(sysc_list);
	}
	else {
		printf("System call list is empty\n");
	}
}

void test_finalize(int audit_fd, auparse_state_t *au, void *mqtt_inst)
{
	free_syscall_list();
	finalize_workarea();
	finalize_audit(audit_fd);

	if (mrkr_name) free(mrkr_name);
	if (au) {
		auparse_flush_feed(au);
		auparse_destroy(au);
	}
	if (mqtt_inst) {
		mqtt_finalize(mqtt_inst);
	}
}

void print_results(void)
{
	unsigned int cpu = -1, rc;
	time_t cur_raw = time(NULL);
	struct tm *local_time = localtime(&cur_raw);
	char *default_path = "/tmp/audit-plugin";

	rc = mkdir(default_path, S_IRWXU);
	if (rc && errno != EEXIST) {
		perror("Failed to make output directory: ");
		goto nofile;
	}

	rc = getcpu(&cpu, NULL);
	if (rc) { perror("getcpu: "); }	
	
	for (int i = 0; i < num_sysc; i++) {
		char datafile_name[1024] = { 0 };
		char info_name[1024] = { 0 };
		struct audit_syscall *sysc = &sysc_list[i];
		FILE *fp_d = NULL, *fp_i = NULL;
		// skip printing marker results
		if (sysc->number == mrkr) continue;

		// Generate data filename for each system call
		sprintf(datafile_name, "%s/%s-i%d-Plugin-%d-%d-%d-%d-%d-%d.csv",
			default_path, sysc->name, iter, local_time->tm_year, local_time->tm_mon,
			local_time->tm_mday, local_time->tm_hour, local_time->tm_min,
			local_time->tm_sec);
		
		fp_d = fopen(datafile_name, "w");
		if (!fp_d) {
			perror("fopen failed: ");
			goto nofile;
		}

		// Generate info filename for each system call
		sprintf(info_name, "%s/%s-i%d-Plugin-%d-%d-%d-%d-%d-%d-info.txt",
			default_path, sysc->name, iter, local_time->tm_year, local_time->tm_mon,
			local_time->tm_mday, local_time->tm_hour, local_time->tm_min,
			local_time->tm_sec);
		
		fp_i = fopen(info_name, "w");
		if (!fp_i) {
			perror("fopen failed: ");
			goto nofile;
		}

		fprintf(fp_i, "System call: %s Syscall No.: %d Audit: Plugin Main CPU: %d APPL CPU: %d\n",
				sysc->name, sysc->number, cpu, clone_cpu);
		
		for (int j = 0; j < iter; j++) {
			fprintf(fp_d, "%12.9f\n", (double)sysc->tm_percall[j]/(double)(hz / SCALE));
		}

		fprintf(fp_i, "Max: %12.9f\nMin:%12.9f\nAvg:%12.9f\n",
				TIME_MAX(sysc->tm_percall, hz),
				TIME_MIN(sysc->tm_percall, hz),
				TIME_AVG(sysc->tm_percall, hz));
		fclose(fp_d);
		fclose(fp_i);

nofile:
		fprintf(stderr, "Data output finished\n");
		fflush(stdout);
	}
}


