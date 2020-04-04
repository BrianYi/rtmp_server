/*
 * Copyright (C) 2020 BrianYi, All rights reserved
 */

#include <iostream>
#include <thread>
#include <vector>
#include <list>
#include <time.h>
#include <unordered_map>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <chrono>
#include <queue>
#include "TCP.h"
//#define TIME_CACULATE
#include "Packet.h"
#include "Log.h"
#include <conio.h>

#pragma comment(lib,"ws2_32.lib")

//#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#define TIMEOUT 3000
#define SERVER_PORT 5566

enum
{
	STREAMING_START,
	STREAMING_IN_PROGRESS,
	STREAMING_STOPPING,
	STREAMING_STOPPED
};

struct ConnectionInfo
{
	int64_t id;
	int64_t acceptTime;
	TCP conn;
	int type;
	std::string app;
	int64_t timestamp;
	//int64_t timebase;
	int64_t lastSendTime;
	int64_t lastRecvTime;
	int32_t seq;
	int timebase;
	bool isLost;
	int sendTimeoutNum;
	StatisticInfo stat;
};

auto cmp = [ ] ( PACKET* ptrPktL, PACKET* ptrPktR )
{
	if ( ptrPktL->header.timestamp > ptrPktR->header.timestamp )
		return true;
	else if ( ptrPktL->header.timestamp == ptrPktR->header.timestamp )
		return ptrPktL->header.seq > ptrPktR->header.seq;
	return false;
};
typedef std::priority_queue<PACKET*, std::vector<PACKET*>, decltype( cmp )> StreamData;
StreamData streamData( cmp );

struct STREAMING_SERVER
{
	TCP conn;
	std::unordered_map<SOCKET,ConnectionInfo*> conns;	// fd-conninfo
	fd_set fdConnSet;									// all connection fd
	fd_set fdPusherSet;									// all pusher connection fd
	fd_set fdPullerSet;									// all puller connection fd
	std::unordered_map<std::string, fd_set> pullersMap;	// app-pullers
	int state;
	std::mutex mux;
	StatisticInfo stat;
};

void show_statistics( STREAMING_SERVER* server )
{
	//std::unique_lock<std::mutex> lock( server->mux );
	printf( "%-15s%-6s%-8s%-20s %-8s\t\t%-13s\t%-10s\t%-15s\t %-8s\t%-13s\t%-10s\t%-15s\n",
					   "ip","port","type","app",
					   "rec-byte", "rec-byte-rate", "rec-packet", "rec-packet-rate",
					   "snd-byte", "snd-byte-rate", "snd-packet", "snd-packet-rate" );
	
	
	printf( "%-15s%-6d%-8s%-20s %-6.2fMB\t\t%-9.2fKB/s\t%-10lld\t%-13lld/s\t %-6.2fMB\t%-9.2fKB/s\t%-10lld\t%-13lld/s\n",
					   server->conn.getIP().c_str(),
					   server->conn.getPort(),
					   "server",
					   "null",

					   MB(server->stat.recvBytes),
					   KB(server->stat.recvByteRate),
					   server->stat.recvPackets,
					   server->stat.recvPacketRate,

					   MB(server->stat.sendBytes),
					   KB(server->stat.sendByteRate),
					   server->stat.sendPackets,
					   server->stat.sendPacketRate );

	auto it = server->conns.begin( );
	while ( it != server->conns.end( ) )
	{
		ConnectionInfo* ptrConnInfo = it->second;
		printf( "%-15s%-6d%-8s%-20s %-6.2fMB\t\t%-9.2fKB/s\t%-10lld\t%-13lld/s\t %-6.2fMB\t%-9.2fKB/s\t%-10lld\t%-13lld/s\n",
				ptrConnInfo->conn.getIP( ).c_str( ),
				ptrConnInfo->conn.getPort( ),
				TYPE_STR( ptrConnInfo->type ),
				ptrConnInfo->app.c_str( ),

				MB( ptrConnInfo->stat.sendBytes ),
				KB( ptrConnInfo->stat.sendByteRate ),
				ptrConnInfo->stat.sendPackets,
				ptrConnInfo->stat.sendPacketRate,
				MB( ptrConnInfo->stat.recvBytes ),
				KB( ptrConnInfo->stat.recvByteRate ),
				ptrConnInfo->stat.recvPackets,
				ptrConnInfo->stat.recvPacketRate );
		++it;
	}
}

bool init_sockets( )
{
#ifdef WIN32
	WORD version = MAKEWORD( 1, 1 );
	WSADATA wsaData;
	return ( WSAStartup( version, &wsaData ) == 0 );
#endif
	return true;
}

