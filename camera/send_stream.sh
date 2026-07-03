#!/bin/bash

echo "=========================================="
echo "  STA端 - 摄像头视频推流"
echo "=========================================="
echo "摄像头设备 : /dev/video41"
echo "分辨率     : 1280x720"
echo "帧率       : 30 FPS"
echo "编码方式   : H.264 (MPP)"
echo "目标IP     : 192.168.100.1"
echo "目标端口   : 5000"
echo "按 Ctrl+C 停止"
echo "=========================================="

gst-launch-1.0 -v \
  v4l2src device=/dev/video41 \
  ! image/jpeg,width=1280,height=720,framerate=30/1 \
  ! jpegdec \
  ! videoconvert \
  ! mpph264enc \
  ! h264parse \
  ! rtph264pay config-interval=-1 \
  ! udpsink host=192.168.100.1 port=5000 sync=false