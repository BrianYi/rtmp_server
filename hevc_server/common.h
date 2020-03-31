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

const UINT32 RECV_BUF_SIZE = /*10 * 1024u;*/1444;
const UINT32 SEND_BUF_SIZE = /*10 * 1024u;*/1444;
const INT32 MAX_CONNECTION_NUM = 32;

enum IOType
{
    Blocking    = 0,
    NonBlocking = 1
};