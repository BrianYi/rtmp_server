// hevc_server.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <thread>
#include <vector>
#include <list>
#include <time.h>
#include <unordered_map>
#include <mutex>
#include "TCP.h"
#include "UDP.h"
#include "librtmp/log.h"
#include <unordered_set>
#include <utility>
#include <chrono>
#include <queue>

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"rtmp/librtmp.lib")

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#define TIMEOUT 3000
#define SERVER_PORT 5566

#define MAX_BODY_SIZE 1400
#define MAX_PACKET_SIZE (MAX_BODY_SIZE+sizeof HEADER)

#define BODY_SIZE(MP,size,seq)	(MP?MAX_BODY_SIZE:size - seq)
#define BODY_SIZE_H(header)		BODY_SIZE(header.MP,header.size,header.seq)
#define PACK_SIZE(MP,size,seq)	(MP?MAX_PACKET_SIZE:(sizeof HEADER+BODY_SIZE(MP,size,seq)))
#define PACK_SIZE_H(header)		PACK_SIZE(header.MP,header.size,header.seq)
#define INVALID_PACK(type) (type < 0 || type >= TypeNum)

#ifdef _DEBUG
#define LOCK_TIME_BEG \
int64_t _lockTime = get_current_milli( );

#define LOCK_TIME_END \
int64_t _unlockTime = get_current_milli( ); \
int64_t _diff = _unlockTime - _lockTime; \
RTMP_Log( RTMP_LOGDEBUG, "lock time is %lldms, %s:%d", \
		  _diff, __FUNCTION__, __LINE__ );
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
	CreateStream,
	Play,
	Push,
	Pull,
	Ack,
	Alive,
	Fin,
	Err,
	TypeNum
};

// 4+4+4+8+8+16=44
#pragma pack(1)
struct HEADER
{
	// total body size
	int32_t size;
	int32_t type;			// setup(0),push(1),pull(2),ack(3),err(4)
	// 
	// default 0 
	// setup: timebase=1000/fps
	// push,pull: more fragment
	// 
	int32_t reserved;
	int32_t MP;				// more packet?
	int32_t seq;			// sequence number
	int64_t timestamp;		// send time
	char app[ 16 ];		// app
};
#pragma pack()

