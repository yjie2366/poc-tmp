#define _GNU_SOURCE
#define __USE_GNU
#include <sched.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libaudit.h>
#include <auparse.h>
#include <getopt.h>
#include <pthread.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include "mqtt_testlib.h"
#include "plugin-testlib.h"

static volatile int start_workload = 0;

pid_t mypid = -1;
int audit_fd = -1;
auparse_state_t *au = NULL;
void *mqtt_inst = NULL;

//long num_event_libcam = 0;
//long num_event_gst = 0;
//long num_event_flask = 0;

#define MQTT_MSG_LEN_MAX ( MAX_AUDIT_MESSAGE_LENGTH + 128 )

static void handle_event(auparse_state_t *au, auparse_cb_event_t cb_event_type, void *user_data)
{
	int nrec = 0, tmp_len = 0, syscall = -1;
	char msg_buf[MQTT_MSG_LEN_MAX] = { 0 };

	if (cb_event_type != AUPARSE_CB_EVENT_READY)
		return;
	
	while (auparse_goto_record_num(au, nrec++) > 0) {
		int type;
		const char *p_field = NULL;

		type = auparse_get_type(au);

		if (type == AUDIT_SYSCALL) {
			int pid;
			
			p_field = auparse_find_field(au, "pid");
			if (!p_field) {
				fprintf(stderr, "[WARNING] Unable to find pid\n");
				continue;
			}

			pid = atoi(auparse_get_field_str(au));
			auparse_first_field(au);
/*
			if (pid == camera_pid) num_event_libcam++;
			else if (pid == gst_pid) num_event_gst++;
			else if (pid == control_pid) num_event_flask++;
*/
			p_field = auparse_find_field(au, "syscall");
			if (!p_field) {
				fprintf(stderr, "[WARNING] Unable to find syscall\n");
				continue;
			}

			syscall = atoi(auparse_get_field_str(au));
			auparse_first_field(au);
			
			tmp_len += snprintf(msg_buf + tmp_len, MQTT_MSG_LEN_MAX,
				"Message from %d:\t%s\t", pid, auparse_get_record_text(au));
		}
		else if (type == AUDIT_PATH) {
			p_field = auparse_find_field(au, "name");
			if (!p_field) {
				fprintf(stderr, "[WARNING] Unable to find name\n");
				continue;
			}

			tmp_len += snprintf(msg_buf + tmp_len, MQTT_MSG_LEN_MAX, "Opened filename: %s\t",
				auparse_get_field_str(au));
		}
		// follows an ioctl SYSCALL record
		else if ((type == AUDIT_KERNEL_OTHER) && (syscall == __NR_ioctl)) {
			tmp_len += snprintf(msg_buf + tmp_len, MQTT_MSG_LEN_MAX, "IOCTL data: %s\t", auparse_get_record_text(au));
		}
	}

	if (tmp_len) {
		if (mqtt_inst) {
//			struct mqtt_message_node *node = NULL;

//			node = mqtt_new_message(msg_buf, tmp_len, 1, "/audit/raw");
//			node = mqtt_new_message(msg_buf, tmp_len, 0, "/audit/raw");
//			if (node) mqtt_publish(mqtt_inst, node);
				mqtt_publish(mqtt_inst, "/audit/raw", tmp_len, msg_buf);
		}

		//fprintf(stderr, "%s\n", msg_buf);
	}
}

static void start_workload_handler(int sig)
{
	start_workload = 1;
}


static void register_handlers(void)
{
	struct sigaction sa1;
	
	sa1.sa_flags = 0;
	sigemptyset(&sa1.sa_mask);

	sa1.sa_handler = start_workload_handler;
	sigaction(SIGUSR2, &sa1, NULL);
}

static inline void poke_child(pid_t pid, int sig)
{
	kill(pid, sig);
}

#include <sys/resource.h>
static int set_pid_priority(pid_t pid, int nice)
{
	int ret = 0;
	id_t _pid = pid;	

	ret = setpriority(PRIO_PROCESS, _pid, nice);
	if (ret) {
		perror("ERROR: setpriority failed!");
	}

	return ret;
}

