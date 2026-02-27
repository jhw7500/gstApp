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
#include "parser.h"

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

CTCPServer::CTCPServer()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

	m_flagDestroy = 1;

	m_threadConnect = 0;
	m_threadSend = 0;

	m_serverSocket = -1;
	m_clientSocket = -1;

	m_fdMax = -1;
	e_fdMax = -1;
}

CTCPServer::~CTCPServer()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

CTCPServer* CTCPServer::getInstance()
{
	static CTCPServer instance ;
	return &instance ;
}

int CTCPServer::setMaxFD(int newFD, int maxFD)
{
	maxFD = (newFD > maxFD) ? newFD : maxFD;

	return maxFD;
}

int CTCPServer::init(ThreadArgs *args)
{
	int ret;
	int option = 1;

	m_flagDestroy = 0;

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
	sockaddr_in serverAddr;

	m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) ;

	if(m_serverSocket < 0) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] cannot create socket", _FILE_, __LINE__) ;
		//m_flagDestroy = 1;
		return m_serverSocket;
	}

	memset(&serverAddr, 0x0, sizeof(sockaddr_in));

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	//serverAddr.sin_addr.s_addr = inet_addr("192.168.1.28");
	//serverAddr.sin_addr.s_addr = inet_addr("100.100.100.101");
	//if(ordConf.ip_addr != NULL) serverAddr.sin_addr.s_addr = inet_addr(ordConf.ip_addr);

	__LOG(LOG_NOTICE, "[TCP][%s:%d] ip : %s, port : %d, socket : %d", _FILE_, __LINE__, inet_ntoa(serverAddr.sin_addr), cmdArg.tcp_port, m_serverSocket);
	serverAddr.sin_port = htons(cmdArg.tcp_port);

	setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

	ret = bind(m_serverSocket, (struct sockaddr *)&serverAddr, sizeof(sockaddr_in));
	if (ret < 0)
	{
		__LOG(LOG_CRIT, "[TCP][%s:%d] Server bind failed", _FILE_, __LINE__);
		close(m_serverSocket);
		m_serverSocket = -1;
		return ret;
	}

	ret = listen(m_serverSocket, MAXPENDING);
	if(ret < 0 ) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] Server listen failed", _FILE_, __LINE__) ;
		close(m_serverSocket);
		m_serverSocket = -1;
		return ret;
	}

	//create pipes. The pipe will be used to wake up blocked select().
	//pipe(m_pipe) ;

	FD_ZERO(&m_fds);
	FD_ZERO(&e_fds);
	FD_SET(m_serverSocket, &m_fds);
	//FD_SET(m_pipe[0], &m_fds);

	m_fdMax = setMaxFD(m_serverSocket, m_fdMax);
	//setMaxFD(m_pipe[0]);

	ret = pthread_create(&m_threadConnect, NULL, &thread_waitingConnect, args);
	if(ret < 0) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		close(m_serverSocket);
		m_serverSocket = -1;
		return ret;
	}

	ret = pthread_create(&m_threadSend, NULL, &thread_waitingSend, NULL);
	if(ret < 0) {
		__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		close(m_serverSocket);
		m_serverSocket = -1;
		return ret;
	}

	return ret;
}

int CTCPServer::destroy()
{
	int ret;
	m_flagDestroy = 1;

	__LOG(LOG_EMERG, "[SYS][%s:%d] tcp server destroy", _FILE_, __LINE__);

	// away server thread.
	//write(m_pipe[1], &ret, 1) ;

	void* nStatus;

	//close(m_pipe[0]) ;
	//close(m_pipe[1]) ;

	if(m_serverSocket >= 0) {
		ret = close(m_serverSocket);
		if(ret < 0) __LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
	}

	if(m_clientSocket >= 0) {
		ret = close(m_clientSocket);
		if(ret < 0) __LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
	}

	/* Skip pthread_join - thread will exit naturally via m_flagDestroy check */
	/* Timeout-based join would be better, but for now just close sockets */
	
#if 0
	if(m_threadConnect > 0) {
		ret = pthread_join(m_threadConnect, &nStatus);
		if(ret < 0) __LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);
	}
#endif

	return 0;
}

int CTCPServer::sendData(int fd, char* data, int len)
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
			sendData(m_clientSocket, (char *)frameData.data, frameData.size);
			cap_step = 0;
		}
	}

	__LOG(LOG_NOTICE, "[TCP][%s:%d] %s thread end", _FILE_, __LINE__, __FUNCTION__);

	return 0;
}

