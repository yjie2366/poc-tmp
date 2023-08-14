#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mosquitto.h>
#include "mqtt_testlib.h"

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    printf("%s ", message->topic);
    if (message->payloadlen > 0) {
        fwrite(message->payload, 1, message->payloadlen, stdout);
        printf("\n");
    } else {
        printf("%s (null)\n", message->topic);
    }
    fflush(stdout);
}

static char *topic_sub = "/audit/filtered";

int main(int argc, char **argv)
{
    struct mosquitto *mosq = NULL;
    char	*host = "192.168.151.23";
    char	*topic = NULL;
    int	keepalive = 60;
    int	port = 1883;
    int ret = 0;

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
