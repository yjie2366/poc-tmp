#!/bin/bash

# start stream systemd service and flask systemd service
# service unit:
# /etc/systemd/system/camera_{control,streaming}.service

syscall_list="all"
cur_dir=$(cd $(dirname ${BASH_SOURCE:-$0}) && pwd)
slice=application

libcamera_bin="/usr/bin/libcamera-vid"
gstreamer_bin="${cur_dir}/gst_camera"

stream_service=camera_streaming.service
flask_service=camera_control.service


# setup rules and run the server
sudo auditctl -D # clean all the previous rules
sudo auditctl -a always,exit -S ${syscall_list} -F exe=${libcamera_bin}
sudo auditctl -a always,exit -S ${syscall_list} -F exe=${gstreamer_bin}

# start flask service
sudo systemctl restart ${flask_service}
flask_pid=$(sudo systemctl show --property MainPID --value ${flask_service})
sudo auditctl -a always,exit -S ${syscall_list} -F pid=${flask_pid}

# start streaming service
sudo systemctl restart ${stream_service}

sleep infinity &
sleep_pid=$!

cleanup()
{
#	sudo kill -KILL ${stream_pid}
	sudo systemctl stop ${slice}.slice
	sudo auditctl -D
	kill ${sleep_pid}
	exit
}

trap cleanup SIGTERM SIGINT

wait ${sleep_pid}