int CTCPServer::waitingConnect(void* pData)
{
#define BUF_SIZE	256
	char szBuf[BUF_SIZE];

	int fd;
	int ret = 0;

	unsigned int clientLen;

	fd_set checkFds;

	int nread;

	unsigned char flagStatus = 0;

	sockaddr_in clientAddr;

	while(1)
	{
		usleep(10000);

		if(m_flagDestroy)
			break;

		checkFds = m_fds;

		ret = select(m_fdMax + 1, &checkFds, 0, 0, NULL);
		if(ret < 0) {
			__LOG(LOG_CRIT, "[TCP][%s:%d] selecet ret:%d", _FILE_, __LINE__, ret);
			continue;
		}


		for(fd = 0; fd <= m_fdMax ; fd++)
		{
			if(!FD_ISSET(fd, &checkFds))
				continue;

			//if(FD_ISSET(m_pipe[0], &checkFds))
				//break ;

			if(fd == m_serverSocket)
			{
				flagStatus = SERVER_RECEIVES_CONNECTION_REQUEST;
			}
			else
			{
				ret = ioctl(fd, FIONREAD, &nread);
				if (ret < 0)
					__LOG(LOG_CRIT, "[TCP][%s:%d] ioctl ret:%d", _FILE_, __LINE__, ret);

				if (nread == 0)
					flagStatus = SERVER_CLOSES_CLIENT_CONNECTION;
				else
					flagStatus = SERVER_RECEIVES_DATA;
			}

			switch (flagStatus)
			{
				case SERVER_RECEIVES_CONNECTION_REQUEST:
				{
					__LOG(LOG_NOTICE, "[TCP][%s:%d] server receive connection request fd %d", _FILE_, __LINE__, fd);
					clientLen = sizeof(clientAddr);
					m_clientSocket = accept(fd, (struct sockaddr *)&clientAddr, (socklen_t *)&clientLen);

					if (m_clientSocket < 0)
					{
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

					__LOG(LOG_NOTICE, "[TCP][%s:%d] after accept & m_clientSocket : %d", _FILE_, __LINE__, m_clientSocket);
					__LOG(LOG_ALERT, "[TCP][%s:%d] client ip addr : %s", _FILE_, __LINE__, inet_ntoa(clientAddr.sin_addr));

					if (client_cnt >= CLIENT_MAX)
					{
						__LOG(LOG_ERR, "[TCP][%s:%d] client disconnect because client_cnt(%d) is oevr client_max(%d)", _FILE_, __LINE__, client_cnt, CLIENT_MAX);
						close(m_clientSocket);
						break;
					}
					client_cnt++;

					FD_SET(m_clientSocket, &m_fds);
					m_fdMax = setMaxFD(m_clientSocket, m_fdMax);
					FD_SET(m_clientSocket, &e_fds);
					e_fdMax = setMaxFD(m_clientSocket, e_fdMax);
					break;
				}

				case SERVER_RECEIVES_DATA:
				{
					//sendBuf.fd[sendBuf.inptr] = fd;
					nread = recv(fd, szBuf, BUF_SIZE - 1, 0);

					if (nread > 0) 
					{
						szBuf[nread] = '\0'; // Ensure null-termination
						__LOG(LOG_INFO, "[TCP][%s:%d] recv data socket_id(%d) byte %d", _FILE_, __LINE__, fd, nread);
						for (int i = 0; i < nread; i++)
							__LOG(LOG_DEBUG, "[TCP][%s:%d] (%d)0x%02x", _FILE_, __LINE__, i, szBuf[i]);

						ret = parseRecvData(fd, szBuf, nread, pData);
						// send(fd_pc, szBuf, nread, 0);
						// debug_printf("pc send\n");
					}
					else
						__LOG(LOG_ERR, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);

					break;
				}
				case SERVER_CLOSES_CLIENT_CONNECTION:
				{
					FD_CLR(fd, &m_fds);
					if (fd == m_fdMax)
						m_fdMax--;
					FD_CLR(fd, &e_fds);
					if (fd == e_fdMax)
						e_fdMax--;
					if (client_cnt)
						client_cnt--;
					ret = close(fd);
					if (ret < 0)
						__LOG(LOG_CRIT, "[TCP][%s:%d] ret:%d", _FILE_, __LINE__, ret);

					__LOG(LOG_ALERT, "[TCP][%s:%d] server close FD by client (ip : %s, fd : %d)", _FILE_, __LINE__, inet_ntoa(clientAddr.sin_addr), fd);
					break;
				}
			}
		}
	}

	__LOG(LOG_NOTICE, "[TCP][%s:%d] %s thread end", _FILE_, __LINE__, __FUNCTION__);

	return ret;
}

int CTCPServer::parseRecvData(int fd, char* data, int len, void* pData)
{
	int ret = -1;
    //ThreadArgs *thraedArgs = (ThreadArgs *)pData;

	ParserClass* parser = ParserClass::getInstance();
	ret = parser->cmd_parser(data, len, pData);
	//ret = parser->cfi_parser(data, len, pData);

    return ret;
}