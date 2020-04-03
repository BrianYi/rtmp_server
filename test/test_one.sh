#!/bin/sh

puller_num=10
puller_app="tnhaoxc.flv"
dst_dir="D:\VS Documents\source\repos\hevc_server\test"
pusher_num=1
pusher_app="tnhaoxc.flv"
pusher_video_path="E:\Movie\test video\tnhaoxc.flv"
sh start.sh $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"