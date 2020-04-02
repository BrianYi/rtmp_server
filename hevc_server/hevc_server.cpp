// hevc_server.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

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
#include "Packet.h"
#include "Log.h"

#pragma comment(lib,"ws2_32.lib")

//#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#define TIMEOUT 3000
#define SERVER_PORT 5566


#ifdef LOCK_TIME_CACULATE
#define LOCK_TIME_BEG \
int64_t _lockTime = get_current_milli( );

#define LOCK_TIME_END \
int64_t _unlockTime = get_current_milli( ); \
int64_t _diff = _unlockTime - _lockTime; \
RTMP_Log( RTMP_LOGDEBUG, "lock time is %lldms, %s:%d", \
		  _diff, __FUNCTION__, __LINE__ );
#else
#define LOCK_TIME_BEG
#define LOCK_TIME_END
#endif // _DEBUG

enum
{
	STREAMING_START,
	STREAMING_IN_PROGRESS,
	STREAMING_STOPPING,
	STREAMING_STOPPED
};

struct ConnectionInfo
{
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
	std::list<ConnectionInfo*> conns;					// 所有连接
	std::unordered_map<std::string, std::list<ConnectionInfo*>> pullers;	// puller连接
	std::unordered_map<std::string, ConnectionInfo*> pushers;			// pusher连接
	int state;
	std::mutex mux;
	StatisticInfo stat;
};

inline fd_set get_fd_set( STREAMING_SERVER* server )
{
	fd_set fdSet;
	FD_ZERO( &fdSet );
	std::unique_lock<std::mutex> lock( server->mux );
	for ( auto it = server->conns.begin( );
		  it != server->conns.end( );
		  ++it )
	{
		FD_SET( (*it)->conn.m_socketID, &fdSet );
	}
	return fdSet;
}

