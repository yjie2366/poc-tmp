#define _GNU_SOURCE
#define __USE_GNU
#include <sched.h>
#include <string.h>
#include <mosquitto.h>
#include <errno.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "mqtt_testlib.h"

volatile int mqtt_connected = 0;
volatile uint64_t nmessage = 0;
volatile uint64_t npuback = 0;
void	*mqtt_queue_mx = NULL;

volatile struct mqtt_message_node *head = NULL;
static struct mqtt_message_node *tail = NULL;
static uint64_t npublish = 0;

struct mqtt_message_node *mqtt_new_message(char *msg, int len, int qos, char *topic)
{
	struct mqtt_message_node *node = NULL;
	
	if (!len || !msg) {
		errno = -EINVAL;
		perror("mqtt_new_message failed:");
		return NULL;
	}

	node = malloc(sizeof(struct mqtt_message_node) + sizeof(char) * len);
	if (!node) {
		perror("failed to create new node: ");
		goto out;
	}

	//(void)topic;
	if (topic) { 
		node->topic = strdup(topic);
	}
	else {
		node->topic = strdup("/audit/raw");
		//node->topic = strdup("test");
	}
	node->mid = 0;
	node->next = NULL;

	node->len = len;
	node->qos = qos;

	memcpy(node->msg, msg, len * sizeof(char));

	mqtt_mx_lock(mqtt_queue_mx);
	if (!head) { 
		head = node;
		tail = node;
	}
	mqtt_mx_unlock(mqtt_queue_mx);

	nmessage++;
out:
	return node;
	
}

void mqtt_mx_lock(void *mutex)
{
	pthread_mutex_t *pmx = (pthread_mutex_t *)mutex;
	if (pmx) pthread_mutex_lock(pmx);
}

void mqtt_mx_unlock(void *mutex)
{
	pthread_mutex_t *pmx = (pthread_mutex_t *)mutex;
	if (pmx) pthread_mutex_unlock(pmx);
}

int mqtt_mx_init(void **pmutex)
{
	int rc = 0;
	pthread_mutex_t *mutex = malloc(sizeof(pthread_mutex_t));
	if (!mutex) {
		perror("Failed to alloc space for mutex: ");
		rc = -1;
		goto out;
	}
	
	pthread_mutex_init(mutex, NULL);
	
	if (pmutex) {
		*pmutex = mutex;
	}
	else {
		mqtt_queue_mx = mutex;
	}

out:
	return rc;
}

void mqtt_mx_destroy(void *mutex)
{
	pthread_mutex_t *_mutex = (pthread_mutex_t *)mutex;
	
	if (_mutex) {
		pthread_mutex_destroy(_mutex);
		free(_mutex);
	}
}

void mqtt_msg_enq(struct mqtt_message_node *node)
{
	tail->next = node;
	tail = node;	
}

void mqtt_msg_deq(void)
{
	mqtt_mx_lock(mqtt_queue_mx);
	struct mqtt_message_node *tmp = (struct mqtt_message_node *)head;
	
	head = head->next;
	if (tmp) {
		if (tmp->topic) free(tmp->topic);
		free(tmp);
	}
	mqtt_mx_unlock(mqtt_queue_mx);
}

static void mqtt_on_publish(struct mosquitto *mosq, void *obj, int mid)
{
	(void)obj;

	npuback++;
	mqtt_msg_deq();

	mqtt_mx_lock(mqtt_queue_mx);
	if (head) {
		struct mqtt_message_node *next = (struct mqtt_message_node *)head;	
		mqtt_publish(mosq, next);
	}
	mqtt_mx_unlock(mqtt_queue_mx);
}

static void mqtt_on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    fprintf(stderr, "%s: CONNECTED rc=%d\n", __func__, rc);
    mqtt_connected = 1;
}

static void mqtt_on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
#if 0
    --mqtt_iter;
    /**/
    strncpy(mqtt_lastmsg, message->payload, 128);
    DEBUG {
	char	buf[128];
	strncpy(buf, message->payload, 10);
	buf[10] = 0;
	printf("%s:<%d> topc=\"%s\" msg(%s)\n", __func__, mqtt_iter, message->topic, buf);
	fflush(stdout);
    }
    if (mqtt_iter == 0) {
	DEBUG {
	    printf("%s: subscriber_bfd= %d\n", __func__, subscriber_bfd); fflush(stdout);
	}
	if (subscriber_bfd > 0) {
	    lockf(subscriber_bfd, F_ULOCK, 0);
	    lockf(subscriber_efd, F_ULOCK, 0);
	    mqtt_lockclose(subscriber_bfd);
	    mqtt_lockclose(subscriber_efd);
	}
	mqtt_fin(mosq);
	tm_et = tick_time();
	if (verbose) {
	    uint64_t	clk = tm_et - tm_st;
	    double	tm = (double)(clk)/((double)tm_hz/(double)SCALE);
	    printf("%s: time: %f (usec) (%ld), start: %ld, end: %ld, \n",
		   __func__, tm, clk, tm_st, tm_et); fflush(stdout);
	}
	exit(0);
    }

