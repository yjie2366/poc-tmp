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

//extern char **environ;
static pid_t camera_pid = -1;
static pid_t gst_pid = -1;
static pid_t control_pid = -1;
static volatile int start_workload = 0;

pid_t mypid = -1;
int audit_fd = -1;
auparse_state_t *au = NULL;
void *mqtt_inst = NULL;

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
			struct mqtt_message_node *node = NULL;

			node = mqtt_new_message(msg_buf, tmp_len, 1, "/audit/raw");
			if (node) mqtt_publish(mqtt_inst, node);
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

int main(int argc, char **argv)
{
	int rc = 0;
	ssize_t msg_len = 0;
	char message[MAX_AUDIT_MESSAGE_LENGTH] = { 0 };
	int vid_pipe[2] = { 0 };

	mypid = getpid();

	rc = core_bind(0, mypid);
	fprintf(stderr, "plugin@CPU %d PID: %d\n", rc, mypid); // debug

	rc = test_init(argv[1], NULL, &mqtt_inst, &au, handle_event);
	if (rc) {
		fprintf(stderr, "Test initialization failed!\n");
		goto out;
	}

	register_handlers();
	
	if (pipe(vid_pipe) == -1) {
		perror("Failed to create pipe:");
		goto out;
	}

	/* 
		implementation of:
			libcamera-vid --framerate=60 --inline -n -t 0 -o - |\
			gst-launch-1.0 fdsrc fd=0 ! h264parse ! rtph264pay ! udpsink host=192.168.151.36 port=5000 sync=false
	*/
	camera_pid = fork();
	if (!camera_pid) {
		char *exe_name = "libcamera-vid";
		char *cmd[] = {
			exe_name, "--width", "640", "--height", "480", "--framerate=60", "--inline",
			"-n", "-t", "0", "-o", "-", NULL
		};
		pid_t _camera_pid = getpid();

		rc = dup2(vid_pipe[1], 1);
		if (rc < 0) {
			perror("Failed to dup stdout:");
			return -1;
		}

		close(vid_pipe[0]);
		close(vid_pipe[1]);

		rc = core_bind(1, _camera_pid);
		fprintf(stderr, "libcamera-vid@CPU %d PID %d\n", rc, _camera_pid); // debug

		/* wait until audit rule is registered */
		while (!start_workload) usleep(50);

		execvp(exe_name, cmd);
		perror("Failed to execute libcamera: ");
	}
	else if (camera_pid > 0) {
		gst_pid = fork();
		// gstreamer
		if (!gst_pid) {
#define GST_CAMERA "/home/jie/Documents/audit/audit-overhead/my_samples/audit/test/plugin-camera/gst_camera"
			char *cmd[] = { GST_CAMERA, NULL };
			pid_t _gst_pid = getpid();

			rc = dup2(vid_pipe[0], 0);
			close(vid_pipe[0]);
			close(vid_pipe[1]);
			
			rc = core_bind(2, _gst_pid);
			fprintf(stderr, "gstreamer@CPU %d PID %d\n", rc, _gst_pid); // debug

			while (!start_workload) usleep(50);

			execvp(GST_CAMERA, cmd);
			perror("Failed to execute gstreamer:");	
		}
		else if (gst_pid > 0) {		
			// camera control process
			control_pid = fork();
			if (!control_pid) {
				pid_t _control_pid = getpid();				

				close(vid_pipe[0]);
				close(vid_pipe[1]);

				rc = core_bind(3, _control_pid);
				fprintf(stderr, "flask@CPU %d PID %d\n", rc, _control_pid); // debug
				
				while (!start_workload) usleep(50);

#define CONTROL_SERVER_PATH "/home/jie/Documents/audit/audit-overhead/my_samples/audit/test/plugin-camera/server.py"
				char *args[] = { CONTROL_SERVER_PATH, NULL };

				execvp(args[0], args);
				perror("Failed to execute Flask server:");
			}
			// Plugin
			else if (control_pid > 0) {
				pid_t target_pids[3] = { 0 };

				target_pids[0] = camera_pid;
				target_pids[1] = gst_pid;
				target_pids[2] = control_pid;			

				close(vid_pipe[0]);
				close(vid_pipe[1]);
				
				rc = init_audit_rules(&audit_fd, target_pids, 3);
				if (rc < 0) {
					fprintf(stderr, "Failed to initialize rules\n");
					goto out;
				}

				poke_child(camera_pid, SIGUSR2);
				poke_child(gst_pid, SIGUSR2);
				poke_child(control_pid, SIGUSR2);

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
				} while (!hup && !stop && !finish);
			}
			else {
				perror("Failed to fork control process");
				goto out;
			}
		}
		// else if (gst_pid > 0)
		else {
			perror("Failed to fork Gstreamer process:");
			goto out;
		}
	}
	else {
		perror("Failed to fork camera process");
		goto out;
	}

	kill(camera_pid, SIGTERM);
	kill(gst_pid, SIGTERM);
	kill(control_pid, SIGTERM);

	finalize_worker_thread();

out:
	test_finalize(audit_fd, au, mqtt_inst);

	return 0;
}
