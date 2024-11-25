#ifndef _IPC_H_
#define _IPC_H_

#ifndef _UTIL_H_
#include "util.h"
#endif

#include <sys/ipc.h>
#include <sys/msg.h>

#define MSG_Q_KEY (0x65)

enum MSG_TYPE {
  PMSG_TYPE_UNUSED = 0,
  PMSG_TYPE_IPC,
  PMSG_TYPE_OVERLAY,
  PMSG_TYPE_OSS
};

struct RecvQueue
{
    long type;
    char data[64];
};

class CIPCInsance
{
public :
	static CIPCInsance* getInstance() ;

	int init(ThreadArgs *args) ;
	int destroy() ;

	int waitingRecv(void* pData) ;
    int destory();
    
private :

public :
    int m_flagDestroy;

private :
    pthread_t m_threadRecv ;
};

#endif