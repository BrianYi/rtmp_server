#!/bin/sh
for dir in $(ls | egrep "[[:digit:]]{10,}")
do
	puller_pid_file="$dir/pullers.pid"
	pusher_pid_file="$dir/pushers.pid"
	if [ -f $puller_pid_file ]; then
		while read puller_pid
		do
			kill $puller_pid
			echo "killed puller(${puller_pid})."
		done < $puller_pid_file
	fi
	
	if [ -f $pusher_pid_file ]; then
		while read pusher_pid
		do
			kill $pusher_pid
			echo "killed pusher(${pusher_pid})."
		done < $pusher_pid_file
	fi
done