/*
	int sched_setscheduler(pid_t pid, int policy,
                       const struct sched_param *param);
*/
static int set_pid_schedpolicy(pid_t pid, int policy)
{
	int ret = 0;
	struct sched_param param = { 0 };
	
	switch (policy) {
		case SCHED_FIFO:
		case SCHED_RR:
			param.sched_priority = 99;
			break;
		default:
			break;
	}

	ret = sched_setscheduler(pid, policy, &param);
	if (ret) {
		perror("ERROR: setpriority failed!");
	}
	
	return ret;
}

static int get_pid_schepolicy(pid_t pid)
{
    int policy = sched_getscheduler(pid);

    switch(policy) {
        case SCHED_OTHER: fprintf(stderr, "SCHED_OTHER\n"); break;
        case SCHED_RR:   fprintf(stderr, "SCHED_RR\n"); break;
        case SCHED_FIFO:  fprintf(stderr, "SCHED_FIFO\n"); break;
        default:   fprintf(stderr, "Unknown...\n");
    }
	
	return policy;
}

int main(int argc, char **argv)
{
	int rc = 0;
	ssize_t msg_len = 0;
	char message[MAX_AUDIT_MESSAGE_LENGTH] = { 0 };
/*
	char *modules[3] = {
		"/usr/bin/libcamera-vid",
		"/home/jie/Documents/audit/poc-tmp/src/camera/gst_camera",
		"/home/jie/Documents/audit/poc-tmp/src/camera/python/server.py"
	};
*/	
//	int exclude_cpus[] = { 0 };
//	int exclude_cpus[] = { 0, 1 };

	mypid = getpid();
//	set_pid_priority(mypid, 0);
//	set_pid_schedpolicy(mypid, SCHED_OTHER);
	get_pid_schepolicy(mypid);

//	rc = core_bind(0, mypid);
//	rc = core_bind(-1, mypid);
//	rc = core_exclude(exclude_cpus, 1, mypid);
//	rc = core_exclude(exclude_cpus, 2, mypid);
	unsigned int cpu = 0; getcpu(&cpu, NULL);
	fprintf(stderr, "plugin@CPU %d PID: %d\n", cpu, mypid); // debug

	rc = test_init(argv[1], NULL, &mqtt_inst, &au, handle_event);
	if (rc) {
		fprintf(stderr, "Test initialization failed!\n");
		goto out;
	}

	register_handlers();
	
	/* 
		script:
			libcamera-vid --framerate=60 --inline -n -t 0 -o - |\
			gst-launch-1.0 fdsrc fd=0 ! h264parse ! rtph264pay ! udpsink host=192.168.151.36 port=5000 sync=false
	*/
//	pid_t script_pid = -1;
//	char *script_path = "/home/jie/Documents/audit/poc-tmp/src/camera/spawn_workload.sh";
	
//	script_pid = fork();
//	if (!script_pid) {

		/* execute script */
//		execl("/bin/bash", "bash", "-c", script_path, (char *) NULL);
//		perror("Failed to execute script");
//	}
//	else if (script_pid > 0) {
		/* plugin handle signal */
		/* start auparsing */
		do {
			if (auparse_feed_has_data(au)) {
				// check events for complete based on time 
				// if there's data
				auparse_feed_age_events(au);
			}
	
			while ((msg_len = read(0, message, MAX_AUDIT_MESSAGE_LENGTH)) > 0) {
				//fprintf(stderr, "msg_len = %zd\n", msg_len);
				message[msg_len] = '\0';
				if (auparse_feed(au, message, msg_len) < 0) {
					perror("auparse_feed failed: ");
				}
			}
		} while (!hup && !stop);
//		} while (!hup && !stop && !finish);
//	}
//	else {
//		fprintf(stderr, "Failed to execute %s\n", script_path);
//		goto out;
//	}

	// output event number
//	fprintf(stderr, "#event Libcamera: %ld Gstreamer: %ld Flask: %ld\n", num_event_libcam, num_event_gst, num_event_flask);

out:
//	if (script_pid > 0) poke_child(script_pid, SIGTERM);
	test_finalize(audit_fd, au, mqtt_inst);

	return 0;
}