void cleanup_sockets( )
{
#ifdef WIN32
	WSACleanup( );
#endif
}


void
stopStreaming( STREAMING_SERVER * server )
{
	if ( server->state != STREAMING_STOPPED )
	{
		if ( server->state == STREAMING_IN_PROGRESS )
		{
			server->state = STREAMING_STOPPING;

			// wait for streaming threads to exit
			while ( server->state != STREAMING_STOPPED )
				Sleep( 10 );
		}
		server->state = STREAMING_STOPPED;
	}
}

/*
 * 接收client,publisher的连接
 */
int thread_func_for_accepter( void *arg )
{
	RTMP_Log( RTMP_LOGDEBUG, "accepter thread is start..." );
	STREAMING_SERVER* server = ( STREAMING_SERVER* ) arg;
	static int64_t id = 0;
	while ( server->state == STREAMING_START )
	{
		//
		TCP conn = server->conn.accept_client( NonBlocking );
		if ( conn.m_socketID == INVALID_SOCKET )
		{
			Sleep( 5 );
			continue;
		}
		//conn.set_socket_rcvbuf_size( MAX_PACKET_SIZE );
		ConnectionInfo *ptrConnInfo = new ConnectionInfo;
		int64_t currentTime = get_current_milli( );
		ptrConnInfo->id = id++;
		ptrConnInfo->app[ 0 ] = '\0';
		ptrConnInfo->type = TypeUnknown;
		ptrConnInfo->acceptTime = currentTime;
		ptrConnInfo->conn = conn;
		ptrConnInfo->lastRecvTime = currentTime;
		ptrConnInfo->lastSendTime = currentTime;
		//ptrConnInfo->timebase = 1000 / 25;
		ptrConnInfo->timestamp = 0;
		ptrConnInfo->isLost = false;
		ptrConnInfo->sendTimeoutNum = 0;
		std::unique_lock<std::mutex> lock( server->mux );
		FD_SET( ptrConnInfo->conn.m_socketID, &server->fdConnSet );
		ZeroMemory( &ptrConnInfo->stat, sizeof StatisticInfo );
		// add connections to link list
		server->conns.insert( std::make_pair( ptrConnInfo->conn.m_socketID, ptrConnInfo ) );
		RTMP_Log( RTMP_LOGDEBUG, "recv request from %s:%u, total connections are %u",
				  conn.getIP( ).c_str( ),
				  conn.getPort( ),
				  server->conns.size( ) );
	}
	RTMP_Log( RTMP_LOGDEBUG, "accepter thread is quit." );
	return true;
}

/*
 * 接收数据,接收Publisher发来的数据
 */
