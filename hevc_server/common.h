/********************************************************************
	日期:	2016/12/14 11:06:52
	文件名:	common.h
	作者:	BrianYi
	
	用途:	公共头文件，用于定义常量枚举等等全局使用
*********************************************************************/
#pragma once

#include <winsock2.h>
#include <intsafe.h>
#include <stdio.h>

const UINT32 RECV_BUF_SIZE = 10 * 1024u;
const UINT32 SEND_BUF_SIZE = 10 * 1024u;
const INT32 MAX_CONNECTION_NUM = 32;

enum IOType
{
    Blocking    = 0,
    NonBlocking = 1
};

#define SEND_FAILED ((int)-1)
#define RECV_FAILED ((int)-1)

// stdafx.h文件
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif