/*
 *
 * Cantops tcpServer.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "tcpServer.h"

#include <unistd.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include "captureBin.h"

#ifdef SEGFAULT_DEBUG
#include <signal.h>

void segfault_sigaction(int signal, siginfo_t *si, void *arg)
{
	CTCPServer* instance = CTCPServer::getInstance() ;
	__LOG(LOG_CRIT, "[SYS][%s:%d] cautght segault at address %p\n", _FILE_, __LINE__, si->si_addr);
	exit(0);
}
#endif

void* thread_waitingSend(void* pData)
{
	CTCPServer* instance = CTCPServer::getInstance() ;
	__LOG(LOG_NOTICE, "[TCP][%s:%d] thread start", _FILE_, __LINE__);
	if(instance->waitingSend() < 0) instance->m_flagDestroy = 1;

	return NULL ;
}

void* thread_waitingConnect(void* pData)
{
	CTCPServer* instance = CTCPServer::getInstance() ;
	//__E(LOG_LEVEL_TRA, "Connect thread start\n");
	__LOG(LOG_NOTICE, "[TCP][%s:%d] thread start", _FILE_, __LINE__);
	if(instance->waitingConnect(pData) < 0) instance->m_flagDestroy = 1;

	return NULL ;
}

CTCPServer* CTCPServer::getInstance()
{
	static CTCPServer instance ;
	return &instance ;
}

int CTCPServer::setMaxFD(int newFD, int maxFD)
{
	maxFD = (newFD > maxFD) ? newFD : maxFD ;

	return maxFD ;
}

int CTCPServer::init(ThreadArgs* args)
{
	int ret ;
	int option = 1 ;

	m_flagDestroy = 0 ;

	m_serverSocket = -1 ;
	m_clientSocket = -1 ;

	m_fdMax = -1 ;
	e_fdMax = -1 ;

#ifdef SENDQUEUE_ENABLE
	sendBuf.inptr = 0;
	sendBuf.outptr = 0;
	memset(sendBuf.cmd, 0, QUEUE_SIZE);
#endif

#ifdef SEGFAULT_DEBUG
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = segfault_sigaction;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sa, NULL);
#endif

	//unsigned short serverPort = TCP_TEST_PORT ;
	sockaddr_in serverAddr ;

	m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) ;

	if(m_serverSocket < 0) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] cannot create socket", _FILE_, __LINE__) ;
		//m_flagDestroy = 1;
		return m_serverSocket;
	}

	memset(&serverAddr, 0x0, sizeof(sockaddr_in)) ;

	serverAddr.sin_family 		= AF_INET ;
	serverAddr.sin_addr.s_addr 	= htonl(INADDR_ANY) ;
	//serverAddr.sin_addr.s_addr = inet_addr("192.168.1.28");
	//serverAddr.sin_addr.s_addr = inet_addr("100.100.100.101");
	//if(ordConf.ip_addr != NULL) serverAddr.sin_addr.s_addr = inet_addr(ordConf.ip_addr);

	__LOG(LOG_NOTICE, "[TCP][%s:%d] ip : %s, port : %d, socket : %d", _FILE_, __LINE__, inet_ntoa(serverAddr.sin_addr), cmdArg.tcp_port, m_serverSocket);
	serverAddr.sin_port = htons(cmdArg.tcp_port) ;

	setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) ;

	ret = bind(m_serverSocket, (struct sockaddr*)&serverAddr, sizeof(sockaddr_in));
	if(ret < 0 ) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] Server bind failed", _FILE_, __LINE__) ;
		//m_flagDestroy = 1;
		return ret;
	}

	ret = listen(m_serverSocket, MAXPENDING);
	if(ret < 0 ) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] Server listen failed", _FILE_, __LINE__) ;
		//m_flagDestroy = 1;
		return ret;
	}

	// create pipes. The pipe will be used to wake up blocked select().
	//pipe(m_pipe) ;

	FD_ZERO(&m_fds) ;
	FD_ZERO(&e_fds) ;
	FD_SET(m_serverSocket, &m_fds) ;
	//FD_SET(m_pipe[0], &m_fds) ;

	m_fdMax = setMaxFD(m_serverSocket, m_fdMax) ;
	//setMaxFD(m_pipe[0]) ;

	ret = pthread_create(&m_threadConnect, NULL, &thread_waitingConnect, args);
	if(ret < 0) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		return ret;
	}

	ret = pthread_create(&m_threadSend, NULL, &thread_waitingSend, NULL);
	if(ret < 0) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		return ret;
	}

	return ret;
}

int CTCPServer::destroy()
{
	int ret ;
	m_flagDestroy = 1 ;

	__LOG(LOG_EMERG, "[SYS][%s:%d] call server destroy", _FILE_, __LINE__) ;

	// away server thread.
	//write(m_pipe[1], &ret, 1) ;

	void* nStatus ;
	ret = pthread_join(m_threadConnect, &nStatus);
	if(ret < 0)
		__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);

	ret = pthread_join(m_threadSend, &nStatus);
	if(ret < 0)
		__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);

	//close(m_pipe[0]) ;
	//close(m_pipe[1]) ;

	if(m_serverSocket >= 0) {
		ret = close(m_serverSocket);
		if(ret < 0)
			__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		//__E(LOG_LEVEL_TRA, "ret1 : %d\n", ret) ;
	}

	if(m_clientSocket >= 0) {
		ret = close(m_clientSocket);
		if(ret < 0)
			__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		//__E(LOG_LEVEL_TRA, "ret2 : %d\n", ret) ;
	}

	//m_flagDestroy = 0 ;

	exit(0);

	return 1 ;
}

int CTCPServer::sendDataTCP(int fd, char* data, int len)
{
	int ret = 0;
	//__LOG(LOG_NOTICE, "[TCP][%s:%d] fd(%d) m_clientSocket(%d) m_serverSocket(%d)", _FILE_, __LINE__, fd, m_clientSocket, m_serverSocket);
	//fd = m_clientSocket;

	if(fd < 0) {
		__LOG(LOG_ERR, "[TCP][%s:%d] fd(%d) is not accept", _FILE_, __LINE__, fd);
		return fd;
	}
	
	ret = send(fd, data, len, MSG_DONTWAIT);
	if(ret < 0) {
		perror("tcpsnd fail");
		__LOG(LOG_CRIT,"[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		return ret;
	} else {
		__LOG(LOG_INFO, "[TCP][%s:%d] send data socket_id(%d) byte %d", _FILE_, __LINE__, fd, len);

		for (int i = 0; i < len; i++)
			__LOG(LOG_DEBUG, "[TCP][%s:%d] (%d)0x%02x", _FILE_, __LINE__, i, data[i]);
	}

	return ret;
}

int CTCPServer::waitingSend()
{
	while(1)
	{
		usleep(1000);

		if(m_flagDestroy)
			break ;

		if(cap_step == 2)
		{
			sendDataTCP(m_clientSocket, (char *)frameData.data, frameData.size);
			cap_step = 0;
		}
	}

	return 0;
}

int CTCPServer::waitingConnect(void* pData)
{
#define BUF_SIZE	256
	char szBuf[BUF_SIZE] ;

	int fd ;
	int ret = 0;

	unsigned int clientLen ;

	fd_set 	checkFds ;

	int nread ;

	const unsigned char SERVER_RECEIVES_CONNECTION_REQUEST 	= 1 ;
	const unsigned char SERVER_RECEIVES_DATA 		= 2 ;
	const unsigned char SERVER_CLOSES_CLIENT_CONNECTION 	= 3 ;
	unsigned char flagStatus = 0 ;

	sockaddr_in clientAddr ;

	while(1)
	{
		usleep(10000) ;

		if(m_flagDestroy)
			break ;

		checkFds = m_fds ;

		ret = select(m_fdMax + 1, &checkFds, 0, 0, NULL);
		if(ret < 0) {
			__LOG(LOG_CRIT, "[TCP][%s:%d] selecet ret:%d", _FILE_, __LINE__, ret);
			continue;
		}


		for(fd = 0; fd <= m_fdMax ; fd++)
		{
			if(!FD_ISSET(fd, &checkFds))
				continue ;

			//if(FD_ISSET(m_pipe[0], &checkFds))
				//break ;

			if(fd == m_serverSocket)
			{
				flagStatus = SERVER_RECEIVES_CONNECTION_REQUEST ;
			}
			else
			{
				ret = ioctl(fd, FIONREAD, &nread);
				if(ret < 0)
					__LOG(LOG_CRIT, "[TCP][%s:%d] ioctl ret:%d", _FILE_, __LINE__, ret);

				if(nread == 0) 
					flagStatus = SERVER_CLOSES_CLIENT_CONNECTION ;
				else
					flagStatus = SERVER_RECEIVES_DATA ;
			}
			
			switch(flagStatus)
			{
			case SERVER_RECEIVES_CONNECTION_REQUEST :
				__LOG(LOG_NOTICE, "[TCP][%s:%d] server receive connection request fd %d", _FILE_, __LINE__, fd) ;
				clientLen = sizeof(clientAddr);
				m_clientSocket = accept(fd, (struct sockaddr*)&clientAddr, (socklen_t*)&clientLen) ;

				if(m_clientSocket < 0) {
					ret = m_clientSocket;
					__LOG(LOG_CRIT, "[TCP][%s:%d] accept ret:%d", _FILE_, __LINE__, ret);
					break;
				}
 /*
				//debug_printf("%s\n",inet_ntoa(clientAddr.sin_addr));
				if (strcmp(inet_ntoa(clientAddr.sin_addr), get_str_from_json(JSON_FILE, "VSD", "ip_oht")) == 0)
					fd_oht = m_clientSocket;
				if (strcmp(inet_ntoa(clientAddr.sin_addr), get_str_from_json(JSON_FILE, "VSD", "ip_pc")) == 0)
					fd_pc = m_clientSocket;
*/
				__LOG(LOG_NOTICE, "[TCP][%s:%d] after accept & m_clientSocket : %d", _FILE_, __LINE__, m_clientSocket) ;
				__LOG(LOG_ALERT, "[TCP][%s:%d] client ip addr : %s", _FILE_, __LINE__, inet_ntoa(clientAddr.sin_addr));

				if(vhl_cnt >= 1) {
					__LOG(LOG_ALERT, "[TCP][%s:%d] vhl connect fail (vhl_cnt:%d vhl_max:%d)", _FILE_, __LINE__, vhl_cnt, 1);
					close(m_clientSocket);
					break;
        		}
				vhl_cnt++;

				FD_SET(m_clientSocket, &m_fds) ;
				m_fdMax = setMaxFD(m_clientSocket, m_fdMax) ;
				FD_SET(m_clientSocket, &e_fds) ;
				e_fdMax = setMaxFD(m_clientSocket, e_fdMax) ;
				break ;

			case SERVER_RECEIVES_DATA :
				//sendBuf.fd[sendBuf.inptr] = fd;
				nread = recv(fd, szBuf, BUF_SIZE, 0) ;

				if (nread > 0) 
				{
					__LOG(LOG_INFO, "[TCP][%s:%d] recv data socket_id(%d) byte %d", _FILE_, __LINE__, fd, nread);
					for (int i = 0; i < nread; i++)
						__LOG(LOG_DEBUG, "[TCP][%s:%d] (%d)0x%02x", _FILE_, __LINE__, i, szBuf[i]);

					ret = parseRecvData(fd, szBuf, nread, pData);
					// send(fd_pc, szBuf, nread, 0);
					// debug_printf("pc send\n");
				}
				else
					__LOG(LOG_ERR, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);

				break ;
			case SERVER_CLOSES_CLIENT_CONNECTION :
				FD_CLR(fd, &m_fds);
				if(fd == m_fdMax) m_fdMax--;
				FD_CLR(fd, &e_fds) ;
				if(fd == e_fdMax) e_fdMax--;
                if(vhl_cnt)vhl_cnt--;
				ret = close(fd);
				if(ret < 0)
					__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
					
				__LOG(LOG_ALERT, "[TCP][%s:%d] server close FD by client (ip : %s, fd : %d)", _FILE_, __LINE__, inet_ntoa(clientAddr.sin_addr), fd) ;
				break ;
			}
		}
	}

	__LOG(LOG_NOTICE, "[TCP][%s:%d] connect thread end", _FILE_, __LINE__);

	return ret;
}

