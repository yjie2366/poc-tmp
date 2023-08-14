#include <gst/gst.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/time.h>

static volatile gboolean finish = FALSE;
//static struct timeval start, end;

/*
static float time_elapsed(struct timeval _start, struct timeval _end)
{
	return (float)(_end.tv_sec - _start.tv_sec) + (float)(_end.tv_usec - _start.tv_usec)/1000000.0f;
}

static void fps_measurements_callback(GstElement sink, gdouble fps, gdouble droprate, gdouble avgfps, gpointer udata)
{
	g_printerr("fps=%.5f droprate=%.5f avgfps=%.5f\n", fps, droprate, avgfps);
}
*/
static void finish_stream(int sig)
{
	printf("Streaming ended\n"); fflush(stdout);
	finish = TRUE;
}

static void register_handlers(void)
{
	struct sigaction sa1;
	
	sa1.sa_flags = 0;
	sigemptyset(&sa1.sa_mask);

	sa1.sa_handler = finish_stream;
	sigaction(SIGTERM, &sa1, NULL);
}


int main(int argc, char **argv)
{
	/*
		gst-launch-1.0 fdsrc fd=0 !
					h264parse !
					rtph264pay !
					udpsink host=192.168.151.36 port=5000 sync=false
	*/
	int rc = 0;
	GError *error = NULL;
	GstElement  *pipeline = NULL,
				*source = NULL,
				*h264parse = NULL,
				*rtph264pay = NULL,
				*udpsink = NULL,
				*src_queue = NULL;

#ifdef FPS_SINK
	GstElement	*fpsdisplaysink = NULL,
				*tee = NULL,
				*fps_queue = NULL,
				*udp_queue = NULL;
	
	GstPad *tee_fps_pad = NULL, *tee_udp_pad = NULL;
	GstPad *queue_fps_pad = NULL, *queue_udp_pad = NULL;
#endif

	gchar *rtp_host = "192.168.151.36";
	guint rtp_port = 5000;
	GstStateChangeReturn ret;
	GstBus *bus = NULL;
	GstMessage *msg = NULL;
	gboolean stream_end = FALSE;

	gst_init(&argc, &argv);

	register_handlers();
	
	pipeline = gst_pipeline_new("stream");

	source = gst_element_factory_make("fdsrc", "source");
	g_object_set(source, "fd", 0, NULL);

	src_queue = gst_element_factory_make("queue", "src_queue");
	h264parse = gst_element_factory_make("h264parse", "parser");
	
	// set rtph264 payload
	rtph264pay = gst_element_factory_make("rtph264pay", "rtph264");
	// set udpsink
	udpsink = gst_element_factory_make("udpsink", "udpsink");
	
	g_object_set(G_OBJECT(udpsink), "host", rtp_host, NULL);
	g_object_set(G_OBJECT(udpsink), "port", rtp_port, NULL);
	g_object_set(G_OBJECT(udpsink), "sync", FALSE, NULL);


	if ( !pipeline || !source || !src_queue || !h264parse || !rtph264pay || !udpsink ) {
		g_printerr("Failed to create elements\n");
		rc = -1;
		goto out;
	}

	gst_bin_add_many(GST_BIN(pipeline), source, src_queue, h264parse, rtph264pay, udpsink, NULL);

	if (gst_element_link_many(source, src_queue, h264parse, rtph264pay, udpsink, NULL) != TRUE) {
		g_printerr("Elements could not be linked\n");
		rc = -1;
		goto out;
	}

	/* start playing */
	ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
	if (ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr("Failed to set into PLAYING state\n");
		return -1;
	}

	bus = gst_element_get_bus(pipeline);
	do {
		gchar *debug_info = NULL;

		/* time-out in 2 gst seconds */
		msg = gst_bus_timed_pop_filtered(bus, GST_SECOND * 2,
			GST_MESSAGE_STATE_CHANGED | 
			GST_MESSAGE_ERROR | 
			GST_MESSAGE_EOS);
		if (msg != NULL) {
			switch(GST_MESSAGE_TYPE(msg)) {
				case GST_MESSAGE_ERROR:
					gst_message_parse_error(msg, &error, &debug_info);
					g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), error->message);
					g_printerr("Debug info: %s\n", (debug_info)? debug_info : "None");
					g_clear_error(&error);
					stream_end = TRUE;
					break;
				case GST_MESSAGE_EOS:
					g_print("Stream ended.\n");
					stream_end = TRUE;
					break;
				case GST_MESSAGE_STATE_CHANGED:
					if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
						GstState old, new, pending;
						gst_message_parse_state_changed(msg, &old, &new, &pending);
						g_print("Pipeline state changed from %s to %s\n",
							gst_element_state_get_name(old),
							gst_element_state_get_name(new));
					}
					break;
				default:
					g_printerr("Unable to parse message\n");
					break;
			}	
			gst_message_unref(msg);
		}

	} while ((finish == FALSE) && (stream_end == FALSE));

	gst_element_set_state(pipeline, GST_STATE_NULL);

out:
	gst_object_unref(pipeline);
	
	return rc;
}
