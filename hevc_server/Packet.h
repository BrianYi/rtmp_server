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

#define INVALID_PACK(type) (type < 0 || type >= TypeNum)

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
	char body[ MAX_BODY_SIZE ];
};

inline PACKET* alloc_packet( size_t size, int type, int reserved, int MP,
							 int32_t seq, int64_t timestamp,
							 const char* app, const char *body )
{
	size_t bodySize = BODY_SIZE( MP, size, seq );
	PACKET* pkt = ( PACKET* ) malloc( sizeof PACKET );
	//zero_packet( *pkt );
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
	int sendSize = conn.send( ( char * ) &pkt, MAX_PACKET_SIZE );
#ifdef _DEBUG
	if (sendSize > 0 )
	{
		if ( type != Alive )
			RTMP_LogHexStr( RTMP_LOGDEBUG, ( uint8_t * ) &pkt, packSize );
	}
	else
	{
		RTMP_Log(RTMP_LOGDEBUG, "send failed with error: %d\n", WSAGetLastError() );
	}
#endif // _DEBUG
	return sendSize;
}

inline int send_packet( TCP& conn, PACKET& pkt )
{
	return send_pkt( conn, pkt.header.size, pkt.header.type,
					 pkt.header.reserved, pkt.header.MP, pkt.header.seq,
					 pkt.header.timestamp, pkt.header.app, pkt.body );
}

inline int recv_packet( TCP& conn, PACKET& pkt, IOType inIOType = Blocking )
{
	int recvSize = conn.receive( ( char * ) &pkt, MAX_PACKET_SIZE, inIOType );
	if ( recvSize <= 0 )
	{
		RTMP_Log( RTMP_LOGDEBUG, "recv failed with error: %d\n", WSAGetLastError( ) );
		return recvSize;
	}

	size_t bodySize = BODY_SIZE( ntohl( pkt.header.MP ),
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

