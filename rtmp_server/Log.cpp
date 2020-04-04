/*
 *  Copyright (C) 2008-2009 Andrej Stepanchuk
 *  Copyright (C) 2009-2010 Howard Chu
 *
 *  This file is part of librtmp.
 *
 *  librtmp is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1,
 *  or (at your option) any later version.
 *
 *  librtmp is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with librtmp see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 *  http://www.gnu.org/copyleft/lgpl.html
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <queue>
#include <mutex>

 //#include "rtmp_sys.h"
#include "Log.h"
#include <process.h>

#define MAX_PRINT_LEN	2048

RTMP_LogLevel RTMP_debuglevel = RTMP_LOGERROR;

static int neednl;

static FILE *fmsg;

static RTMP_LogCallback rtmp_log_default, *cb = rtmp_log_default;


static std::queue<LOG> logQue;
static std::mutex mux;
static bool startThread = false;
static HANDLE threadId = INVALID_HANDLE_VALUE;

static const char *levels[ ] = {
  "CRIT", "ERROR", "WARNING", "INFO",
  "DEBUG", "DEBUG2"
};

static void rtmp_log_default( int level, const char *format, va_list vl, int isPrint )
{
	if ( !startThread ) return;
	char str[ MAX_PRINT_LEN ] = "";
	const char *levels[ ] = {
	  "CRIT", "ERROR", "WARNING", "INFO",
	  "DEBUG", "DEBUG2"
	};
	vsnprintf( str, MAX_PRINT_LEN - 1, format, vl );

	/* Filter out 'no-name' */
	if ( RTMP_debuglevel < RTMP_LOGALL && strstr( str, "no-name" ) != NULL )
		return;

	if ( level <= RTMP_debuglevel )
	{
		LOG log;
		log.data = ( char * ) malloc( MAX_PRINT_LEN );
		log.dataSize = sprintf( log.data, "%s: %s", levels[ level ], str );
		if ( isPrint )
			printf( "%s\n", log.data );
		std::unique_lock<std::mutex> lock( mux );
		logQue.push( log );
	}
}

static unsigned int CALLBACK thread_func_for_logger( void * )
{
	while ( startThread )
	{
		if ( logQue.empty( ) )
		{
			Sleep( 10 );
			continue;
		}
		std::unique_lock<std::mutex> lock( mux );
		while ( !logQue.empty( ) )
		{
			LOG log = logQue.front( );
			logQue.pop( );
			fprintf( fmsg, "%s\n", log.data );
			free( log.data );
		}
#ifdef _DEBUG
		fflush( fmsg );
#endif
		lock.unlock( );
		Sleep( 10 );
	}
	return true;
}

void RTMP_LogSetOutput( FILE *file )
{
	fmsg = file;
}


void RTMP_LogThreadStart( )
{
	startThread = true;
	threadId = ( HANDLE ) _beginthreadex( nullptr, 0, thread_func_for_logger, 0, 0, nullptr );
}

void RTMP_LogThreadStop( )
{
	startThread = false;
}

void RTMP_LogSetLevel( RTMP_LogLevel level )
{
	RTMP_debuglevel = level;
}

void RTMP_LogSetCallback( RTMP_LogCallback *cbp )
{
	cb = cbp;
}

RTMP_LogLevel RTMP_LogGetLevel( )
{
	return RTMP_debuglevel;
}

void RTMP_Log( int level, const char *format, ... )
{
	if ( !startThread ) return;
	va_list args;

	if ( level > RTMP_debuglevel )
		return;

	va_start( args, format );
	cb( level, format, args, false );
	va_end( args );
}

void RTMP_LogAndPrintf( int level, const char *format, ... )
{
	if ( !startThread ) return;
	va_list args;

	if ( level > RTMP_debuglevel )
		return;

	va_start( args, format );
	cb( level, format, args, true );
	va_end( args );
}

static const char hexdig[ ] = "0123456789abcdef";

void RTMP_LogHex( int level, const uint8_t *data, unsigned long len )
{
#ifdef _DEBUG
#ifdef _NOHEXSTR
	return;
#endif
#endif // _DEBUG
	if ( !startThread ) return;
	unsigned long i;
	char line[ 50 ], *ptr;

	if ( level > RTMP_debuglevel )
		return;

	ptr = line;

	for ( i = 0; i < len; i++ )
	{
		*ptr++ = hexdig[ 0x0f & ( data[ i ] >> 4 ) ];
		*ptr++ = hexdig[ 0x0f & data[ i ] ];
		if ( ( i & 0x0f ) == 0x0f )
		{
			*ptr = '\0';
			ptr = line;
			RTMP_Log( level, "%s", line );
		}
		else
		{
			*ptr++ = ' ';
		}
	}
	if ( i & 0x0f )
	{
		*ptr = '\0';
		RTMP_Log( level, "%s", line );
	}
}

void RTMP_LogHexStr( int level, const uint8_t *data, unsigned long len )
{
#ifdef _DEBUG
#ifdef _NOHEXSTR
	return;
#endif
#endif // _DEBUG

	if ( !startThread ) return;
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

/* These should only be used by apps, never by the library itself */
void RTMP_LogPrintf( const char *format, ... )
{
	if ( !startThread ) return;
	char str[ MAX_PRINT_LEN ] = "";
	int len;
	va_list args;
	va_start( args, format );
	len = vsnprintf( str, MAX_PRINT_LEN - 1, format, args );
	va_end( args );

	if ( RTMP_debuglevel == RTMP_LOGCRIT )
		return;

	if ( !fmsg ) fmsg = stderr;

	if ( neednl )
	{
		putc( '\n', fmsg );
		neednl = 0;
	}

	if ( len > MAX_PRINT_LEN - 1 )
		len = MAX_PRINT_LEN - 1;
	fprintf( fmsg, "%s", str );
	if ( str[ len - 1 ] == '\n' )
		fflush( fmsg );
}

void RTMP_LogStatus( const char *format, ... )
{
	if ( !startThread ) return;
	char str[ MAX_PRINT_LEN ] = "";
	va_list args;
	va_start( args, format );
	vsnprintf( str, MAX_PRINT_LEN - 1, format, args );
	va_end( args );

	if ( RTMP_debuglevel == RTMP_LOGCRIT )
		return;

	if ( !fmsg ) fmsg = stderr;

	fprintf( fmsg, "%s", str );
	fflush( fmsg );
	neednl = 1;
}
