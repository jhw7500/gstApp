
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
    int msg_id = msgget((key_t)MSG_Q_REQ_KEY, IPC_CREAT | 0600);

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

    // Ensure capture output directory exists
    if (cmdArg.cap.dir && cmdArg.cap.dir[0] == '/') {
        if (safe_mkdir_p(cmdArg.cap.dir, 0755) < 0) {
            __LOG(LOG_ERR, "[IPC][%s:%d] err mkdir: %s", _FILE_, __LINE__, cmdArg.cap.dir);
        }
    } else {
        gchar *cap_dir = g_strdup_printf("%s/%s", cmdArg.mntDir, cmdArg.cap.path);
        if (safe_mkdir_p(cap_dir, 0755) < 0) {
            __LOG(LOG_ERR, "[IPC][%s:%d] err mkdir: %s", _FILE_, __LINE__, cap_dir);
        }
        g_free(cap_dir);
    }

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
    int msg_id = msgget((key_t)MSG_Q_REQ_KEY, IPC_CREAT | 0600);

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

static guint16 rd_le16(const guint8 *p)
{
    return (guint16)p[0] | ((guint16 )p[1] << 8);
}

static void dump_cfi_header(const guint8 *buf, size_t len)
{
    if (len < 18) {
        printf("CFI hdr: too short (%zu)\n", len);
        return;
    }

    guint16 LEN   = rd_le16(buf + 0);
    guint16 VER   = rd_le16(buf + 2);

    char SID[7];
    memcpy(SID, buf + 4, 6);
    SID[6] = '\0';

    guint16 CMD   = rd_le16(buf + 10);
    guint16 TXID  = rd_le16(buf + 12);
    guint8  CH    = buf[14];
    guint8  RSV   = buf[15];
    guint16 REQ   = rd_le16(buf + 16);

    __LOG(LOG_NOTICE, "[CFG][%s:%d] CFI hdr: LEN=%u (0x%04x) VER=0x%04x SID='%s' CMD=0x%04x TXID=%u CH=0x%02x RSV=0x%02x REQ=%u", _FILE_, __LINE__, LEN, LEN, VER, SID, CMD, TXID, CH, RSV, REQ);
}

int CIPCInsance::sendData(char* data, int len)
{
    int msg_id = msgget((key_t)MSG_Q_RES_KEY, IPC_CREAT | 0600);
    if (msg_id == -1) {
        perror("msgget fail");
        __LOG(LOG_ERR, "[IPC][%s:%d] msgget fail", _FILE_, __LINE__);
        return -1;
    }

    IpcBuffer sendMsg;
    memset(&sendMsg, 0, sizeof(sendMsg));               // ✅ 잔여 쓰레기 제거
    sendMsg.type = PMSG_TYPE_IPC_CFI;

    if (!data || len < 0 || len > (int)sizeof(sendMsg.data)) {   // ✅ NULL 체크 + 오버플로 방지
        __LOG(LOG_ERR, "[IPC][%s:%d] invalid parameters: data=%p, len=%d (max=%zu)", _FILE_, __LINE__, data, len, sizeof(sendMsg.data));
        return -1;
    }

    memcpy(sendMsg.data, data, len);

    // ✅ 전송 전에 헤더 해석(원하면)
    //dump_cfi_header((const guint8 *)sendMsg.data, (size_t)len);

    if (cmdArg.log_level > LOG_INFO)
    {
        char buffer[256];
        convert_data_to_hex(sendMsg.data, len, buffer, sizeof(buffer)); // ✅ len만큼만
        __LOG(LOG_DEBUG, "[IPC][%s:%d] len=%d data=%s", _FILE_, __LINE__, len, buffer);
    }

    __LOG(LOG_INFO, "[IPC][%s:%d] send msg_id=%d byte=%d", _FILE_, __LINE__, msg_id, len);

    int ret = msgsnd(msg_id, &sendMsg, len, IPC_NOWAIT);
    if (ret < 0) {
        perror("msgsnd fail");
        __LOG(LOG_ERR, "[IPC][%s:%d] msgsnd fail", _FILE_, __LINE__);
        return -1;
    }


    return 0;
}

int CIPCInsance::waitingRecv(void* pData)
{
    int ret = 0;
    int msg_id;
	IpcBuffer recvMsg;
    ParserClass* parser = ParserClass::getInstance();

	//msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0600);

    while(1) {
        g_usleep(10000);

        if(m_flagDestroy)
            break;

		msg_id = msgget((key_t)MSG_Q_REQ_KEY, IPC_CREAT | 0600);
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
