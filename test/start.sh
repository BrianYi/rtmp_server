#!/bin/sh
# pull n appname, save_path, pusher n appname, video_path
# usage: start.sh puller-num appname dst_dir pusher-num appname video_path 
pusher_exe="../../hevc_pusher/Debug/hevc_pusher.exe"
server_exe="../../hevc_server/Debug/hevc_server.exe"
puller_exe="../../hevc_puller/Debug/hevc_puller.exe"
default_puller_num=3
default_pusher_num=1
default_app="live"
default_dst_dir="../../hevc_server/test"
default_video_path="/E/Movie/test video/bbb_sunflower_1080p_60fps_normal.mp4"

puller_num="${1:-$default_puller_num}"
puller_app="${2:-$default_app}"
dst_dir="${3:-$default_dst_dir}/$(date +%s%N)"
pusher_num="${4:-$default_pusher_num}"
pusher_app="${5:-$default_app}"
pusher_video_path="${6:-$default_video_path}"

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

