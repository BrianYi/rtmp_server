#!/bin/sh
# 3 pushers
#	pusher 1: "Av41342335P2.mp4"						"E:\Movie\test video\Av41342335P2.mp4"
#	pusher 2: "bbb_sunflower_1080p_60fps_normal.mp4"	"E:\Movie\test video\bbb_sunflower_1080p_60fps_normal.mp4"
#	pusher 3: "Av41342335P3.mp4"						"E:\Movie\test video\Av41342335P3.mp4"
# 10 pullers
#	puller 1-3: "Av41342335P2.mp4"						"D:\VS Documents\source\repos\hevc_server\test"
#	puller 4-7: "bbb_sunflower_1080p_60fps_normal.mp4"	"D:\VS Documents\source\repos\hevc_server\test"
#	puller 8-10:"Av41342335P3.mp4"						"D:\VS Documents\source\repos\hevc_server\test"

puller_num=3
puller_app="Av41342335P2.mp4"
dst_dir="D:\VS Documents\source\repos\hevc_server\test"
pusher_num=1
pusher_app="Av41342335P2.mp4"
pusher_video_path="E:\Movie\test video\Av41342335P2.mp4"
sh start.sh $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"

puller_num=3
puller_app="Av41342335P3.mp4"
dst_dir="D:\VS Documents\source\repos\hevc_server\test"
pusher_num=1
pusher_app="Av41342335P3.mp4"
pusher_video_path="E:\Movie\test video\Av41342335P3.mp4"
sh start.sh $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"

puller_num=4
puller_app="bbb_sunflower_1080p_60fps_normal.mp4"
dst_dir="D:\VS Documents\source\repos\hevc_server\test"
pusher_num=1
pusher_app="bbb_sunflower_1080p_60fps_normal.mp4"
pusher_video_path="E:\Movie\test video\bbb_sunflower_1080p_60fps_normal.mp4"
sh start.sh $puller_num "$puller_app" "$dst_dir" $pusher_num "$pusher_app" "$pusher_video_path"