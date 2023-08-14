#!/usr/bin/python

import socket, time
import subprocess
import requests
import os, sys

def get_my_ip():
	sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	sock.connect(('8.8.8.8', 80))
	my_ip = sock.getsockname()[0]
	sock.close()
	return my_ip

streaming_port = 5000
control_port = 5001
server_ip = '192.168.151.23'
client_ip = get_my_ip()
cur_dir = os.getcwd()

# request counter
req_cnt = 0

def start_stream():
	# on Linux
	#gst_str = f"gst-launch-1.0 udpsrc address={client_ip} port={streaming_port} caps=application/x-rtp ! queue ! rtph264depay ! h264parse ! v4l2h264dec ! fpsdisplaysink video-sink=autovideosink text-overlay=false sync=false"

	# on MacOS
	#gst_str = f"gst-launch-1.0 udpsrc address={client_ip} port={streaming_port} caps=application/x-rtp ! queue ! rtph264depay ! h264parse ! avdec_h264 ! fpsdisplaysink video-sink="glimagesink force-aspect-ratio=false" text-overlay=false sync=false
	gst_str = ["gst-launch-1.0", "udpsrc",\
		 f"address={client_ip}", f"port={streaming_port}",\
		 "caps=application/x-rtp", "!", "queue", "!", "rtph264depay", "!",\
		 "h264parse", "!", "avdec_h264", "!", "fpsdisplaysink",\
		 "video-sink=glimagesink force-aspect-ratio=false", "text-overlay=false", "sync=false", "-v"]
	return subprocess.Popen(gst_str, stdout=open(cur_dir + '/cam_log.txt', 'w'))

streamer = start_stream()

# for testing stream only
time.sleep(5)

# TODO: how to verify if gstreamer udp is successfully connected
# a.k.a streaming has started?

# start sending controlling requeset

def control_loop(url, min_v, max_v, step):
	global req_cnt
	for i in range(min_v, max_v + 1, step):
		data = { 'value': i }
		requests.put(url, data=data)
		req_cnt += 1

top_url = f"http://{server_ip}:{control_port}/"
info_url = top_url + "info"
reset_url = top_url + "reset/all"
zoom_url = top_url + "zoom"
focus_url = top_url + "focus"
pan_url = top_url + "pan"
tilt_url = top_url + "tilt"

# FIXME: ResetControl in server.py works incorrectly
# requests.put(reset_url, None)
reset_val = { 'value': 0 }
cur_stat = requests.get(info_url).json()

if cur_stat['zoom'] != 0:
	requests.put(zoom_url, data=reset_val)
if cur_stat['focus'] != 0:
	requests.put(focus_url, data=reset_val)
if cur_stat['pan'] != 0:
	requests.put(pan_url, data=reset_val)
if cur_stat['tilt'] != 0:
	requests.put(tilt_url, data=reset_val)

time.sleep(5)

measure_start = time.perf_counter()

# workload 
control_loop(pan_url, 0, 180, 1)
control_loop(tilt_url, 0, 180, 1)
control_loop(zoom_url, 0, 15000, 100)
control_loop(focus_url, 0, 15000, 100)

measure_end = time.perf_counter()

filename = time.asctime().replace(' ', '-') + '_time.txt'
with open(filename, 'w') as output_file:
	output = [ f"Total # of requests: {req_cnt}", 
		f"Elapsed time: {measure_end - measure_start} sec" ]
	output_file.write('\n'.join(output))

requests.put(zoom_url, data=reset_val)
requests.put(focus_url, data=reset_val)
requests.put(pan_url, data=reset_val)
requests.put(tilt_url, data=reset_val)

streamer.terminate()
streamer.wait()