int CTCPServer::parseRecvData(int fd, char* data, int len, void* pData)
{
	int ret = 0;
	GstState state;
	
    ThreadArgs *thraedArgs = (ThreadArgs *)pData;
    CaptureBin *captureBin = (CaptureBin *)(thraedArgs->arg0);

	if(data[0] == 0x30)
	{
    	gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
		if(state == GST_STATE_PLAYING) 
		{
			//cap_step = 1;
			for (guint i = 0; i < MAX_CHANNEL; i++)
				if (captureBin[i].getBinSinkPad()) captureBin[i].startCapture(2);
			//__LOG(LOG_NOTICE, "[TCP][%s:%d] cap_step : 1", _FILE_, __LINE__);
		}
	}
	
    return ret;
}

#ifdef SENDQUEUE_ENABLE
int CTCPServer::sendData()
{
	//char szBuf[128] ;
	int len = 0;
	int ret = 0;
	//_OHTDATA ohtdata;
	//void* nStatus;

	_MSGQueue msgBuf;
	int msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

	if (msg_id == -1)
	{
		perror("msgget fail");
		return -1;
	}
	
	msgBuf.type = PMSG_TYPE_1;
	ohtdata.fmt.machineType = htons(machineType_BLACKBOX);
	//debug_printf("send data\n");

	switch (sendBuf.cmd[sendBuf.outptr])
	{
	case RES_OVERLAY:
		__E(LOG_LEVEL_DBG, "RES_OVERLAY\n");
		ohtdata.fmt.cmd = htons(CMD_STATUSINFO_BLACKBOX);
		//ohtdata.fmt.curTime = get_sys_time();
		len = 55;
		memcpy(msgBuf.data.byte, ohtdata.byte, len);
		if (msgsnd(msg_id, &msgBuf, len*sizeof(char), IPC_NOWAIT) < 0) {
			perror("msgsnd fail");
		}
		__E(LOG_LEVEL_DBG, "Send Data msg_id(%d) byte  %d\n", msg_id, len);
		break;
	case RES_RTC_SET:
		__E(LOG_LEVEL_DBG, "RES_RTC_SET\n");
		ohtdata.fmt.cmd = htons(CMD_TIMESETTING_BLACKBOX_RESPONSE);
		//ohtdata.fmt.curTime = get_sys_time();
		len = 26;
		ret = send(sendBuf.fd[sendBuf.outptr], ohtdata.byte, len, 0);
		__E(LOG_LEVEL_DBG, "Send Data socket_id(%d) byte  %d\n", sendBuf.fd[sendBuf.outptr], len);
		break;
	case RES_EVT_COPY:
		__E(LOG_LEVEL_DBG, "RES_EVT_COPY\n");
		ohtdata.fmt.cmd = htons(CMD_EVENTACK_BLACKBOX);
		ohtdata.fmt.eventType = EVT_COPY;
		ohtdata.fmt.eventResult = EVT_RESULT_SUCCESS;
		len = 12;
		ret = send(sendBuf.fd[sendBuf.outptr], ohtdata.byte, len, 0);
		__E(LOG_LEVEL_DBG, "Send Data socket_id(%d) byte  %d\n", sendBuf.fd[sendBuf.outptr], len);
		break;
	case RES_EVT_PRI:
		__E(LOG_LEVEL_DBG, "RES_EVT_PRI\n");
		ohtdata.fmt.cmd = htons(CMD_EVENTACK_BLACKBOX);
		ohtdata.fmt.eventType = EVT_PRI;
		ohtdata.fmt.eventResult = EVT_RESULT_SUCCESS;
		len = 12;
		memcpy(msgBuf.data.byte, ohtdata.byte, len);
		//ret = send(sendBuf.fd[sendBuf.outptr], ohtdata.byte, len, 0);
		if (msgsnd(msg_id, &msgBuf, len*sizeof(char), IPC_NOWAIT) < 0) {
			perror("msgsnd fail");
		}
		__E(LOG_LEVEL_DBG, "Send Data msg_id(%d) byte  %d\n", msg_id, len);
		break;
	case RES_ERROR:
		__E(LOG_LEVEL_DBG, "RES_ERROR\n");
		ohtdata.fmt.cmd = htons(CMD_ERROR_BLACKBOX);
		//ohtdata.fmt.Error = ERROR_NULL;
		//ohtdata.fmt.Reserved = 0;
		len = 12;
		ret = send(sendBuf.fd[sendBuf.outptr], ohtdata.byte, len, 0);
		__E(LOG_LEVEL_DBG, "Send Data socket_id(%d) byte  %d\n", sendBuf.fd[sendBuf.outptr], len);
		break;
	default:
		__E(LOG_LEVEL_CRI, "CMD_ERROR : not defined\n");
		return 0;
		break;
	}

	for (int i = 0; i < len; i++)
		__E(LOG_LEVEL_MSG, "%02x", ohtdata.byte[i]);
	__E(LOG_LEVEL_MSG, "\n");
	//debug_printf("%s\n", ohtdata.byte);
	
	//pthread_join(m_threadConnect, &nStatus) ;
	sendBuf.outptr++;
	return ret;
}
#endif