int thread_func_for_receiver( void *arg )
{
	RTMP_Log( RTMP_LOGDEBUG, "receiver thread is start..." );
	STREAMING_SERVER* server = ( STREAMING_SERVER* ) arg;
	int32_t maxRecvBuf = SEND_BUF_SIZE;
	while ( server->state == STREAMING_START )
	{
#ifdef _DEBUG
		TIME_BEG( 1 ); // 274ms 260ms
#endif // _DEBUG
		int recvSize = 0;
		timeval tm { 1,0 };
		fd_set fdConnSet = server->fdConnSet;
		while ( select( 0, &fdConnSet, nullptr, nullptr, &tm ) <= 0 &&
				server->state == STREAMING_START )
		{
			fdConnSet = server->fdConnSet;
			Sleep( 5 );
		}
#ifdef _DEBUG
		TIME_END( 1 );
#endif // _DEBUG

#ifdef _DEBUG
		TIME_BEG( 2 ); //1162ms
#endif // _DEBUG
		std::unique_lock<std::mutex> lock( server->mux );
		auto& connMap = server->conns;
		for ( uint32_t i = 0; i < fdConnSet.fd_count; ++i )
		{
			auto ptrConnInfo = connMap[ fdConnSet.fd_array[ i ] ];
			if (!FD_ISSET( ptrConnInfo->conn.m_socketID, &fdConnSet ))
			{
				RTMP_Log( RTMP_LOGDEBUG, "error" );
				break;
			}

			PACKET pkt;
#ifdef _DEBUG
			TIME_BEG( 3 );
#endif // _DEBUG
			if ( recv_packet( ptrConnInfo->conn, pkt, NonBlocking ) <= 0 )
			{
				if ( ptrConnInfo->type == TypePusher )
				{
					//server->pushers.erase( ptrConnInfo->app );
					FD_CLR( ptrConnInfo->conn.m_socketID, &server->fdPusherSet );
					RTMP_LogAndPrintf( RTMP_LOGDEBUG, "pusher for app[%s] from %s:%d has lost",
									   ptrConnInfo->app.c_str( ), ptrConnInfo->conn.getIP( ).c_str( ),
									   ptrConnInfo->conn.getPort( ) );
				}
				else if ( ptrConnInfo->type == TypePuller )
				{
					FD_CLR( ptrConnInfo->conn.m_socketID, &server->fdPullerSet );
					FD_CLR( ptrConnInfo->conn.m_socketID, &server->pullersMap[ ptrConnInfo->app ] );
				}
				else
				{
					RTMP_LogAndPrintf( RTMP_LOGDEBUG, "anonymous for app[%s] from %s:%d has lost",
									   ptrConnInfo->app.c_str( ), ptrConnInfo->conn.getIP( ).c_str( ),
									   ptrConnInfo->conn.getPort( ) );
				}
				FD_CLR( ptrConnInfo->conn.m_socketID, &server->fdConnSet );
				server->conns.erase( ptrConnInfo->conn.m_socketID );
				delete ptrConnInfo;
				continue;	// has one connection lose
			}
#ifdef _DEBUG
			TIME_END( 3 );
#endif
			caculate_statistc( server->stat, pkt, StatRecv );
			caculate_statistc( ptrConnInfo->stat, pkt, StatRecv );

			if ( maxRecvBuf < pkt.header.size )
			{
				maxRecvBuf = ( pkt.header.size + MAX_PACKET_SIZE - 1 ) / MAX_PACKET_SIZE * MAX_PACKET_SIZE;
				server->conn.set_socket_rcvbuf_size( maxRecvBuf );
				ptrConnInfo->conn.set_socket_rcvbuf_size( maxRecvBuf );
				ptrConnInfo->conn.set_socket_sndbuf_size( maxRecvBuf );
			}

			// valid packet, deal with packet
			ptrConnInfo->lastRecvTime = get_current_milli( );
#ifdef _DEBUG
			TIME_BEG( 4 );
#endif // _DEBUG
			switch ( pkt.header.type )
			{
			case CreateStream:
			{
				// already exist stream
				auto& pushers = server->fdPusherSet;
				bool isExistStream = false;
				for ( uint32_t i = 0; i < pushers.fd_count; ++i )
				{
					auto& connInfo = server->conns[ pushers.fd_array[ i ] ];
					if ( connInfo->app == pkt.header.app )
					{
						if (connInfo->id == ptrConnInfo->id )//same connection
							// send ack back
							send_ack_packet( ptrConnInfo->conn,
											 get_current_milli( ),
											 pkt.header.app,
											 0, NonBlocking );
						else
							send_err_packet( ptrConnInfo->conn,
											 get_current_milli( ),
											 pkt.header.app, NonBlocking );
						isExistStream = true;
						break;
					}
				}
				if (!isExistStream )
				{
					// save time base and app name
					ptrConnInfo->timestamp = 0;
					//ptrConnInfo->timebase = pkt.header.reserved;
					ptrConnInfo->app = pkt.header.app;
					ptrConnInfo->type = TypePusher;
					ptrConnInfo->timebase = pkt.header.reserved;

					FD_SET( ptrConnInfo->conn.m_socketID, &pushers );

					// send ack back
					send_ack_packet( ptrConnInfo->conn,
									 get_current_milli( ),
									 pkt.header.app,
									 0, NonBlocking );

					RTMP_LogAndPrintf( RTMP_LOGDEBUG, "pusher from %s:%d has create app[%s].",
									   ptrConnInfo->conn.getIP( ).c_str( ),
									   ptrConnInfo->conn.getPort( ),
									   ptrConnInfo->app.c_str( ) );
				}
				break;
			}
			case Play:
			{
				// doesn't exist stream
				auto& pushers = server->fdPusherSet;
				bool isExistStream = false;
				int timebase = 0;
				for ( uint32_t i = 0; i < pushers.fd_count; ++i )
				{
					auto& connInfo = server->conns[ pushers.fd_array[ i ] ];
					if ( connInfo->app == pkt.header.app )
					{
						timebase = connInfo->timebase;
						isExistStream = true;
						break;
					}
				}
				
				if ( !isExistStream )
				{
					send_err_packet( ptrConnInfo->conn,
									get_current_milli( ),
									pkt.header.app, NonBlocking );
				}
				else
				{
					auto& pullers = server->fdPullerSet;
					bool isTheSamePuller = false;
					for ( uint32_t i = 0; i < pullers.fd_count; ++i )
					{
						auto& connIno = server->conns[ pullers.fd_array[ i ] ];
						if ( connIno->id == ptrConnInfo->id )
						{
							isTheSamePuller = true;
							break;
						}
					}
					if ( !isTheSamePuller ) // new puller
					{
						// save time base and app name
						ptrConnInfo->timestamp = 0;
						//ptrConnInfo->timebase = pkt.header.reserved;
						ptrConnInfo->app = pkt.header.app;
						ptrConnInfo->type = TypePuller;
						ptrConnInfo->timebase = timebase;

						FD_SET( ptrConnInfo->conn.m_socketID, &server->fdPullerSet );
						if ( !server->pullersMap.count( ptrConnInfo->app ) )
							FD_ZERO( &server->pullersMap[ ptrConnInfo->app ] );
						FD_SET( ptrConnInfo->conn.m_socketID, &server->pullersMap[ ptrConnInfo->app ] );

						// send ack back
						send_ack_packet( ptrConnInfo->conn,
										 get_current_milli( ),
										 pkt.header.app,
										 ptrConnInfo->timebase, NonBlocking );

						RTMP_LogAndPrintf( RTMP_LOGDEBUG, "puller from %s:%d is playing app[%s].",
										   ptrConnInfo->conn.getIP( ).c_str( ),
										   ptrConnInfo->conn.getPort( ),
										   ptrConnInfo->app.c_str( ) );
					}

				}
				break;
			}
			case Push:
			{	// haven't receive SETUP
				if ( ptrConnInfo->app != pkt.header.app )
				{
					ptrConnInfo->isLost = true;
					RTMP_Log( RTMP_LOGDEBUG, "streamData.size() == %s", streamData.size( ) );
					break;
				}

				//int64_t nextTimestamp = pkt.header.timestamp + server->pushers[ ptrConnInfo->app ].timebase;
				PACKET* ptrPkt = alloc_pull_packet( pkt.header.size,
													pkt.header.MP,
													pkt.header.seq,
													//nextTimestamp,
													pkt.header.timestamp,
													pkt.header.app,
													pkt.body );
				streamData.push( ptrPkt );
				break;
			}
			case Fin:// retransmit directly
			{
				// haven't receive SETUP
				if ( ptrConnInfo->app != pkt.header.app )
				{
					ptrConnInfo->isLost = true;
					break;
				}

				PACKET* ptrPkt = alloc_fin_packet( pkt.header.timestamp,
												   pkt.header.app );
				streamData.push( ptrPkt );
				break;
			}
			case Err:
			{
				RTMP_Log( RTMP_LOGERROR, "err packet." );
				break;
			}
			default:
				RTMP_Log( RTMP_LOGDEBUG, "unknown packet." );
				break;
			}

#ifdef _DEBUG
			TIME_END( 4 );
#endif // _DEBUG
		}
		lock.unlock( );
#ifdef _DEBUG
		TIME_END( 2 );
#endif // _DEBUG
	}
	RTMP_Log( RTMP_LOGDEBUG, "receiver thread is quit." );
	return true;
}

