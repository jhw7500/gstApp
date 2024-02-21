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

#define CLIENT_MAX  1
#define MAXPENDING  5

typedef enum {
  SERVER_NONE = 0,
	SERVER_RECEIVES_CONNECTION_REQUEST = 1,
	SERVER_RECEIVES_DATA = 2,
	SERVER_CLOSES_CLIENT_CONNECTION = 3
} ServerStatus;

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
  CTCPServer();
  ~CTCPServer();

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

private :
	pthread_t m_threadConnect;
  pthread_t m_threadSend;
  int m_serverSocket ;

	int m_fdMax ;
  int e_fdMax ;

	fd_set m_fds ;
  fd_set e_fds;
  unsigned char client_cnt;
};

#endif