struct PACKET
{
	HEADER header;
	char body[MAX_BODY_SIZE];
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

auto cmp = [ ] ( PACKET* ptrPktL, PACKET* ptrPktR ) { return ptrPktL->header.timestamp > ptrPktR->header.timestamp; };
typedef std::priority_queue<PACKET*, std::vector<PACKET*>, decltype(cmp)> StreamData;
StreamData streamData( cmp );
// struct StreamInfo
// {
// 	std::string app;
// 	int timebase;
// 	StreamData* ptrStreamData;
// };

struct STREAMING_SERVER
{
	TCP conn;
	std::list<ConnectionInfo> conns;									// 所有连接
	std::unordered_map<std::string, std::list<ConnectionInfo>> pullers;	// puller连接
	std::unordered_map<std::string, ConnectionInfo> pushers;			// pusher连接
//	std::unordered_map<std::string, StreamInfo> streams;	// streams
	int state;
	std::mutex mux;
};

constexpr long long get_current_milli( )
{
	return std::chrono::duration_cast< std::chrono::milliseconds >
		( std::chrono::system_clock::now( ).time_since_epoch( ) ).count( );
}

#define MAX_LOG_SIZE 2048
struct LOG
{
	size_t dataSize;
	char *data;
};
std::queue<LOG> logQue;
FILE *dumpfile;
std::mutex mux;

static void logCallback( int level, const char *format, va_list vl )
{
	char str[ MAX_LOG_SIZE ] = "";
	const char *levels[ ] = {
	  "CRIT", "ERROR", "WARNING", "INFO",
	  "DEBUG", "DEBUG2"
	};
	vsnprintf( str, MAX_LOG_SIZE - 1, format, vl );

	/* Filter out 'no-name' */
	if ( RTMP_debuglevel < RTMP_LOGALL && strstr( str, "no-name" ) != NULL )
		return;

	if ( level <= RTMP_debuglevel )
	{
		LOG log;
		log.data = ( char * ) malloc( MAX_LOG_SIZE );
		log.dataSize = sprintf( log.data, "%s: %s", levels[ level ], str );
		std::unique_lock<std::mutex> lock( mux );
		logQue.push( log );
		//fprintf( fmsg, "%s: %s\n", levels[ level ], str );
	}
}

void RTMP_LogHexStr( int level, const uint8_t *data, unsigned long len )
{
#define BP_OFFSET 9
#define BP_GRAPH 60
#define BP_LEN	80
	static const char hexdig[ ] = "0123456789abcdef";
	char	line[ BP_LEN ];
	unsigned long i;
	const char *levels[ ] = {
	  "CRIT", "ERROR", "WARNING", "INFO",
	  "DEBUG", "DEBUG2"
	};
	if ( !data || level > RTMP_debuglevel )
		return;

	/* in case len is zero */
	line[ 0 ] = '\0';
	std::string tmpStr = "";
	for ( i = 0; i < len; i++ )
	{
		int n = i % 16;
		unsigned off;

		if ( !n )
		{
			if ( i )
			{
				tmpStr = tmpStr + levels[ level ] + ": " + line + '\n';
			}
			memset( line, ' ', sizeof( line ) - 2 );
			line[ sizeof( line ) - 2 ] = '\0';

			off = i % 0x0ffffU;

			line[ 2 ] = hexdig[ 0x0f & ( off >> 12 ) ];
			line[ 3 ] = hexdig[ 0x0f & ( off >> 8 ) ];
			line[ 4 ] = hexdig[ 0x0f & ( off >> 4 ) ];
			line[ 5 ] = hexdig[ 0x0f & off ];
			line[ 6 ] = ':';
		}

		off = BP_OFFSET + n * 3 + ( ( n >= 8 ) ? 1 : 0 );
		line[ off ] = hexdig[ 0x0f & ( data[ i ] >> 4 ) ];
		line[ off + 1 ] = hexdig[ 0x0f & data[ i ] ];

		off = BP_GRAPH + n + ( ( n >= 8 ) ? 1 : 0 );

		if ( isprint( data[ i ] ) )
		{
			line[ BP_GRAPH + n ] = data[ i ];
		}
		else
		{
			line[ BP_GRAPH + n ] = '.';
		}
	}
	tmpStr = tmpStr + levels[ level ] + ": " + line;

	LOG log;
	log.data = ( char* ) malloc( tmpStr.size( ) + 1 );
	log.dataSize = sprintf( log.data, "%s", tmpStr.c_str( ) );
	std::unique_lock<std::mutex> lock( mux );
	logQue.push( log );
}


inline void zero_packet( PACKET& pkt )
{
	memset( &pkt, 0, sizeof PACKET );
}

inline void free_packet( PACKET* ptrPkt )
{
	if ( ptrPkt )
		free( ptrPkt );
}


inline int send_pkt( TCP& conn, size_t size, int type, int reserved, int MP,
					 int32_t seq, int64_t timestamp, const char* app, const char *body )
{
	int bodySize = BODY_SIZE( MP, size, seq );
	int packSize = PACK_SIZE( MP, size, seq );
#ifdef _DEBUG
	if ( type != Alive )
	{
		int64_t sendTimestamp = get_current_milli( );
		RTMP_Log( RTMP_LOGDEBUG, "send packet(%d) to %s:%u, %dB:[%d,%d-%d], packet timestamp=%lld, send timestamp=%lld, S-P=%lld",
				  type,
				  conn.getIP( ).c_str( ),
				  conn.getPort( ),
				  MAX_PACKET_SIZE,
				  size,
				  seq,
				  seq + bodySize,
				  timestamp,
				  sendTimestamp,
				  sendTimestamp - timestamp );
	}
#endif // _DEBUG

	PACKET pkt;
	pkt.header.size = htonl( size );
	pkt.header.type = htonl( type );
	pkt.header.reserved = htonl( reserved );
	pkt.header.MP = htonl( MP );
	pkt.header.seq = htonl( seq );
	pkt.header.timestamp = htonll( timestamp );
	strcpy( pkt.header.app, app );
	if ( bodySize > 0 )
		memcpy( pkt.body, body, bodySize );
#ifdef _DEBUG
	if (type != Alive )
		RTMP_LogHexStr( RTMP_LOGDEBUG, ( uint8_t * ) &pkt, packSize );
#endif // _DEBUG
	return conn.send( ( char * ) &pkt, MAX_PACKET_SIZE );
}


int recv_packet( TCP& conn, PACKET& pkt, IOType inIOType = Blocking )
{
	int recvSize = conn.receive( (char *)&pkt, MAX_PACKET_SIZE, inIOType );
	if ( recvSize <= 0 )
	{
		//RTMP_Log(RTMP_LOGDEBUG, "recv size is <= 0 %d %s", __LINE__, __FUNCTION__ );
		return recvSize;
	}

	size_t bodySize = BODY_SIZE( ntohl(pkt.header.MP),
								 ntohl( pkt.header.size ),
								 ntohl( pkt.header.seq ) );
	size_t packSize = PACK_SIZE( ntohl( pkt.header.MP ),
								 ntohl( pkt.header.size ),
								 ntohl( pkt.header.seq ) );
#ifdef _DEBUG
	if ( ntohl( pkt.header.type ) != Alive )
	{
		int64_t recvTimestamp = get_current_milli( );
		RTMP_Log( RTMP_LOGDEBUG, "recv packet(%d) from %s:%u, %dB:[%d,%d-%d], packet timestamp=%lld, recv timestamp=%lld, R-P=%lld",
				  ntohl( pkt.header.type ),
				  conn.getIP( ).c_str( ),
				  conn.getPort( ),
				  MAX_PACKET_SIZE,
				  ntohl( pkt.header.size ),
				  ntohl( pkt.header.seq ),
				  ntohl( pkt.header.seq ) + bodySize,
				  ntohll( pkt.header.timestamp ),
				  recvTimestamp,
				  recvTimestamp - ntohll( pkt.header.timestamp ) );
		RTMP_LogHexStr( RTMP_LOGDEBUG, ( uint8_t * ) &pkt, packSize );
	}
#endif // _DEBUG

	// to host byte
	pkt.header.size = ntohl( pkt.header.size );
	pkt.header.type = ntohl( pkt.header.type );			// setup(0),push(1),pull(2),ack(3),err(4)
	pkt.header.reserved = ntohl( pkt.header.reserved );		// default 0, setup:fps
	pkt.header.MP = htonl( pkt.header.MP );
	pkt.header.seq = ntohl( pkt.header.seq );			// sequence number
	pkt.header.timestamp = ntohll( pkt.header.timestamp );
	return recvSize;
}

#define send_createStream_packet(conn, timestamp, app, timebase) \
	send_pkt( conn, 0, CreateStream, timebase,  0, 0, timestamp, app, nullptr )
#define send_play_packet(conn, timestamp, app ) \
	send_pkt( conn, 0, Play, 0, 0, 0, timestamp, app, nullptr )
#define send_ack_packet(conn, timestamp, app, timebase ) \
	send_pkt( conn, 0, Ack, timebase, 0, 0, timestamp, app, nullptr )
#define send_alive_packet(conn, timestamp, app ) \
	send_pkt( conn, 0, Alive, 0, 0, 0, timestamp, app, nullptr )
#define send_fin_packet(conn, timestamp, app ) \
	send_pkt( conn, 0, Fin, 0, 0, 0, timestamp, app, nullptr )
#define send_err_packet(conn, timestamp, app ) \
	send_pkt( conn, 0, Err, 0, 0, 0, timestamp, app, nullptr )

#define send_push_packet(conn, pkt) \
	send_packet( conn, pkt )
#define send_pull_packet(conn, pkt) \
	send_packet( conn, pkt )

#define alloc_createStream_packet(timestamp, app, timebase) \
	alloc_packet(0, CreateStream, timebase, 0, 0, timestamp, app, nullptr )
#define alloc_play_packet(timestamp, app ) \
	alloc_packet( 0, Play, 0,  0, 0, timestamp, app, nullptr )
#define alloc_ack_packet(timestamp, app ) \
	alloc_packet( 0, Ack, 0,  0, 0, timestamp, app, nullptr )
#define alloc_alive_packet(timestamp, app ) \
	alloc_packet( 0, Alive, 0,  0, 0, timestamp, app, nullptr )
#define alloc_err_packet(timestamp, app ) \
	alloc_packet( 0, Err, 0, 0, 0, timestamp, app, nullptr )
#define alloc_fin_packet(timestamp, app) \
	alloc_packet(0, Fin, 0, 0, 0, timestamp, app, nullptr)

#define alloc_push_packet(size, MP, seq, timestamp, app, body ) \
	alloc_packet(size, Push, 0, MP, seq, timestamp, app, body )
#define alloc_pull_packet(size, MP, seq, timestamp, app, body ) \
	alloc_packet(size, Pull, 0, MP, seq,  timestamp, app, body )

inline PACKET* alloc_packet( size_t size, int type, int reserved, int MP,
							 int32_t seq, int64_t timestamp,
							 const char* app, const char *body )
{
	size_t bodySize = BODY_SIZE( MP, size, seq );
	PACKET* pkt = ( PACKET* ) malloc( sizeof PACKET );
	zero_packet( *pkt );
	pkt->header.size = size;			// packet size
	pkt->header.type = type;			// setup(0),push(1),pull(2),ack(3),err(4)
	pkt->header.reserved = reserved;		// default 0, setup:timebase=1000/fps
	pkt->header.MP = MP;
	pkt->header.seq = seq;			// sequence number
	pkt->header.timestamp = timestamp;		// send time
	strcpy( pkt->header.app, app );
	if ( bodySize > 0 )
	{
		memcpy( pkt->body, body, bodySize );
	}
	return pkt;
}

inline int send_packet( TCP& conn, PACKET& pkt )
{
	return send_pkt( conn, pkt.header.size, pkt.header.type,
					 pkt.header.reserved, pkt.header.MP, pkt.header.seq,
					 pkt.header.timestamp, pkt.header.app, pkt.body );
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


int thread_func_for_logger( void *arg )
{
	RTMP_Log( RTMP_LOGDEBUG, "logger thread is start..." );
	STREAMING_SERVER *server = ( STREAMING_SERVER * ) arg;

	while ( server->state == STREAMING_START )
	{
		if ( logQue.empty( ) )
		{
			Sleep( 10 );
			continue;
		}
		std::unique_lock<std::mutex> lock( mux );
		LOG log = logQue.front( );
		logQue.pop( );
		lock.unlock( );

		fprintf( dumpfile, "%s\n", log.data );
#ifdef _DEBUG
		fflush( dumpfile );
#endif
		free( log.data );
		Sleep( 10 );
	}

	RTMP_Log( RTMP_LOGDEBUG, "logger thread is quit." );
	return true;
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
		TCP conn = server->conn.accept_client( Blocking );
		conn.set_socket_rcvbuf_size( MAX_PACKET_SIZE );
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
		// add connections to link list
		std::unique_lock<std::mutex> lock( server->mux );
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
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
	while ( server->state == STREAMING_START )
	{
		int recvSize = 0;
		std::unique_lock<std::mutex> lock( server->mux );
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
		for ( auto connIter = server->conns.begin( );
			  connIter != server->conns.end( );
			  ++connIter )
		{
			// receive packet
			PACKET pkt;
			if ( recv_packet( connIter->conn, pkt, NonBlocking ) <= 0 ) // no packet, continue loop next
				continue;

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
				int64_t nextTimestamp = pkt.header.timestamp + server->pushers[ connIter->app ].timebase;
				PACKET* ptrPkt = alloc_pull_packet( pkt.header.size,
													  pkt.header.MP,
													  pkt.header.seq,
													  nextTimestamp,
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
		std::unique_lock<std::mutex> lock( server->mux );
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
		if ( streamData.empty( ) )
		{
			Sleep( 10 );
			continue;
		}
		PACKET* ptrPkt = streamData.top( );
		streamData.pop( );
		if ( server->pullers.find( ptrPkt->header.app ) == server->pullers.end() )
		{
#ifdef _DEBUG
			RTMP_Log( RTMP_LOGDEBUG, "no pullers for stream %s", ptrPkt->header.app );
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
			send_packet( puller->conn, *ptrPkt );
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
		std::unique_lock<std::mutex> lock( server->mux );
#ifdef _DEBUG
		LOCK_TIME_BEG
#endif // _DEBUG
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
					if ( server->pullers.find( tmpConnIter->app ) == server->pullers.end( ) )
					{
						RTMP_Log( RTMP_LOGDEBUG, "no pullers for stream %s", tmpConnIter->app.c_str( ) );
					}
					else
					{
						auto& pullers = server->pullers[ tmpConnIter->app ];
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
	char ich;
	while ( server->state == STREAMING_START )
	{
		ich = getchar( );
		switch ( ich )
		{
		case 'q':
			RTMP_Log( RTMP_LOGDEBUG, "Exiting" );
			stopStreaming( server );
			break;
		default:
			RTMP_Log( RTMP_LOGDEBUG, "Unknown command \'%c\', ignoring", ich );
		}
	}
	RTMP_Log( RTMP_LOGDEBUG, "controller thread is quit." );
	return true;
}
int main()
{
#ifdef _DEBUG
	dumpfile = fopen( "hevc_server.dump", "a+" );
	RTMP_LogSetOutput( dumpfile );
	RTMP_LogSetCallback( logCallback );
	RTMP_LogSetLevel( RTMP_LOGALL );

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
	init_sockets( );
	STREAMING_SERVER* server = new STREAMING_SERVER;
	server->state = STREAMING_START;
	server->conn.listen_on_port( SERVER_PORT );

	std::thread logger( thread_func_for_logger, server );
	std::thread accepter( thread_func_for_accepter, server );
	std::thread receiver( thread_func_for_receiver, server);
	std::thread sender( thread_func_for_sender, server);
	std::thread cleaner( thread_func_for_cleaner, server );
	//std::thread controller( thread_func_for_controller, server );

	logger.join( );
	accepter.join( );
	receiver.join( );
	sender.join( );
	cleaner.join( );
	
	if (server )
		delete server;
#ifdef _DEBUG
	if ( dumpfile )
		fclose( dumpfile );
#endif
	cleanup_sockets( );
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
