#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <mosquitto.h>
#include "mqtt_testlib.h"

unsigned long num = 0;

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
	num++;

	printf("%s ", message->topic);
	if (message->payloadlen > 0) {
		fwrite(message->payload, 1, message->payloadlen, stdout);
//		printf("ID %d received\n", message->mid);
		printf("\n");
	} else {
		printf("%s (null)\n", message->topic);
	}

	fflush(stdout);
}

static char *topic_sub = "/audit/filtered";

static void sigterm_handler(int sig)
{
	fprintf(stderr, "NUmber of received msg: %ld\n", num);
	exit(0);
}

int main(int argc, char **argv)
{
	struct mosquitto *mosq = NULL;
	//    char	*host = "192.168.151.23";
	char	*host = "136.187.99.22";
	char	*topic = NULL;
	int	keepalive = 60;
	int	port = 1883;
	int ret = 0;
	struct sigaction sa;

	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = sigterm_handler;
	sigaction(SIGINT, &sa, NULL);

	if (argc == 3) {
		host = argv[1]; 
		topic = argv[2];
	} else if (argc == 2) {
		topic = argv[1];
	}

	if (!topic) topic = topic_sub;


	mosq = mqtt_init(host, port, keepalive, NULL, on_message, NULL);
	if (!mosq) {
		fprintf(stderr, "Failed to initialize MQTT\n");
		ret = -1;
		goto out;
	}

	ret = mqtt_subscribe(mosq, NULL, topic, 1);
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
