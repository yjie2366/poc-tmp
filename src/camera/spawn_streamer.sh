#!/bin/bash

# /usr/lib/systemd/system/camera_streaming.service

cur_dir=$(cd $(dirname ${BASH_SOURCE:-$0}) && pwd)
slice=application

# start streaming
width=640
height=480
framerate=60

libcamera_bin="/usr/bin/libcamera-vid"
gstreamer_bin="${cur_dir}/gst_camera"

${libcamera_bin} --width ${width} --height ${height} --framerate=${framerate} --inline -n -t 0 -o - | ${gstreamer_bin}

