/*
 * Copyright (C) 2020 BrianYi, All rights reserved
 */

#pragma once

#include <stdint.h>
#include "TCP.h"
#include "Log.h"

 //============================================================================
 //
 // global related
 //
inline long long get_current_milli( )
{
	return std::chrono::duration_cast< std::chrono::milliseconds >
		( std::chrono::system_clock::now( ).time_since_epoch( ) ).count( );
}

// packet info
#define KEEP_TRACK_PACKET		1
// packet hex info
#define KEEP_TRACK_PACKET_HEX	0
// only debug mode use, to caculate time wasting
#define TIME_CACULATE			0

#if KEEP_TRACK_PACKET
#define KEEP_TRACK_PACKET_SND
#define KEEP_TRACK_PACKET_RCV
#endif

#if KEEP_TRACK_PACKET_HEX
#define KEEP_TRACK_PACKET_SND_HEX
#define KEEP_TRACK_PACKET_RCV_HEX
#endif


//============================================================================
//
// Packet related
//
#define MAX_BODY_SIZE 1400
#define MAX_PACKET_SIZE (MAX_BODY_SIZE+sizeof HEADER)

#define BODY_SIZE(MP,size,seq)	(MP?MAX_BODY_SIZE:size - seq)
#define BODY_SIZE_H(header)		BODY_SIZE(header.MP,header.size,header.seq)
#define PACK_SIZE(MP,size,seq)	(MP?MAX_PACKET_SIZE:(sizeof HEADER+BODY_SIZE(MP,size,seq)))
#define PACK_SIZE_H(header)		PACK_SIZE(header.MP,header.size,header.seq)
#define NUM_PACK(size)			((size + MAX_BODY_SIZE - 1) / MAX_BODY_SIZE)
#define LAST_PACK_SEQ(size)		((size / MAX_BODY_SIZE) * MAX_BODY_SIZE)
#define INVALID_TYPE(header)	(header.type < 0 || header.type >= TypeNum)
#define INVALID_SEQ(header)		(header.seq % MAX_BODY_SIZE)
#define INVALID_SIZE(header)	(PACK_SIZE_H(header)>MAX_PACKET_SIZE || PACK_SIZE_H(header)<sizeof HEADER)
#define INVALID_PACK(header)	(INVALID_TYPE(header)||INVALID_SEQ(header)||INVALID_SIZE(header))

enum
{
	CreateStream,
	Play,
	Push,
	Pull,
	Ack,
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
	char app[ 64 ];		// app
};
#pragma pack()

struct PACKET
{
	HEADER header;
	char body[ MAX_BODY_SIZE ];
};

inline int send_pkt( TCP&, const int32_t&, const int32_t&, const int32_t&, const int32_t&,
					 const int32_t&, const int64_t&, const char*, const char *,
					 IOType, const time_t& );
inline int send_packet( TCP&, PACKET&, IOType, time_t );
inline PACKET* alloc_packet( const int32_t&, const int32_t&, const int32_t&, const int32_t&,
							 const int32_t&, const int64_t&,
							 const char*, const char * );

