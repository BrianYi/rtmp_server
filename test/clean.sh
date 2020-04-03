#!/bin/sh
for dir in $(ls | egrep "[[:digit:]]{10,}")
do
	read -p "Delete $dir?(y/n)" ans
	[ "$ans" != "n" ] && rm -r $dir
done