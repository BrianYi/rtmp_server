#!/bin/sh
for dump in $(ls | grep "server-dump")
do
	read -p "Delete $dump?(y/n)" ans
	[ "$ans" != "n" ] && rm -r $dump
done
for dir in $(ls | egrep "[[:digit:]]{10,}")
do
	# avoid removing accendently
	read -p "Delete $dir?(y/n)" ans
	[ "$ans" != "n" ] && rm -r $dir
done

read -p "Delete all other single dump file?(y/n)" ans
if [ "$ans" != "n" ]; then
	rm '../../rtmp_pusher/rtmp_pusher/rtmp_pusher.dump'
	rm '../../rtmp_server/rtmp_server/rtmp_server.dump'
	rm '../../rtmp_puller/rtmp_puller/rtmp_puller.dump'
fi