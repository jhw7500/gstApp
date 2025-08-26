
#include "ipc.h"
#include "parser.h"

void* thread_waitingRecv(void* pData)
{
	CIPCInsance* instance = CIPCInsance::getInstance() ;

	__LOG(LOG_NOTICE, "[IPC][%s:%d] thread start", _FILE_, __LINE__);

	if(instance->waitingRecv(pData) < 0) instance->m_flagDestroy = 1;

	return NULL ;
}

CIPCInsance::CIPCInsance()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

	m_flagDestroy = 1;
    m_threadRecv = 0;
}

CIPCInsance::~CIPCInsance()
{
    __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
}

CIPCInsance* CIPCInsance::getInstance()
{
	static CIPCInsance instance ;

    return &instance ;
}

int CIPCInsance::init(ThreadArgs *args)
{
    int ret = 0;
	m_flagDestroy = 0;
    m_threadRecv = 0;
	
#if 1
    int msg_id = msgget((key_t)MSG_Q_REQ_KEY, IPC_CREAT | 0666);

    if(msg_id == -1) {
        perror("msgget fail");
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return msg_id;
    }
	ret = msgctl(msg_id, IPC_RMID, NULL);
    if(ret < 0) {
		 perror("msgctl fail");
		 __LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
	}
 #endif
	ret = pthread_create(&m_threadRecv, NULL, &thread_waitingRecv, args);
    if(ret < 0)
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);

    makeDir(cmdArg.cap.path);

    return ret;
}

int CIPCInsance::destroy()
{
    int ret = 0;
    void* nStatus ;
    m_flagDestroy = 1;

	__LOG(LOG_EMERG, "[IPC][%s:%d] call server destroy", _FILE_, __LINE__) ;
    if(m_threadRecv > 0)
    {
        ret = pthread_join(m_threadRecv, &nStatus);
        if(ret < 0)
            __LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
    }

#if 1
    int msg_id = msgget((key_t)MSG_Q_REQ_KEY, IPC_CREAT | 0666);

    if(msg_id == -1) {
        perror("msgget fail");
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return msg_id;
    }

	ret = msgctl(msg_id, IPC_RMID, NULL);
    if(ret < 0) {
		perror("msgctl fail");
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
	}
#endif

	exit(0);

    return ret;
}

int CIPCInsance::sendData(char* data, int len)
{
	int ret = 0;
	int msg_id = msgget((key_t)MSG_Q_RES_KEY, IPC_CREAT | 0666);
	
	if (msg_id == -1) {
		ret = -1;
		perror("msgget fail");
		__LOG(LOG_ERR, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		return ret;
	}

	IpcBuffer sendMsg;
	sendMsg.type = PMSG_TYPE_IPC_CFI;

	memcpy(sendMsg.data, data, len);
	ret = msgsnd(msg_id, &sendMsg, len, IPC_NOWAIT);

	if (ret < 0) {
		perror("msgsnd fail");
		__LOG(LOG_ERR, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
		return ret;
	} else {
		__LOG(LOG_INFO, "[IPC][%s:%d] send data msg_id(%d) byte  %d", _FILE_, __LINE__, msg_id, len);
	}

    if(cmdArg.log_level > LOG_INFO)
    {
        len = sizeof(sendMsg.data) / sizeof(sendMsg.data[0]);
        char buffer[256];
        convert_data_to_hex(sendMsg.data, len, buffer, sizeof(buffer));
        __LOG(LOG_DEBUG, "[IPC][%s:%d] data : %s", _FILE_, __LINE__, buffer);
    }

	return 0;
}

int CIPCInsance::waitingRecv(void* pData)
{
    int ret = 0;
    int msg_id;
	IpcBuffer recvMsg;
    ParserClass* parser = ParserClass::getInstance();

	//msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

    while(1) {
        g_usleep(10000);

        if(m_flagDestroy)
            break;

		msg_id = msgget((key_t)MSG_Q_REQ_KEY, IPC_CREAT | 0666);
        if(msg_id == -1) {
            perror("msgget fail");
            __LOG(LOG_CRIT, "[IPC][%s:%d] msg_id:%d", _FILE_, __LINE__, ret);
            g_usleep(990000);
            continue;
        }

        ret = msgrcv(msg_id, &recvMsg, sizeof(recvMsg) - sizeof(long), PMSG_TYPE_IPC_CFI, IPC_NOWAIT);
        if (ret <= 0) {
            if (errno == ENOMSG) {
                g_usleep(10000);
            } else {
                perror("msgrcv fail");
                __LOG(LOG_ERR, "[IPC][%s:%d] errno:%d, ret:%d", __FILE__, __LINE__, errno, ret);
                g_usleep(990000);
            }
            continue;
        }
        else
        {
            __LOG(LOG_INFO, "[IPC][%s:%d] recv data msg_id(%d) byte %d", _FILE_, __LINE__, msg_id, ret);
            if(cmdArg.log_level > LOG_INFO)
            {
                int len = sizeof(recvMsg.data) / sizeof(recvMsg.data[0]);
                char buffer[256];
                convert_data_to_hex(recvMsg.data, len, buffer, sizeof(buffer));
                __LOG(LOG_DEBUG, "[IPC][%s:%d] data : %s", _FILE_, __LINE__, buffer);
            }

            ret = parser->cfi_parser(recvMsg.data, ret, pData);
            //ret = parseIpcRecvData(msg_id, recvMsg.data, ret);
            if(ret < 0) {
                __LOG(LOG_ERR, "[IPC][%s:%d] capture parser err (ret:%d)", _FILE_, __LINE__, ret);
            }
        }
    };

	__LOG(LOG_NOTICE, "[TCP][%s:%d] ipc thread end", _FILE_, __LINE__);

    return ret;
}