void show_statistics( STREAMING_SERVER* server )
{
	//std::unique_lock<std::mutex> lock( server->mux );
	printf( "%-15s%-6s%-8s%-10s %-8s\t\t%-13s\t%-10s\t%-15s\t %-8s\t%-13s\t%-10s\t%-15s\n",
					   "ip","port","type","app",
					   "rec-byte", "rec-byte-rate", "rec-packet", "rec-packet-rate",
					   "snd-byte", "snd-byte-rate", "snd-packet", "snd-packet-rate" );
	
	
	printf( "%-15s%-6d%-8s%-10s %-6.2fMB\t\t%-9.2fKB/s\t%-10lld\t%-13lld/s\t %-6.2fMB\t%-9.2fKB/s\t%-10lld\t%-13lld/s\n",
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
		ConnectionInfo* ptrConnInfo = *it;
		printf( "%-15s%-6d%-8s%-10s %-6.2fMB\t\t%-9.2fKB/s\t%-10lld\t%-13lld/s\t %-6.2fMB\t%-9.2fKB/s\t%-10lld\t%-13lld/s\n",
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
		ptrConnInfo->app[ 0 ] = '\0';
		ptrConnInfo->type = TypeUnknown;
		ptrConnInfo->acceptTime = currentTime;
		ptrConnInfo->conn = conn;
		ptrConnInfo->lastRecvTime = currentTime;
		ptrConnInfo->lastSendTime = currentTime;
		//ptrConnInfo->timebase = 1000 / 25;
		ptrConnInfo->timestamp = 0;
		ptrConnInfo->isLost = false;
		ZeroMemory( &ptrConnInfo->stat, sizeof StatisticInfo );
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
			std::unique_lock<std::mutex> lock( server->mux );
		// add connections to link list
		server->conns.push_back( ptrConnInfo );
#if _DEBUG
		RTMP_Log( RTMP_LOGDEBUG, "recv request from %s:%u, total connections are %u",
				  conn.getIP( ).c_str( ),
				  conn.getPort( ),
				  server->conns.size( ) );
		LOCK_TIME_END
#endif // _DEBUG
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
#ifdef _DEBUG
	int64_t _timeBeg = time( 0 );
#endif // _DEBUG
	while ( server->state == STREAMING_START )
	{
		int recvSize = 0;
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG

		timeval tm { 0,100 };
		fd_set fdSet = get_fd_set( server );
		while ( select( 0, &fdSet, nullptr, nullptr, &tm ) <= 0 && 
				server->state == STREAMING_START )
		{
			fdSet = get_fd_set( server );
			Sleep( 10 );
		}
		std::unique_lock<std::mutex> lock( server->mux );
		auto connIter = server->conns.begin( );
		while ( connIter != server->conns.end( ) )
		{
			if ( !FD_ISSET( (*connIter)->conn.m_socketID, &fdSet ) )
			{
				++connIter;
				continue;
			}
			ConnectionInfo* ptrConnInfo = *connIter;
			PACKET pkt;
			if ( recv_packet( ptrConnInfo->conn, pkt, NonBlocking ) <= 0 ) // no packet, continue loop next
			{
				if ( ptrConnInfo->type == TypePusher )
				{
					server->pushers.erase( ptrConnInfo->app );
					RTMP_LogAndPrintf( RTMP_LOGDEBUG, "pusher for app[%s] from %s:%d has lost",
									   ptrConnInfo->app.c_str( ), ptrConnInfo->conn.getIP( ).c_str( ),
									   ptrConnInfo->conn.getPort( ) );
				}
				else if ( ptrConnInfo->type == TypePuller )
				{
					auto iter = server->pullers.find( ptrConnInfo->app );
					if ( iter != server->pullers.end( ) )
					{
						auto& pullers = iter->second;
						pullers.erase( find_if( pullers.begin( ),
												pullers.end( ), [ & ] ( auto puller )
						{
							if ( ptrConnInfo->acceptTime == puller->acceptTime )
							{
								RTMP_LogAndPrintf( RTMP_LOGDEBUG, "puller for app[%s] from %s:%d has lost",
												   ptrConnInfo->app.c_str( ), ptrConnInfo->conn.getIP( ).c_str( ),
												   ptrConnInfo->conn.getPort( ) );
								return true;
							}
							return false;
						} ) );
					}
				}
				else
				{
					RTMP_LogAndPrintf( RTMP_LOGDEBUG, "anonymous for app[%s] from %s:%d has lost",
									   ptrConnInfo->app.c_str( ), ptrConnInfo->conn.getIP( ).c_str( ),
									   ptrConnInfo->conn.getPort( ) );
				}
				connIter = server->conns.erase( connIter );
				delete ptrConnInfo;
				continue;	// has one connection lose
			}
#ifdef _DEBUG
			caculate_statistc( server->stat, pkt, StatRecv );
			caculate_statistc( ptrConnInfo->stat, pkt, StatRecv );
#endif // _DEBUG
			if ( maxRecvBuf < pkt.header.size )
			{
				maxRecvBuf = ( pkt.header.size + MAX_PACKET_SIZE - 1 ) / MAX_PACKET_SIZE * MAX_PACKET_SIZE;
				server->conn.set_socket_rcvbuf_size( maxRecvBuf );
				ptrConnInfo->conn.set_socket_rcvbuf_size( maxRecvBuf );
				ptrConnInfo->conn.set_socket_sndbuf_size( maxRecvBuf );
			}

			// valid packet, deal with packet
			ptrConnInfo->lastRecvTime = get_current_milli( );

			switch ( pkt.header.type )
			{
			case CreateStream:
			{
				// already exist stream
				if ( server->pushers.count( pkt.header.app ) )
				{
					send_err_packet( ptrConnInfo->conn,
									 get_current_milli( ),
									 pkt.header.app, NonBlocking );
				}
				else
				{
					// save time base and app name
					ptrConnInfo->timestamp = 0;
					//ptrConnInfo->timebase = pkt.header.reserved;
					ptrConnInfo->app = pkt.header.app;
					ptrConnInfo->type = TypePusher;
					ptrConnInfo->timebase = pkt.header.reserved;

					server->pushers[ ptrConnInfo->app ] = ptrConnInfo;


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
				if ( !server->pushers.count( pkt.header.app ) )
				{
					send_err_packet( ptrConnInfo->conn,
									 get_current_milli( ),
									 pkt.header.app, NonBlocking );
				}
				else
				{
					// exist stream
					// if puller is already exist
					auto pullers = server->pullers[ pkt.header.app ];
					if ( find_if( pullers.begin( ), pullers.end( ), [ & ] ( auto connInfo )
					{
						if ( connInfo->acceptTime == ptrConnInfo->acceptTime )
							return true;
						return false;
					} ) != pullers.end( ) )
						break;

					// save time base and app name
					ptrConnInfo->timestamp = 0;
					//ptrConnInfo->timebase = pkt.header.reserved;
					ptrConnInfo->app = pkt.header.app;
					ptrConnInfo->type = TypePuller;
					ptrConnInfo->timebase = server->pushers[ pkt.header.app ]->timebase;

					server->pullers[ ptrConnInfo->app ].push_back( ptrConnInfo );

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
				break;
			}
			case Push:
			{	// haven't receive SETUP
				if ( ptrConnInfo->app != pkt.header.app )
				{
					ptrConnInfo->isLost = true;
					break;
				}
				//StreamInfo streamInfo = server->streams[ ptrConnInfo->app ];
				if ( server->pushers.find( ptrConnInfo->app ) == server->pushers.end( ) )
				{
#ifdef _DEBUG
					RTMP_Log( RTMP_LOGERROR, "recv push packet, but doesn't have stream %s", ptrConnInfo->app );
#endif // _DEBUG
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
				if ( server->pushers.find( ptrConnInfo->app ) == server->pushers.end( ) )
				{
#ifdef _DEBUG
					RTMP_Log( RTMP_LOGERROR, "recv fin packet, but doesn't have stream %s", ptrConnInfo->app );
#endif // _DEBUG
					break;
				}
				PACKET* ptrPkt = alloc_fin_packet( pkt.header.timestamp,
												   pkt.header.app );
				streamData.push( ptrPkt );
				break;
			}
			case Alive:
			{
#ifdef _DEBUG
				int64_t currentTime = get_current_milli( );
				RTMP_Log( RTMP_LOGDEBUG,
						  "receive alive packet from %s:%u, packet time=%lld, currentTime=%lld, C-P=%lld",
						  ptrConnInfo->conn.getIP( ).c_str( ),
						  ptrConnInfo->conn.getPort( ),
						  pkt.header.timestamp,
						  currentTime,
						  currentTime - pkt.header.timestamp );
#endif // _DEBUG
				ptrConnInfo->lastRecvTime = get_current_milli( );
				break;
			}
			case Err:
			{
				RTMP_Log( RTMP_LOGDEBUG, "err packet." );
				break;
			}
			default:
				RTMP_Log( RTMP_LOGDEBUG, "unknown packet." );
				break;
			}
			++connIter;
			//	} while ( MP );
		}
		lock.unlock( );
#ifdef _DEBUG
		LOCK_TIME_END
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

#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
			std::unique_lock<std::mutex> lock( server->mux );
		PACKET* ptrPkt = streamData.top( );
		streamData.pop( );
		lock.unlock( );
		if ( server->pullers.find( ptrPkt->header.app ) == server->pullers.end( ) )
		{
#ifdef _DEBUG
			//RTMP_Log( RTMP_LOGDEBUG, "no pullers for stream %s", ptrPkt->header.app );
			LOCK_TIME_END
#endif // _DEBUG
				free_packet( ptrPkt );
			continue;
		}

		auto& pullers = server->pullers[ ptrPkt->header.app ];
		currentTime = get_current_milli( );
		waitTime = ptrPkt->header.timestamp - currentTime;
		if ( waitTime > 0 )
			Sleep( waitTime );

		timeval tm { 0,100 };
		fd_set fdSet = get_fd_set( server );
		while ( select( 0, nullptr, &fdSet, nullptr, &tm ) <= 0 &&
				server->state == STREAMING_START )
		{
			fdSet = get_fd_set( server );
			Sleep( 10 );
		};

		lock.lock( );
		for ( auto it = pullers.begin( );
			  it != pullers.end( );
			  ++it )
		{
			ConnectionInfo *puller = *it;
			if ( !FD_ISSET( puller->conn.m_socketID, &fdSet ) )
				continue;

			if ( send_packet( puller->conn, *ptrPkt, NonBlocking ) <= 0 )
				continue; // error
			puller->lastSendTime = currentTime;
#ifdef _DEBUG
			caculate_statistc( server->stat, *ptrPkt, StatSend );
			caculate_statistc( puller->stat, *ptrPkt, StatSend );
#endif // _DEBUG
		}
		free_packet( ptrPkt );
		lock.unlock( );
#ifdef _DEBUG
		LOCK_TIME_END
#endif // _DEBUG
	}
	RTMP_Log( RTMP_LOGDEBUG, "sender thread is quit." );
	return true;
}

// int thread_func_for_cleaner( void *arg )
// {
// 	RTMP_Log( RTMP_LOGDEBUG, "cleaner thread is start..." );
// 	STREAMING_SERVER* server = ( STREAMING_SERVER* ) arg;
// 	while ( server->state == STREAMING_START )
// 	{
// 		// deal with temp connections
// #ifdef _DEBUG
// 		LOCK_TIME_BEG
// #endif // _DEBUG
// 		std::unique_lock<std::mutex> lock( server->mux );
// 		int64_t currentTime = get_current_milli( );
// 		auto connIter = server->conns.begin( );
// 		while ( connIter != server->conns.end( ) )
// 		{
// 			if ( connIter->isLost ||
// 				 currentTime - connIter->lastRecvTime > TIMEOUT )
// 			{
// #ifdef _DEBUG
// 				RTMP_Log( RTMP_LOGDEBUG, "clean timeout connections %s:%u, lastRecvTime=%lld, currentTime=%lld, C-L=%lld",
// 						  connIter->conn.getIP( ).c_str( ),
// 						  connIter->conn.getPort( ),
// 						  connIter->lastRecvTime,
// 						  currentTime,
// 						  currentTime - connIter->lastRecvTime );
// #endif // _DEBUG
// 				// if is pusher, then cut out the stream deliver
// 				if ( connIter->type == Pusher )
// 				{
// 					RTMP_Log( RTMP_LOGDEBUG, "clean one pusher for stream %s", connIter->app.c_str( ) );
// 					server->pushers.erase( connIter->app );
// 				}
// 				else if ( connIter->type == Puller )
// 				{
// 					auto iter = server->pullers.find( connIter->app );
// 					if ( iter == server->pullers.end( ) )
// 					{
// 						RTMP_Log( RTMP_LOGDEBUG, "no pullers for stream %s", connIter->app.c_str( ) );
// 					}
// 					else
// 					{
// 						auto& pullers = iter->second;
// 						pullers.erase( find_if( pullers.begin( ),
// 												pullers.end( ), [ & ] ( auto& puller )
// 						{
// 							if ( connIter->acceptTime == puller.acceptTime )
// 							{
// 								RTMP_Log( RTMP_LOGDEBUG, "clean one puller for stream %s", connIter->app.c_str( ) );
// 								return true;
// 							}
// 							return false;
// 						} ) );
// 					}
// 				}
// 				connIter = server->conns.erase( connIter );
// 			}
// 			else
// 				++connIter;
// 		}
// 		lock.unlock( );
// #ifdef _DEBUG
// 		LOCK_TIME_END
// #endif // _DEBUG
// 			Sleep( 1000 );
// 	}
// 	RTMP_Log( RTMP_LOGDEBUG, "cleaner thread is quit." );
// 	return true;
// }


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
// 		if ( choice == "quit" || choice == "q" || choice == "exit" )
// 		{
// 			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "Exiting" );
// 			stopStreaming( server );
// 		}
// 		else if ( choice == "status" || choice == "s" )
// 		{
// 			auto& pullers = server->pullers;
// 			auto& pushers = server->pushers;
// 			int totalPullers = 0;
// 			int totalPushers = 0;
// 			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "====== Online Pullers ======" );
// 			std::unique_lock<std::mutex> lock( server->mux );
// 			for ( auto puller = pullers.begin( );
// 				  puller != pullers.end( );
// 				  ++puller )
// 			{
// 				std::string app = puller->first;
// 				int num = puller->second.size( );
// 				totalPullers += num;
// 				RTMP_LogAndPrintf( RTMP_LOGDEBUG, "app[%s] people[%d], ", app.c_str( ), num );
// 			}
// 			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "total pullers: %d", totalPullers );
// 
// 			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "====== Online Pushers ======" );
// 			for ( auto pusher = pushers.begin( );
// 				  pusher != pushers.end( );
// 				  ++pusher )
// 			{
// 				std::string app = pusher->first;
// 				++totalPushers;
// 				RTMP_LogAndPrintf( RTMP_LOGDEBUG, "app[%s], ", app.c_str( ) );
// 			}
// 			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "total pushers: %d", totalPushers );
// 			show_statistics( server );
// 		}
// 		else
// 		{
// 			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "Unknown command \'%s\', ignoring", choice.c_str( ) );
// 		}
	}
	RTMP_Log( RTMP_LOGDEBUG, "controller thread is quit." );
	return true;
}
int main( )
{
	init_sockets( );
	STREAMING_SERVER* server = new STREAMING_SERVER;
	server->state = STREAMING_START;
	ZeroMemory( &server->stat, sizeof StatisticInfo );
	server->conn.listen_on_port( SERVER_PORT );

#ifdef _DEBUG
	FILE *dumpfile = fopen( "hevc_server.dump", "a+" );
	RTMP_LogSetOutput( dumpfile );
	RTMP_LogSetLevel( RTMP_LOGALL );
	RTMP_LogThreadStart( );

	SYSTEMTIME tm;
	GetSystemTime( &tm );
	RTMP_Log( RTMP_LOGDEBUG, "==============================" );
	RTMP_Log( RTMP_LOGDEBUG, "log file:\thevc_server.dump" );
	RTMP_Log( RTMP_LOGDEBUG, "log timestamp:\t%lld", get_current_milli( ) );
	RTMP_Log( RTMP_LOGDEBUG, "log date:\t%d-%d-%d %d:%d:%d.%d",
			  tm.wYear,
			  tm.wMonth,
			  tm.wDay,
			  tm.wHour + 8, tm.wMinute, tm.wSecond, tm.wMilliseconds );
	RTMP_Log( RTMP_LOGDEBUG, "==============================" );
#endif
	std::thread controller( thread_func_for_controller, server );
	std::thread accepter( thread_func_for_accepter, server );
	std::thread receiver( thread_func_for_receiver, server );
	std::thread sender( thread_func_for_sender, server );
	//std::thread cleaner( thread_func_for_cleaner, server );

	controller.join( );
	accepter.join( );
	receiver.join( );
	sender.join( );
	//cleaner.join( );
#ifdef _DEBUG
	RTMP_LogThreadStop( );
#endif // _DEBUG
	Sleep( 10 );

	if ( server )
		delete server;
#ifdef _DEBUG
	if ( dumpfile )
		fclose( dumpfile );
#endif
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