inline int send_createStream_packet( TCP& conn, const int64_t& timestamp, const char* app, const int32_t& timebase, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
{
	return send_pkt( conn, 0, CreateStream, timebase, 0, 0, timestamp, app, nullptr, iotype, timeout_ms );
}

inline int send_play_packet( TCP& conn, const int64_t& timestamp, const char* app, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
{
	return send_pkt( conn, 0, Play, 0, 0, 0, timestamp, app, nullptr, iotype, timeout_ms );
}

inline int send_ack_packet( TCP& conn, const int64_t& timestamp, const char* app, const int32_t& timebase, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
{
	return send_pkt( conn, 0, Ack, timebase, 0, 0, timestamp, app, nullptr, iotype, timeout_ms );
}

// inline int send_alive_packet( TCP& conn, const int64_t& timestamp, const char* app, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
// {
// 	return send_pkt( conn, 0, Alive, 0, 0, 0, timestamp, app, nullptr, iotype, timeout_ms );
// }

inline int send_fin_packet( TCP& conn, const int64_t& timestamp, const char* app, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
{
	return send_pkt( conn, 0, Fin, 0, 0, 0, timestamp, app, nullptr, iotype, timeout_ms );
}

inline int send_err_packet( TCP& conn, const int64_t& timestamp, const char* app, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
{
	return send_pkt( conn, 0, Err, 0, 0, 0, timestamp, app, nullptr, iotype, timeout_ms );
}

inline int send_push_packet( TCP& conn, PACKET& pkt, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
{
	return send_packet( conn, pkt, iotype, timeout_ms );
}

inline int send_pull_packet( TCP& conn, PACKET& pkt, IOType iotype = Blocking, const time_t& timeout_ms = 1000 )
{
	return send_packet( conn, pkt, iotype, timeout_ms );
}

inline PACKET* alloc_createStream_packet( const int64_t& timestamp, const char* app, const int32_t& timebase )
{
	return alloc_packet( 0, CreateStream, timebase, 0, 0, timestamp, app, nullptr );
}

inline PACKET* alloc_play_packet( const int64_t& timestamp, const char* app )
{
	return alloc_packet( 0, Play, 0, 0, 0, timestamp, app, nullptr );
}

inline PACKET* alloc_ack_packet( const int64_t& timestamp, const char* app )
{
	return alloc_packet( 0, Ack, 0, 0, 0, timestamp, app, nullptr );
}

// inline PACKET* alloc_alive_packet( const int64_t& timestamp, const char* app )
// {
// 	return alloc_packet( 0, Alive, 0, 0, 0, timestamp, app, nullptr );
// }

inline PACKET* alloc_err_packet( const int64_t& timestamp, const char* app )
{
	return alloc_packet( 0, Err, 0, 0, 0, timestamp, app, nullptr );
}

inline PACKET* alloc_fin_packet( const int64_t& timestamp, const char* app )
{
	return alloc_packet( 0, Fin, 0, 0, 0, timestamp, app, nullptr );
}

inline PACKET* alloc_push_packet( const int32_t& size, const int32_t& MP, const int32_t& seq, const int64_t& timestamp, const char* app, const char* body )
{
	return alloc_packet( size, Push, 0, MP, seq, timestamp, app, body );
}

inline PACKET* alloc_pull_packet( const int32_t& size, const int32_t& MP, const int32_t& seq, const int64_t& timestamp, const char* app, const char* body )
{
	return alloc_packet( size, Pull, 0, MP, seq, timestamp, app, body );
}

inline PACKET* alloc_packet( const int32_t& size, const int32_t& type, const int32_t& reserved, const int32_t& MP,
							 const int32_t& seq, const int64_t& timestamp,
							 const char* app, const char *body )
{
	int32_t bodySize = BODY_SIZE( MP, size, seq );
	PACKET* pkt = ( PACKET* ) malloc( sizeof PACKET );
	//zero_packet( *pkt );
	pkt->header.size = size;			// packet size
	pkt->header.type = type;			// setup(0),push(1),pull(2),ack(3),err(4)
	pkt->header.reserved = reserved;		// default 0, setup:timebase=1000/fps
	pkt->header.MP = MP;
	pkt->header.seq = seq;			// sequence number
	pkt->header.timestamp = timestamp;		// send time
	strncpy( pkt->header.app, app, sizeof pkt->header.app );
	if ( bodySize > 0 )
	{
		memcpy( pkt->body, body, bodySize );
	}
	return pkt;
}

inline void free_packet( PACKET* ptrPkt )
{
	if ( ptrPkt )
		free( ptrPkt );
}


inline int send_pkt( TCP& conn, const int32_t& size, const int32_t& type, const int32_t& reserved, const int32_t& MP,
					 const int32_t& seq, const int64_t& timestamp, const char* app, const char *body,
					 IOType iotype, const time_t& timeout_ms )
{
	int bodySize = BODY_SIZE( MP, size, seq );
	int packSize = PACK_SIZE( MP, size, seq );

#ifdef KEEP_TRACK_PACKET_SND
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
#endif

	PACKET pkt;
	pkt.header.size = htonl( size );
	pkt.header.type = htonl( type );
	pkt.header.reserved = htonl( reserved );
	pkt.header.MP = htonl( MP );
	pkt.header.seq = htonl( seq );
	pkt.header.timestamp = htonll( timestamp );
	strncpy( pkt.header.app, app, sizeof pkt.header.app );
	if ( bodySize > 0 )
		memcpy( pkt.body, body, bodySize );
	int sendSize = conn.send( ( char * ) &pkt, MAX_PACKET_SIZE, iotype, timeout_ms );

	if ( sendSize > 0 )
	{
#ifdef KEEP_TRACK_PACKET_SND_HEX
		RTMP_LogHexStr( RTMP_LOGDEBUG, ( uint8_t * ) &pkt, packSize );
#endif
	}
	else
	{
		RTMP_Log( RTMP_LOGDEBUG, "send failed with error: %d\n", WSAGetLastError( ) );
	}

	return sendSize;
}

inline int send_packet( TCP& conn, PACKET& pkt, IOType iotype = Blocking, time_t timeout_ms = 1000 )
{
	return send_pkt( conn, pkt.header.size, pkt.header.type,
					 pkt.header.reserved, pkt.header.MP, pkt.header.seq,
					 pkt.header.timestamp, pkt.header.app, pkt.body, iotype, timeout_ms );
}

inline int recv_packet( TCP& conn, PACKET& pkt, IOType iotype = Blocking, time_t timeout_ms = 1000 )
{
	int recvSize = conn.receive( ( char * ) &pkt, MAX_PACKET_SIZE, iotype, timeout_ms );
	if ( recvSize <= 0 )
	{
		RTMP_Log( RTMP_LOGDEBUG, "recv failed with error: %d\n", WSAGetLastError( ) );
		return recvSize;
	}

	int32_t bodySize = BODY_SIZE( ntohl( pkt.header.MP ),
								  ntohl( pkt.header.size ),
								  ntohl( pkt.header.seq ) );
	int32_t packSize = PACK_SIZE( ntohl( pkt.header.MP ),
								  ntohl( pkt.header.size ),
								  ntohl( pkt.header.seq ) );
#ifdef KEEP_TRACK_PACKET_RCV
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
#endif
#ifdef KEEP_TRACK_PACKET_RCV_HEX
	RTMP_LogHexStr( RTMP_LOGDEBUG, ( uint8_t * ) &pkt, packSize );
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


//============================================================================
//
// statistic info
//


enum
{
	TypeUnknown,
	TypePusher,
	TypePuller,
};

struct StatisticInfo
{
	int64_t recvPackets;
	int64_t recvBytes;
	int64_t sendPackets;
	int64_t sendBytes;
	int64_t recvByteRate;
	int64_t sendByteRate;
	int64_t recvPacketRate;
	int64_t sendPacketRate;
	int64_t every10sRecvBytes;
	int64_t every10sSendBytes;
	int64_t every10sRecvPackets;
	int64_t every10sSendPackets;
	int64_t beginTime;
	//std::mutex mux;
};

#define TYPE_STR(type) (type==TypePusher?"Pusher":(type==TypePuller?"Puller":"Unknown"))
#define KB(bytes) (bytes/1024.0)
#define MB(bytes) (KB(bytes)/1024.0)
#define GB(bytes) (MB(bytes)/1024.0)
enum
{
	StatRecv,
	StatSend
};

inline void caculate_statistc( StatisticInfo& stat, PACKET& pkt, int recvOrSend )
{
	if ( !( pkt.header.type == Push || pkt.header.type == Pull ) )
		return;

	//std::unique_lock<std::mutex> lock( stat.mux );
	if ( stat.beginTime == 0 )
		stat.beginTime = time( 0 );

	switch ( recvOrSend )
	{
	case StatRecv:
		stat.recvBytes += BODY_SIZE_H( pkt.header );
		stat.recvPackets++;
		stat.every10sRecvBytes += BODY_SIZE_H( pkt.header );
		stat.every10sRecvPackets++;
		break;
	case StatSend:
		stat.sendBytes += BODY_SIZE_H( pkt.header );
		stat.sendPackets++;
		stat.every10sSendBytes += BODY_SIZE_H( pkt.header );
		stat.every10sSendPackets++;
		break;
	default:
		return;
	}

	time_t currentTime = time( 0 );
	int64_t duration = currentTime - stat.beginTime;
	if ( duration >= 10 )
	{
		stat.recvByteRate = stat.every10sRecvBytes / duration;
		stat.recvPacketRate = stat.every10sRecvPackets / duration;
		stat.sendByteRate = stat.every10sSendBytes / duration;
		stat.sendPacketRate = stat.every10sSendPackets / duration;
		stat.every10sRecvBytes = 0;
		stat.every10sRecvPackets = 0;
		stat.every10sSendBytes = 0;
		stat.every10sSendPackets = 0;
		stat.beginTime = currentTime;
	}
}

//============================================================================
//
// tools
//

#if TIME_CACULATE
#define TIME_BEG(x) \
int64_t _begTime##x = get_current_milli( ); \
int32_t _begLine##x =__LINE__;

#define TIME_END(x) \
int64_t _endTime##x = get_current_milli( ); \
int64_t _diff##x = _endTime##x - _begTime##x; \
int64_t _endLine##x=__LINE__; \
RTMP_Log( RTMP_LOGDEBUG, "runing time is %lldms, %s:[%d %d]", \
		  _diff##x, __FUNCTION__, _begLine##x, _endLine##x );
#else
#define TIME_BEG(x)
#define TIME_END(x)
#endif // _DEBUG