/*
 * 分发数据,根据streamId分发数据
 */
int thread_func_for_sender( void *arg )
{
	RTMP_Log( RTMP_LOGDEBUG, "sender thread is start..." );
	STREAMING_SERVER* server = ( STREAMING_SERVER* ) arg;
	int64_t currentTime = 0;
	int64_t waitTime = 0;
	while ( server->state == STREAMING_START )
	{
		if ( streamData.empty( ) )
		{
			Sleep( 10 );
			continue;
		}


		std::unique_lock<std::mutex> lock( server->mux );
		while ( !streamData.empty( ) )
		{
#ifdef _DEBUG
			TIME_BEG( 3 );	// 1194ms
#endif // _DEBUG
			RTMP_Log( RTMP_LOGDEBUG, "streamData.size() == %d", streamData.size() );
			PACKET* ptrPkt = streamData.top( );
			streamData.pop( );
			
			auto iter = server->pullersMap.find(ptrPkt->header.app);
			if ( iter == server->pullersMap.end( ) || iter->second.fd_count == 0)
			{
#ifdef _DEBUG
				RTMP_Log( RTMP_LOGDEBUG, "no pullers for stream %s", ptrPkt->header.app );
#endif // _DEBUG
					free_packet( ptrPkt );
				continue;
			}
#ifdef _DEBUG
			TIME_BEG( 11 );
#endif // _DEBUG
			timeval tm { 0,100 };
			fd_set timeoutPullers = iter->second;
			fd_set timeoutPullersCopy = timeoutPullers;
			fd_set testFd;
			while ( timeoutPullers.fd_count )
			{
				timeoutPullersCopy = timeoutPullers;
				for ( uint32_t i = 0; i < timeoutPullersCopy.fd_count; ++i )
				{
					FD_ZERO( &testFd );
					FD_SET( timeoutPullersCopy.fd_array[ i ], &testFd );
					if ( select( 0, nullptr, &testFd, nullptr, &tm ) <= 0 )
						continue;
					else
					{
						FD_CLR( timeoutPullersCopy.fd_array[ i ], &timeoutPullers );

// 						currentTime = get_current_milli( );
// 						waitTime = ptrPkt->header.timestamp - currentTime;
// 						if ( waitTime > 0 )
// 							Sleep( waitTime );

						ConnectionInfo* puller = server->conns[ testFd.fd_array[ 0 ] ];
						if ( send_packet( puller->conn, *ptrPkt, NonBlocking ) <= 0 )
						{
							RTMP_LogAndPrintf( RTMP_LOGDEBUG, "send_packet error %s:%d", __FUNCTION__, __LINE__ );
							continue; // error
						}
						puller->lastSendTime = currentTime;

						caculate_statistc( server->stat, *ptrPkt, StatSend );
						caculate_statistc( puller->stat, *ptrPkt, StatSend );

					}
				}
			}
			free_packet( ptrPkt );

#ifdef _DEBUG
			TIME_END( 3 );
			TIME_END( 11 );
#endif // _DEBUG
		}
		lock.unlock( );
	}
	RTMP_Log( RTMP_LOGDEBUG, "sender thread is quit." );
	return true;
}