#endif
}

void mqtt_connect_callback_set(void *mqtt_inst, void *on_connect_func)
{
	void (*on_connect)(struct mosquitto*, void*, int) = NULL;
	struct mosquitto *mosq = (struct mosquitto *)mqtt_inst;

	on_connect = (void(*)(struct mosquitto*, void*, int))on_connect_func;
	if (on_connect) {
		mosquitto_connect_callback_set(mosq, on_connect);
	}
	else {
		mosquitto_connect_callback_set(mosq, mqtt_on_connect);
	}
}

void mqtt_message_callback_set(void *mqtt_inst, void *on_message_func)
{
	void (*on_message)(struct mosquitto*, void*, const struct mosquitto_message *) = NULL;
	struct mosquitto *mosq = (struct mosquitto *)mqtt_inst;

	on_message = (void(*)(struct mosquitto*, void*, const struct mosquitto_message *))on_message_func;
	if (on_message) {
		mosquitto_message_callback_set(mosq, on_message);
	}
	else {
		mosquitto_message_callback_set(mosq, mqtt_on_message);
	}
}

void mqtt_publish_callback_set(void *mqtt_inst, void *on_publish_func)
{
	void (*on_publish)(struct mosquitto*, void*, int) = NULL;
	struct mosquitto *mosq = (struct mosquitto *)mqtt_inst;

	on_publish = (void(*)(struct mosquitto*, void*, int))on_publish_func;
	if (on_publish) {
		mosquitto_publish_callback_set(mosq, on_publish);
	}
	else {
		mosquitto_publish_callback_set(mosq, mqtt_on_publish);
	}
}

void *mqtt_init(char *host, int port, int keepalive, void *on_connect, void *on_message, void *on_publish)
{
	int rc = 0;
	struct mosquitto *mosq = NULL;

	rc = mosquitto_lib_init();
	if (rc != MOSQ_ERR_SUCCESS) {
		perror("mosquitto lib init failed: ");
		goto error;
	}
	
	mosq = mosquitto_new(NULL, true, NULL);
	if (!mosq) {
		perror("Failed to create a new client instance: ");
		goto error;
	}

	mqtt_connect_callback_set(mosq, on_connect);
	mqtt_message_callback_set(mosq, on_message);
	mqtt_publish_callback_set(mosq, on_publish);

	mqtt_mx_init(NULL);

	if (mosquitto_connect(mosq, host, port, keepalive)) {
		perror("failed to connect to a MQTT broker: ");
		goto error;
	}

	return (void*) mosq;

error:
	mosquitto_lib_cleanup();
	return NULL;
}

void mqtt_finalize(void *mqtt_inst)
{
	struct mosquitto *mosq = (struct mosquitto *)mqtt_inst;

	mosquitto_disconnect(mosq);
	mosquitto_destroy(mosq);
	mosquitto_lib_cleanup();
	mqtt_mx_destroy(mqtt_queue_mx);

	// DEBUG
	// fprintf(stderr, "Number of published messages: %ld\n", npublish);
	// fprintf(stderr, "Number of pubacked messages: %ld\n", npuback);
}

int mqtt_publish(void *mqtt_inst, struct mqtt_message_node *node)
{
	int	rc = 0;
	struct mosquitto *mosq = (struct mosquitto *) mqtt_inst;
	char *topic = node->topic;
	int len = node->len;
	char *msg = node->msg;
	int qos = node->qos;

	if (node == head) {
		rc = mosquitto_publish(mosq, &node->mid, topic, len, msg, qos, 0);
		if (rc != MOSQ_ERR_SUCCESS) {
			fprintf(stderr, "Publish msg ID %d failed ERROR %d\n", node->mid, rc);
			return -1;
		}
	} else {
		mqtt_msg_enq(node);
	}

	npublish++;

	return 0;
}

int mqtt_subscribe(void *mqtt_inst, int *mid, char *topic, int qos)
{
	int rc = 0;
	struct mosquitto *mosq = (struct mosquitto *)mqtt_inst;

	rc = mosquitto_subscribe(mosq, mid, topic, qos);
	if (rc != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "Failed to subscribe topic %s ERROR %d\n", topic, rc);
		return -1;
	}
	
	return 0;
}

int mqtt_loop_start(void *mqtt_inst)
{
	struct mosquitto *mosq = mqtt_inst;
	mosquitto_loop_start(mosq);
	
	// wait until on_connect callback is called
	// when broker sends a CONNACK message in response to a connection
	while (!mqtt_connected) usleep(10);

	return mqtt_connected;
}

int mqtt_loop_forever(void *mqtt_inst, int timeout, int max_packets)
{
	struct mosquitto *mosq = mqtt_inst;
	int ret = 0;
	
	(void)max_packets;
	
	// max_packets is curretly unused and should be set to 1 for compatibility
	// based on https://mosquitto.org/api/files/mosquitto-h.html#mosquitto_loop_forever
	ret = mosquitto_loop_forever(mosq, timeout, 1);
	if (ret != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "mosquitto_loop_forever failed\n");
		return -1;
	}
	
	return 0;
}

