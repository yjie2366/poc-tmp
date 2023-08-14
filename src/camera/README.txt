# https://flask-restful.readthedocs.io/en/latest/installation.html#installation

pip install flask-restful

Info
----

1. plugin-libcamera-vid.c: The audit plugin for camera streaming and remote control testing. This implementation use libcamera-vid, which supports streaming using different framerate.
2. camera-fdsrc.c: A camera streaming module written using gstreamer. It reads h.264 decoded camera video from stdin by using fdsrc, and sends the stream
	to udpsink.
	
3. A flask-RESTful based web server for remote camera control is also included.
 
plugin-libcamera-vid.c generates three child processes. 
	process #1: libcamera-vid sends h.264 decoded video stream to stdout
	process #2: gstreamer process reads the video stream from stdin and streams the video via udpsink
	process #3: a flask-RESTful based web server, which consists of multiple RESTful interfaces for camera remote controlling.
* The supported controlling interfaces are Zoom, Focus, Pan, Tilt, Reset and IRCut, which can be accessed by HTTP requests in the commands listed in Section: How to Use.

               	          +-----------------+                 |
+--------------+==fork==> | Camera Streamer | --- RTP/UDP --- | --> ...
|              |	  +-----------------+                 |
| Audit Plugin |
|              |          +-----------------+
+--------------+==fork==> |   Flask Server  | <--- HTTP ---   | --> ...
                          +-----------------+

The default port number for camera streaming is 5000, and 5001 for remote camera control.

How to Use
----------
1. Start Linux Audit plugin plugin-camera-test
	sudo taskset -c 3 auditd -f -n -s enable -c /usr/local/etc/audit > /dev/null

2. Start stream receiver on another platform in the same network (p.s. check next section)

3. Send request to the Flask server via the methods listed below:

* Zoom
	# get: get current zoom status of the camera
	`curl http://192.168.151.xxx:5001/zoom`

	# put: zoom the camera into a certain value
	`curl http://192.168.151.xxx:5001/zoom -X put -d "value=10000"`

	** Requests for Focus, Pan, Tilt and IRCut are same as above.

* Info
	# Only GET method is supported
	# Get current status of the camera
	`curl http://192.168.151.xxx:5001/info`

* Reset
	# Only PUT method is supported
	# BUG: sometimes the PAN value is not reset. Need further investigation.
	# Reset all the controlling interfaces
	`curl http://192.168.151.xxx:5001/reset -X put`
	# or
	`curl http://192.168.151.xxx:5001/reset/all -X put`

	# Reset individual interface (e.g. Zoom)
	`curl http://192.168.151.xxx:5001/reset/zoom -X put`

(Optional) 2. Use client.py for performance measuring in web_server/ folder.

About Gstreamer
--------------

1. Change framerate:

Streamer implementation:
libcamera-vid --framerate=60 --inline -n -t 0 -o - | gst-launch-1.0 fdsrc fd=0 ! h264parse ! rtph264pay ! udpsink host=192.168.151.36 port=5000 sync=false

Receiver command (MacOS): # on MacOS avdec_h264 decoder is used
gst-launch-1.0 udpsrc address=192.168.151.36 port=5000 caps=application/x-rtp ! queue ! rtph264depay ! h264parse ! avdec_h264 ! fpsdisplaysink video-sink="glimagesink force-aspect-ratio=false" text-overlay=false

2. How to end plugin-camera-test?
kill -TERM <plugin_pid>

# it will send SIGTERM to all the child process and do the cleanups.

How to perform plugin-camera-test ($TOP_DIR/plugin-camera)?
--------------------------------------
0. Set up config file in /usr/local/etc/audit/plugins.d/ (e.g. plugin-camera-test.conf)

1. Open Mosquitto broker, filter on the server and set affinity by taskset

taskset -c 0 /usr/local/sbin/mosquitto -c mosquitto/mosquitto.conf
taskset -c 0 ./filter

2. Open subscriber on another device

./subscriber

3. Run the audit plugin
NOTE: remember to redirect stdout and stderr to /dev/null before starting evaluation

sudo taskset -c 0 auditd -f -n -s enable -c /usr/local/etc/audit &> /dev/null

4. with no audit case:

# For streamer & Flask on server side, remember to set affinity for all the process.
# libcamera-vid@CPU_1
# gstreamde@CPU_2
# flask@CPU_3

taskset -c 1 libcamera-vid --framerate=60 --inline -n -t 0 -o - | taskset -c 2 gst-launch-1.0 fdsrc fd=0 ! h264parse ! rtph264pay ! udpsink host=192.168.151.36 port=5000 sync=false

taskset -c 3 ./server.py &> /dev/null

On the client side, just run python script: client.py
