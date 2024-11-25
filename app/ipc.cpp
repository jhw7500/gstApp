
#include "ipc.h"
#include "parser.h"

void* thread_waitingRecv(void* pData)
{
	CIPCInsance* instance = CIPCInsance::getInstance() ;

	__LOG(LOG_NOTICE, "[IPC][%s:%d] thread start", _FILE_, __LINE__);

	if(instance->waitingRecv(pData) < 0) instance->m_flagDestroy = 1;

	return NULL ;
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
	
#if 1
    int msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

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

    makeDir(cmdArg.captureDir);

    return ret;
}

int CIPCInsance::destory()
{
    int ret = 0;
    void* nStatus ;

	__LOG(LOG_EMERG, "[IPC][%s:%d] call server destroy", _FILE_, __LINE__) ;
	ret = pthread_join(m_threadRecv, &nStatus);
	if(ret < 0)
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);

    int msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

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
	exit(0);

    return ret;
}

int CIPCInsance::waitingRecv(void* pData)
{
    int ret;
    int i;
    int msg_id;
	RecvQueue recvMsg;

	msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

    if(msg_id == -1) {
		ret = -1;
        perror("msgget fail");
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
        return -1;
    }


    while(1) {

        g_usleep(10000);

        if(m_flagDestroy)
            break;

		//msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

		ret = msgrcv(msg_id, &recvMsg, sizeof(recvMsg) - sizeof(long), PMSG_TYPE_IPC, 0);
        if(ret <= 0) {
            perror("msgrcv fail");
			__LOG(LOG_ERR, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
            continue;
        }
        __LOG(LOG_NOTICE, "[IPC][%s:%d] recv data msg_id(%d) byte %d", _FILE_, __LINE__, msg_id, ret);

        ParserClass* parser = ParserClass::getInstance();
        ret = parser->cfi_parser(recvMsg.data, ret, pData);
		//ret = parseIpcRecvData(msg_id, recvMsg.data, ret);
        if(ret < 0) {
			__LOG(LOG_ERR, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
            continue;
        }

/*
        printf("IPC recv data len %d : ", ret);
        for(i=0;i<ret;i++)
            printf("%02x", msg.data[i]);
        printf("\n");
*/
        //ret = parseRecvData(msg.data, ret);
    };

	__LOG(LOG_NOTICE, "[TCP][%s:%d] ipc thread end", _FILE_, __LINE__);

    return ret;
}