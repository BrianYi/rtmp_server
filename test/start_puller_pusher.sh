#!/bin/sh
# usage: start.sh debug/release puller-num appname dst_dir pusher-num appname video_path
# 		 you need to start server first, compile server code and runing it, then start this bash script


if [ "$#" -lt 7 ]; then
	echo "<usage>: start.sh debug/release puller-num app /puller/dst/dir pusher-num app /pusher/video/path"
	echo "<example>"
	echo " if you want to setup a release env with 1 pusher pushing /video/bbb_sunflower.mp4 to bbb_sunflower.mp4, 3 puller pulling from bbb_sunflower.mp4 and save into /Download/bbb_sunflower.mp4"
	echo "then do the follow:"
	echo " start.sh release 3 bbb_sunflower.mp4 /Download/bbb_sunflower.mp4 1 bbb_sunflower.mp4 /video/bbb_sunflower.mp4"
	exit 1
fi

debug_or_release="${1:-Debug}"
pusher_release_path="../../rtmp_pusher/Release"
puller_release_path="../../rtmp_puller/Release"
pusher_debug_path="../../rtmp_pusher/Debug"
puller_debug_path="../../rtmp_puller/Debug"
if [ "$debug_or_release" = "release" ]; then
	pusher_exe="$pusher_release_path/rtmp_pusher.exe"
	puller_exe="$puller_release_path/rtmp_puller.exe"
else
	pusher_exe="$pusher_debug_path/rtmp_pusher.exe"
	puller_exe="$puller_debug_path/rtmp_puller.exe"
fi

default_puller_num=3
default_pusher_num=1
default_app="live"
default_dst_dir="../../rtmp_server/test"
default_video_path="/E/Movie/test video/bbb_sunflower_1080p_60fps_normal.mp4"


puller_num="${2:-$default_puller_num}"
puller_app="${3:-$default_app}"
dst_dir="${4:-$default_dst_dir}/$(date +%s%N)"
pusher_num="${5:-$default_pusher_num}"
pusher_app="${6:-$default_app}"
pusher_video_path="${7:-$default_video_path}"

dir_file="dir.txt"
# save dest files
[ ! -d "$dst_dir" ] && mkdir -p "$dst_dir" && echo "create dir $dst_dir successful."
[ ! -f "$dir_file" ] && touch "$dir_file" && echo "create dir file $dir_file successful."
echo "$dst_dir" >> "$dir_file"

# start puller
puller_pid_file="$dst_dir/pullers.pid"
puller_pid=()
[ ! -f "$puller_pid_file" ] && touch "$puller_pid_file" && echo "create $puller_pid_file successful."
> "$puller_pid_file"
for (( i = 0; i< puller_num; ++i ))
do
	puller_video_name="puller-video$i.$puller_app"
	puller_dump_name="puller-dump$i.txt"
	puller_save_path="$dst_dir/$puller_video_name"
	puller_dump_path="$dst_dir/$puller_dump_name"
	$puller_exe "$puller_app" "$puller_save_path" "$puller_dump_path" >/dev/null &
	puller_pid[i]=$!
	echo $! >> "$puller_pid_file"
	echo "start puller$i(${puller_pid[i]})..."
done


# sleep 1 second to wait all puller is ready
sleep 1

# start pusher
pusher_pid_file="$dst_dir/pushers.pid"
pusher_pid=()
[ ! -f "$pusher_pid_file" ] && touch "$pusher_pid_file" && echo "create $pusher_pid_file successful."
> "$pusher_pid_file"
for (( i = 0; i< pusher_num; ++i ))
do
	pusher_dump_name="pusher-dump$i.txt"
	pusher_dump_path="$dst_dir/$pusher_dump_name"
	$pusher_exe "$pusher_app" "$pusher_video_path" "$pusher_dump_path" >/dev/null &
	pusher_pid[i]=$!
	echo $! >> "$pusher_pid_file"
	echo "start pusher$i(${pusher_pid[i]})..."
done