int thread_func_for_controller( void *arg )
{
	RTMP_Log( RTMP_LOGDEBUG, "controller thread is start..." );
	STREAMING_SERVER* server = ( STREAMING_SERVER* ) arg;
	std::string choice;
	while ( server->state == STREAMING_START )
	{
		system( "cls" );
		show_statistics( server );
		Sleep( 1000 );
// 		std::cin >> choice;
// 		if (choice == "s" || choice == "status" )
// 		{
// 			show_statistics( server );
// 		}
// 		else if ( choice == "quit" )
// 		{
// 			stopStreaming( server );
// 			Sleep( 10 );
// 			break;
// 		}
	}
	RTMP_Log( RTMP_LOGDEBUG, "controller thread is quit." );
	return true;
}
int main( int argc, char* argv[ ] )
{
	init_sockets( );
	STREAMING_SERVER* server = new STREAMING_SERVER;
	server->state = STREAMING_START;
	ZeroMemory( &server->stat, sizeof StatisticInfo );
	server->conn.listen_on_port( SERVER_PORT );
	FD_ZERO( &server->fdConnSet );
	FD_ZERO( &server->fdPullerSet );
	FD_ZERO( &server->fdPusherSet );


	FILE* dumpfile = nullptr;
	if ( argv[ 1 ] )
		dumpfile = fopen( argv[ 1 ], "a+" );
	else
		dumpfile = fopen( "rtmp_server.dump", "a+" );
	RTMP_LogSetOutput( dumpfile );
	RTMP_LogSetLevel( RTMP_LOGALL );
	RTMP_LogThreadStart( );

	SYSTEMTIME tm;
	GetSystemTime( &tm );
	RTMP_Log( RTMP_LOGDEBUG, "==============================" );
	RTMP_Log( RTMP_LOGDEBUG, "log file:\trtmp_server.dump" );
	RTMP_Log( RTMP_LOGDEBUG, "log timestamp:\t%lld", get_current_milli( ) );
	RTMP_Log( RTMP_LOGDEBUG, "log date:\t%d-%d-%d %d:%d:%d.%d",
			  tm.wYear,
			  tm.wMonth,
			  tm.wDay,
			  tm.wHour + 8, tm.wMinute, tm.wSecond, tm.wMilliseconds );
	RTMP_Log( RTMP_LOGDEBUG, "==============================" );

	std::thread controller( thread_func_for_controller, server );
	std::thread accepter( thread_func_for_accepter, server );
	std::thread receiver( thread_func_for_receiver, server );
	std::thread sender( thread_func_for_sender, server );

	controller.join( );
	accepter.join( );
	receiver.join( );
	sender.join( );

	RTMP_LogThreadStop( );

	Sleep( 10 );

	if ( server )
		delete server;

	if ( dumpfile )
		fclose( dumpfile );

	cleanup_sockets( );

#ifdef _DEBUG
	_CrtDumpMemoryLeaks( );
#endif // _DEBUG
	return 0;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
