#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mosquitto.h>
#include <errno.h>
#include <signal.h>
#include "mqtt_testlib.h"

char *topic_sub = "/audit/raw";
char *topic_pub = "/audit/filtered";
unsigned long num_msg = 0;

static void sigterm_handler(int sig)
{
        fprintf(stderr, "NUmber of received msg: %ld\n", num_msg);
        exit(0);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
	char *payload = message->payload;
	int msg_len = message->payloadlen;
	int qos = message->qos;
	int rc = 0;

	num_msg++;
//	struct mqtt_message_node *new_msg = mqtt_new_message(payload, msg_len, qos, topic_pub);
//	if (!new_msg) {
//		perror("Failed to create message node:");
//		mosquitto_disconnect(mosq);
//	}

//	rc = mqtt_publish(mosq, new_msg);
	rc = mqtt_publish(mosq, topic_pub, msg_len, payload);
	if (rc) {
		perror("Failed to publish received message: ");
		mosquitto_disconnect(mosq);
	}
}

int main(int argc, char **argv)
{
    struct mosquitto *mosq = NULL;
//	char *host = "192.168.151.23";
	char *host = "136.187.99.22";
    int	keepalive = 60;
    int	port = 1883;
    int ret = 0;
	struct sigaction sa;

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);
	
	mosq = mqtt_init(host, port, keepalive, NULL, on_message, NULL);
	if (!mosq) {
		fprintf(stderr, "Failed to initialize MQTT\n");
		ret = -1;
		goto out;
	}

    ret = mqtt_subscribe(mosq, NULL, topic_sub, 1);
	if (ret) {
		perror("mqtt_subscribe failed: ");
		goto out;
	}

    ret = mosquitto_loop_forever(mosq, -1, 1);
    if (ret != MOSQ_ERR_SUCCESS) {
		fprintf(stderr, "mosquitto_loo_forever error %d\n", ret);
		ret = -1;
		goto out;
    }

out:
	mqtt_finalize(mosq);

	return ret;
}
