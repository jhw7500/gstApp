/*
 *
 * Cantops tcpServer.h support
 *
 * Copyright (C)2023 cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#ifndef _TCPSERVER_H_
#define _TCPSERVER_H_

#include "util.h"

#define MAXPENDING  5

#pragma pack(push, 1)
typedef struct _FrameData
{
    gpointer data;
    unsigned long size;
} FrameData;
#pragma pack(pop)

class CTCPServer
{
public :
	static CTCPServer* getInstance() ;

	int init(ThreadArgs* args) ;
	int destroy() ;

	int sendData() ;
  int sendDataTCP(int fd, char* data, int len);

	int waitingConnect(void* pData) ;
  int waitingSend();

private :
	int setMaxFD(int newFD, int maxFD);
	int parseRecvData(int fd, char* data, int len, void* pData);

public :
  int m_flagDestroy ;
  int m_clientSocket ;
  unsigned char cap_step;
  FrameData frameData;
#ifdef SENDQUEUE_EANBLE
  _BUFQueue sendBuf;
#endif
private :
	pthread_t m_threadConnect;
  pthread_t m_threadSend;
  int m_serverSocket ;

	int m_fdMax ;
  int e_fdMax ;

	fd_set m_fds ;
  fd_set e_fds;
  unsigned char vhl_cnt;
};

#endif

