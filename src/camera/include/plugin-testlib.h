#ifndef __PLUGIN_TEST_H_
#define __PLUGIN_TEST_H_

#include <auparse.h>

extern volatile int stop;
extern volatile int hup;
extern volatile int finish;
extern int mrkr;
extern int iter;
extern int num_sysc;
extern int clone_cpu;
extern int clone_pid;
extern struct audit_syscall *sysc_list;

int generate_worker_thread(void *workload, void *child_arg);
unsigned int core_bind(int _cpu, pid_t pid);
void start_work(void);
void wait_for_release(void);
void wait_for_exit(void);
void finish_work(void);
void finalize_worker_thread(void);
int invoke_marker(void);
int test_init(char *argv1, char *marker, void **mqtt_s, auparse_state_t **p_au, auparse_callback_ptr auparse_callback);
void test_finalize(int audit_fd, auparse_state_t *au, void *mqtt_inst);
void print_results(void);
int search_syscall(char *snam);
int init_audit_rules(int *audit_fd, pid_t *target_pids, int num_pids);
//int init_audit_rules_exe(int *audit_fd, char **target_exes, int num_exes);
int init_audit_rules_exe(int *audit_fd, char **target_exes, int num_exes, pid_t *target_pids, int num_pids);

#endif
