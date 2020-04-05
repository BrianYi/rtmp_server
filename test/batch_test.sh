#!/bin/sh
# 3 pushers
#	pusher 1: "test1.mp4"	"E:\Movie\test video\test.mp4"
#	pusher 2: "test2.mp4"	"E:\Movie\test video\test.mp4"
#	pusher 3: "test3.mp4"	"E:\Movie\test video\test.mp4"
# 30 pullers
#	puller 1-10: "test1.mp4"	"D:\VS Documents\source\repos\rtmp_server\test"
#	puller 11-20: "test2.mp4"	"D:\VS Documents\source\repos\rtmp_server\test"
#	puller 21-30:"test3.mp4"	"D:\VS Documents\source\repos\rtmp_server\test"
debug_or_release="release" # debug/release switch
start_script=start_puller_pusher.sh

num=10
for(( i=0; i<num; ++i ))
do
	puller_num=1
	puller_app="test$i.mp4"
	dst_dir="D:\VS Documents\source\repos\rtmp_server\test"
	pusher_num=1
	pusher_app="test$i.mp4"
	pusher_video_path="E:\Movie\test video\test.mp4"
	sh $start_script "$debug_or_release" $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"
done

#puller_num=5
#puller_app="test1.mp4"
#dst_dir="D:\VS Documents\source\repos\rtmp_server\test"
#pusher_num=1
#pusher_app="test1.mp4"
#pusher_video_path="E:\Movie\test video\test.mp4"
#sh $start_script "$debug_or_release" $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"
#
#puller_num=5
#puller_app="test2.mp4"
#dst_dir="D:\VS Documents\source\repos\rtmp_server\test"
#pusher_num=1
#pusher_app="test2.mp4"
#pusher_video_path="E:\Movie\test video\test.mp4"
#sh $start_script "$debug_or_release" $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"
#
#puller_num=5
#puller_app="test3.mp4"
#dst_dir="D:\VS Documents\source\repos\rtmp_server\test"
#pusher_num=1
#pusher_app="test3.mp4"
#pusher_video_path="E:\Movie\test video\test.mp4"
#sh $start_script "$debug_or_release" $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"
#
#puller_num=5
#puller_app="bbb_sunflower1.mp4"
#dst_dir="D:\VS Documents\source\repos\rtmp_server\test"
#pusher_num=1
#pusher_app="bbb_sunflower1.mp4"
#pusher_video_path="E:\Movie\test video\bbb_sunflower.mp4"
#sh $start_script "$debug_or_release" $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"
#
#puller_num=5
#puller_app="bbb_sunflower2.mp4"
#dst_dir="D:\VS Documents\source\repos\rtmp_server\test"
#pusher_num=1
#pusher_app="bbb_sunflower2.mp4"
#pusher_video_path="E:\Movie\test video\bbb_sunflower.mp4"
#sh $start_script "$debug_or_release" $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"
#
#puller_num=5
#puller_app="test4.mp4"
#dst_dir="D:\VS Documents\source\repos\rtmp_server\test"
#pusher_num=1
#pusher_app="test4.mp4"
#pusher_video_path="E:\Movie\test video\test.mp4"
#sh $start_script "$debug_or_release" $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"


