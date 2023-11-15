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
server_ip = '136.187.99.22'
client_ip = get_my_ip()
cur_dir = os.getcwd()

tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
tcp_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

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
time.sleep(3)

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
zf_val = { 'value': 0 }
pt_val = { 'value': 90 }
cur_stat = requests.get(info_url).json()

if cur_stat['zoom'] != 0:
	requests.put(zoom_url, data=zf_val)
if cur_stat['focus'] != 0:
	requests.put(focus_url, data=zf_val)
if cur_stat['pan'] != 90:
	requests.put(pan_url, data=pt_val)
if cur_stat['tilt'] != 90:
	requests.put(tilt_url, data=pt_val)

time.sleep(2)

def fire_request(url, val):
	requests.put(url, data=val)
#	time.sleep(1)
	time.sleep(0.025)

measure_start = time.perf_counter()

# workload 
#control_loop(pan_url, 0, 180, 1)
#control_loop(tilt_url, 0, 180, 1)
#control_loop(zoom_url, 0, 15000, 100)
#control_loop(focus_url, 0, 15000, 100)

#for i in range(75): # 1s interval
for i in range(600): # 25ms interval
	fire_request(zoom_url, zf_val)
	fire_request(focus_url, zf_val)
	fire_request(pan_url, pt_val)
	fire_request(tilt_url, pt_val)

measure_end = time.perf_counter()

filename = time.asctime().replace(' ', '-') + '_time.txt'
with open(filename, 'w') as output_file:
	output = [ f"Total # of requests: {req_cnt}", 
		f"Elapsed time: {measure_end - measure_start} sec" ]
	output_file.write('\n'.join(output))

streamer.terminate()
streamer.wait(60)
