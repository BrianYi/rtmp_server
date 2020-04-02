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

enum
{
	Anonymous,
	Pusher,
	Puller,
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
};

auto cmp = [ ] ( PACKET* ptrPktL, PACKET* ptrPktR ) 
{ 
	if ( ptrPktL->header.timestamp > ptrPktR->header.timestamp )
		return true;
	else if ( ptrPktL->header.timestamp == ptrPktR->header.timestamp )
		return ptrPktL->header.seq > ptrPktR->header.seq;
	return false;
};
typedef std::priority_queue<PACKET*, std::vector<PACKET*>, decltype(cmp)> StreamData;
StreamData streamData( cmp );

struct STREAMING_SERVER
{
	TCP conn;
	std::list<ConnectionInfo> conns;									// 所有连接
	std::unordered_map<std::string, std::list<ConnectionInfo>> pullers;	// puller连接
	std::unordered_map<std::string, ConnectionInfo> pushers;			// pusher连接
	int state;
	std::mutex mux;
};



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
		ConnectionInfo connInfo;
		int64_t currentTime = get_current_milli( );
		connInfo.app[0] = '\0';
		connInfo.type = Anonymous;
		connInfo.acceptTime = currentTime;
		connInfo.conn = conn;
		connInfo.lastRecvTime = currentTime;
		connInfo.lastSendTime = currentTime;
		//connInfo.timebase = 1000 / 25;
		connInfo.timestamp = 0;
		connInfo.isLost = false;
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
		std::unique_lock<std::mutex> lock( server->mux );
		// add connections to link list
		server->conns.push_back( connInfo );
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
	while ( server->state == STREAMING_START )
	{
		int recvSize = 0;
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
		std::unique_lock<std::mutex> lock( server->mux );
		for ( auto connIter = server->conns.begin( );
			  connIter != server->conns.end( );
			  ++connIter )
		{
			// receive packet
			int MP = 0;
			do
			{
				PACKET pkt;
				if ( recv_packet( connIter->conn, pkt, NonBlocking ) <= 0 ) // no packet, continue loop next
					break; //continue;
				if ( maxRecvBuf < pkt.header.size )
				{
					maxRecvBuf = ( pkt.header.size + MAX_PACKET_SIZE - 1 ) / MAX_PACKET_SIZE * MAX_PACKET_SIZE;
					server->conn.set_socket_rcvbuf_size( maxRecvBuf );
					connIter->conn.set_socket_rcvbuf_size( maxRecvBuf );
					connIter->conn.set_socket_sndbuf_size( maxRecvBuf );
				}

				MP = pkt.header.MP;
				// valid packet, deal with packet
				connIter->lastRecvTime = get_current_milli( );

				switch ( pkt.header.type )
				{
				case CreateStream:
				{
					// already exist stream
					if ( server->pushers.count( pkt.header.app ) )
					{
						send_err_packet( connIter->conn, 
										 get_current_milli(), 
										 pkt.header.app );
					}
					else
					{
						// save time base and app name
						connIter->timestamp = 0;
						//connIter->timebase = pkt.header.reserved;
						connIter->app = pkt.header.app;
						connIter->type = Pusher;
						connIter->timebase = pkt.header.reserved;

						server->pushers.insert( 
							make_pair( std::string(connIter->app), *connIter ) );


						// send ack back
						send_ack_packet( connIter->conn,
										 get_current_milli( ),
										 pkt.header.app,
										 0 );
					}
					break;
				}
				case Play:
				{
					// doesn't exist stream
					if ( !server->pushers.count( pkt.header.app ) )
					{
						send_err_packet( connIter->conn, 
										 get_current_milli( ), 
										 pkt.header.app );
					}
					else
					{
						// exist stream
						// if puller is already exist
						auto pullers = server->pullers[ pkt.header.app ];
						if (find_if( pullers.begin( ), pullers.end( ), [&](auto& connInfo){
							if ( connInfo.acceptTime == connIter->acceptTime )
								return true;
							return false;
						} ) != pullers.end( ) )
							break;

						// save time base and app name
						connIter->timestamp = 0;
						//connIter->timebase = pkt.header.reserved;
						connIter->app = pkt.header.app;
						connIter->type = Puller;
						connIter->timebase = server->pushers[ pkt.header.app ].timebase;

						server->pullers[connIter->app].push_back(*connIter );

						// send ack back
						send_ack_packet( connIter->conn, 
										 get_current_milli( ), 
										 pkt.header.app, 
										 connIter->timebase );
					}
					break;
				}
				case Push:
				{	// haven't receive SETUP
					if ( connIter->app != pkt.header.app )
					{
						connIter->isLost = true;
						break;
					}
					//StreamInfo streamInfo = server->streams[ connIter->app ];
					if ( server->pushers.find( connIter->app ) == server->pushers.end( ) )
					{
	#ifdef _DEBUG
						RTMP_Log( RTMP_LOGERROR, "recv push packet, but doesn't have stream %s", connIter->app );
	#endif // _DEBUG
						break;
					}
					//int64_t nextTimestamp = pkt.header.timestamp + server->pushers[ connIter->app ].timebase;
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
					if ( connIter->app != pkt.header.app )
					{
						connIter->isLost = true;
						break;
					}
					if ( server->pushers.find( connIter->app ) == server->pushers.end( ) )
					{
	#ifdef _DEBUG
						RTMP_Log( RTMP_LOGERROR, "recv fin packet, but doesn't have stream %s", connIter->app );
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
							  connIter->conn.getIP( ).c_str( ),
							  connIter->conn.getPort( ),
							  pkt.header.timestamp,
							  currentTime,
							  currentTime - pkt.header.timestamp );
	#endif // _DEBUG
					connIter->lastRecvTime = get_current_milli( );
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
			} while ( MP );
		}
		lock.unlock( );
#ifdef _DEBUG
		LOCK_TIME_END
#endif // _DEBUG
		Sleep( 10 );
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
		if ( server->pullers.find( ptrPkt->header.app ) == server->pullers.end() )
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
		for ( auto puller = pullers.begin( );
			  puller != pullers.end( );
			  ++puller )
		{
			puller->lastSendTime = currentTime;
			while ( send_packet( puller->conn, *ptrPkt ) <= 0 )
				;
		}
		free_packet( ptrPkt );
#ifdef _DEBUG
		LOCK_TIME_END
#endif // _DEBUG
	}
	RTMP_Log(RTMP_LOGDEBUG,  "sender thread is quit." );
	return true;
}

int thread_func_for_cleaner( void *arg )
{
	RTMP_Log(RTMP_LOGDEBUG,  "cleaner thread is start..." );
	STREAMING_SERVER* server = ( STREAMING_SERVER* ) arg;
	while ( server->state == STREAMING_START )
	{
		// deal with temp connections
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
		std::unique_lock<std::mutex> lock( server->mux );
		int64_t currentTime = get_current_milli( );
		auto tmpConnIter = server->conns.begin( );
		while ( tmpConnIter != server->conns.end( ) )
		{
			if ( tmpConnIter->isLost ||
				 currentTime - tmpConnIter->lastRecvTime > TIMEOUT)
			{
#ifdef _DEBUG
				RTMP_Log( RTMP_LOGDEBUG, "clean timeout connections %s:%u, lastRecvTime=%lld, currentTime=%lld, C-L=%lld",
						  tmpConnIter->conn.getIP( ).c_str( ),
						  tmpConnIter->conn.getPort( ),
						  tmpConnIter->lastRecvTime,
						  currentTime,
						  currentTime - tmpConnIter->lastRecvTime );
#endif // _DEBUG
				// if is pusher, then cut out the stream deliver
				if (tmpConnIter->type == Pusher )
				{
					RTMP_Log( RTMP_LOGDEBUG, "clean one pusher for stream %s", tmpConnIter->app.c_str() );
					server->pushers.erase( tmpConnIter->app );
				}
				else if ( tmpConnIter->type == Puller )
				{
					auto iter = server->pullers.find( tmpConnIter->app );
					if ( iter == server->pullers.end( ) )
					{
						RTMP_Log( RTMP_LOGDEBUG, "no pullers for stream %s", tmpConnIter->app.c_str( ) );
					}
					else
					{
						auto& pullers = iter->second;
						pullers.erase( find_if( pullers.begin( ), 
												pullers.end( ), [&] (auto& puller ) { 
							if ( tmpConnIter->acceptTime == puller.acceptTime )
							{
								RTMP_Log( RTMP_LOGDEBUG, "clean one puller for stream %s", tmpConnIter->app.c_str( ) );
								return true;
							}
							return false;
						} ) );
					}
				}
				tmpConnIter = server->conns.erase( tmpConnIter );
			}
			else
				++tmpConnIter;
		}
		lock.unlock( );
#ifdef _DEBUG
		LOCK_TIME_END
#endif // _DEBUG
		Sleep( 1000 );
	}
	RTMP_Log(RTMP_LOGDEBUG,  "cleaner thread is quit." );
	return true;
}

int thread_func_for_controller( void *arg )
{
	RTMP_Log( RTMP_LOGDEBUG, "controller thread is start..." );
	STREAMING_SERVER* server = ( STREAMING_SERVER* ) arg;
	std::string choice;
	while ( server->state == STREAMING_START )
	{
		std::cin >> choice;
		if ( choice == "quit" || choice == "q" || choice == "exit" )
		{
			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "Exiting" );
			stopStreaming( server );
		}
		else if ( choice == "status" || choice == "s" )
		{
			auto& pullers = server->pullers;
			auto& pushers = server->pushers;
			int totalPullers = 0;
			int totalPushers = 0;
			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "====== Online Pullers ======" );
			std::unique_lock<std::mutex> lock( server->mux );
			for ( auto puller = pullers.begin( );
				  puller != pullers.end( );
				  ++puller )
			{
				std::string app = puller->first;
				int num = puller->second.size( );
				totalPullers += num;
				RTMP_LogAndPrintf( RTMP_LOGDEBUG, "app[%s] people[%d], ", app.c_str( ), num );
			}
			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "total pullers: %d", totalPullers );

			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "====== Online Pushers ======" );
			for ( auto pusher = pushers.begin( );
				  pusher != pushers.end( );
				  ++pusher )
			{
				std::string app = pusher->first;
				++totalPushers;
				RTMP_LogAndPrintf( RTMP_LOGDEBUG, "app[%s], ", app.c_str( ));
			}
			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "total pushers: %d", totalPushers );
		}
		else
		{
			RTMP_LogAndPrintf( RTMP_LOGDEBUG, "Unknown command \'%s\', ignoring", choice.c_str() );
		}
	}
	RTMP_Log( RTMP_LOGDEBUG, "controller thread is quit." );
	return true;
}
int main()
{
	init_sockets( );
	STREAMING_SERVER* server = new STREAMING_SERVER;
	server->state = STREAMING_START;
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
	std::thread receiver( thread_func_for_receiver, server);
	std::thread sender( thread_func_for_sender, server);
	std::thread cleaner( thread_func_for_cleaner, server );

	controller.join( );
	accepter.join( );
	receiver.join( );
	sender.join( );
	cleaner.join( );
#ifdef _DEBUG
	RTMP_LogThreadStop( );
#endif // _DEBUG
	Sleep( 10 );
	
	if (server )
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
