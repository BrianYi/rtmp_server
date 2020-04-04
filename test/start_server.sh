#!/bin/sh
# you best start server from source code
server_exe="../../rtmp_server/Debug/rtmp_server.exe"
dump_path="./server-dump$(date +%s%N).txt"
echo "start server..."
$server_exe "$dump_path"
echo "shutdown server."