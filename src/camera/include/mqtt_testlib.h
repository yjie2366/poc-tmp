#ifndef __MQTT_TEST_H
#define __MQTT_TEST_H

struct mqtt_message_node {
	struct mqtt_message_node *next;
	char *topic;
	int mid;
	int len;
	int qos;
	char msg[];
};

extern void *mqtt_queue_mx;
extern volatile struct mqtt_message_node *head;
//extern volatile uint64_t npuback, nmessage;

struct mqtt_message_node *mqtt_new_message(char *msg, int len, int qos, char *topic);
void mqtt_mx_lock(void *mutex);
void mqtt_mx_unlock(void *mutex);
int mqtt_mx_init(void **pmutex);
void mqtt_mx_destroy(void *mutex);
void mqtt_msg_enq(struct mqtt_message_node *node);
void mqtt_msg_deq(void);
void mqtt_publish_callback_set(void *mqtt_inst, void *on_publish_func);
void *mqtt_init(char *host, int port, int keepalive, void *on_connect, void *on_message, void *on_publish);
void mqtt_finalize(void *mqtt_inst);
//int mqtt_publish(void *mqtt_inst, struct mqtt_message_node *node);
int mqtt_publish(void *mqtt_inst, char *topic, int len, char *msg);
int mqtt_subscribe(void *mqtt_inst, int *mid, char *topic, int qos);
int mqtt_loop_start(void *mqtt_inst);
int mqtt_loop_forever(void *mqtt_inst, int timeout, int max_packets);

#endif
