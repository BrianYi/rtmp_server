/********************************************************************
	����:	2016/12/14 11:06:52
	�ļ���:	common.h
	����:	BrianYi
	
	��;:	����ͷ�ļ������ڶ��峣��ö�ٵȵ�ȫ��ʹ��
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