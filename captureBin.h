#ifndef _CAPTRUEBIN_H_
#define _CAPTUREBIN_H_

#include "util.h"
#include <turbojpeg.h>
#include <deque>
#include <mutex>
#include <memory>
#include <atomic>

#define CAP_LOG_KEY "CAP"
#define TURBO_JPEG

// Forward declaration for async capture task
typedef struct _AsyncCaptureTask AsyncCaptureTask;

typedef struct {
    void* arg0;
    void* arg1;
} CapArgs;

typedef enum
{
    CAP_ENC_JPEG = 0,
    CAP_ENC_TURBO =1,
    CAP_ENC_RAW =2
} CapEncType;

// Completion callback type
typedef void (*CaptureCompleteCallback)(guint8 ch, gint completed_count, gpointer user_data);

typedef struct _CaptureRequest {
    gint maxCnt;
    gchar *filePath;
    gpointer userData;
    guint8 mode;
    gint captureCnt; // Written count (atomic)
    gint64 startTime;
    gint timeoutMs;
    std::atomic<bool> responseSent;  // Atomic for thread-safe callback prevention

    _CaptureRequest() : maxCnt(0), filePath(NULL), userData(NULL), mode(0),
                        captureCnt(0), startTime(0), timeoutMs(0), responseSent(false) {}
    ~_CaptureRequest() { if(filePath) g_free(filePath); }
} CaptureRequest;

typedef struct _CaptureData
{
    GstElement *appsrc;
    guint8 ch;
    gint captureCnt;
    gint captureCnt_;
    gint captureMaxCnt;
    GstBuffer *buf;
    guint8 mode;
    gint fps;
    gboolean debug;
    tjhandle tjCompressor;
    CapEncType enc_type;
    gchar *filePath; 
    const gchar *extention;
    gint quality;
    // Async capture support
    GAsyncQueue *task_queue;
    GThread *worker_thread;
    volatile gboolean worker_running;
    volatile gint push_index;  // Index for pushed frames (incremented at push time)
    // Completion callback
    CaptureCompleteCallback complete_callback;
    gpointer callback_user_data;
    
    // Request Queue
    std::deque<std::shared_ptr<CaptureRequest>> *request_queue;
    std::shared_ptr<CaptureRequest> current_request;
    std::mutex *queue_mutex;
    GstElement *valve;
} CaptureData;

typedef struct _CaptureElement
{
    GstElement *enc;
    GstElement *queue;
    GstElement *queue2;
    GstElement *imx_convert;
    GstElement *bin;
    GstElement *appsink;
    GstElement *crop;
    GstElement *overlay;
    GstElement *capsfilter;
    GstElement *queue_sink;
    GstElement *appsrc;
    GstElement *valve;  // Valve for zero-CPU buffer dropping when not capturing
}
CaptureElement;

class CaptureBin
{
public :
	static CaptureBin* getInstance();
    CaptureBin();
    ~CaptureBin();
	gboolean init(guint8 num);
    gint setFilePath(const gchar *prefix); 
    gint startCapture(gint maxCnt);
    gint stopCapture();
    void addCaptureRequest(gint maxCnt, const gchar *prefix, gpointer userData, guint8 mode, gint timeoutMs);
    void setMode(guint8 mode);
    guint8 getMode();
    guint8 getFPS();
    gint getCaptureCnt();
    gint getCaptureCnt_();
    const gchar* getCaptureFilePath();
    GstPad* getBinSinkPad();
    void setQueueSize(guint size);
    GstStateChangeReturn setState(GstState state);
    GstState getState();
    gboolean addBinToPipe(GstElement *pipe);
    gboolean removeBinToPipe(GstElement *pipe);
    void setTimeStampDebug();
    void setAppsrc(GstElement *appsrc);
    void setMaxCnt(guint16 maxCnt);
    void startWorker();
    void stopWorker();
    gboolean isQueueEmpty();
    void setCompleteCallback(CaptureCompleteCallback callback, gpointer user_data);
    gboolean checkTimeout();

private :
    void processNextRequest();

public :
	gboolean m_flagDestroy;
    //GstElement *pipeline[2];
	CaptureElement be;
    gboolean add_cap_f = 0;
    gulong probe_id;
    GstPad *queue_src_pad;
    //CaptureData captureData;

private :
    GstPad *sinkPad;
    CaptureData captureData;
    guint timeout_source_id;
};

#endif