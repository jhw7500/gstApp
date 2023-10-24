#include <gst/gst.h>
#include <stdio.h>
#include <syslog.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/check/gstcheck.h>

#define AUDIO_ENABLEx
#define RECORD_ENABLE
#define RTSP_ENABLEx
#define CAPTURE_ENABLEx
#define RECORD_SPLITMUXSINK_ENABLE
#define FILENAME_SEC_ZERO
#define SPLIT_TIMER_ENABLE
#define TERMINAL_CMD_ENABLE
#define RTSP_AUTH_ENABLEx
#define OVERLAY_ENABLEx
#define TIMEOVERLAYx
#define IPC_ENABLE
#define FHD_ENABLE

#ifdef FHD_ENABLE
#define _WIDTH   1920
#define _HEIGHT  1080
#else
#define _WIDTH   1280
#define _HEIGHT  720
#endif

#define LEAKY_NONE          0
#define LEAKY_UPSTREAM      1
#define LEAKY_DOWNSTREAM    2
#define MAIN_FPS 30
#define FILE_FPS 30
#define RTSP_FPS 15
#define DEFAULT_FPS     2147483647
#define HIGH_BITRATE    FILE_BITRATE
#define LOW_BITRATE     RTSP_BITRATE
#define FILE_BITRATE    4096
#define RTSP_BITRATE    1024
#define QUEUE_MAX_SIZE_BUFFER   60
#define QUEUE_MAX_SIZE_TIME     10*GST_SECOND
#define QUEUE_RTSP_LEAKY        LEAKY_DOWNSTREAM
#define QUEUE_RECORD_LEAKY      LEAKY_NONE

#define MAX_SRC 2
#define MAX_CAM 4
#define MAX_ENC 6

#define FILE_SAVE_DURATION 60
#define CAPTURE_MAX_CNT     5



#define JHW_TESTx
#ifdef JHW_TEST
#define FILE_PATH   ""
#else
#define FILE_PATH   "/mnt/sd_cam/"
#endif

void mylog( int opt, const char* _szfmt, ... );
#define __LOG(opt, fmt, args...) do { mylog(opt, (char*)fmt, ##args); } while(0)
#define _FILE_  strrchr(__FILE__,'/')? strrchr(__FILE__,'/')+1:__FILE__
#define PROGRAM_NAME	"mp4App"
#define RTSP_PORT       "8554"
#define APPSRC_NAME    "appsrc"

static gboolean change_file_datetime() ;
static gboolean captureStart(gpointer data, guint16 max_cnt);
static gboolean changeBitrate(gpointer data);
static gboolean changeFPS(gpointer data, guint16 setfps);
static gboolean visibleSRT(gboolean val, gpointer data);

//GstElement *pipeline = NULL;
//GstElement *pipeline[2];
GMainLoop  *gstLoop = NULL;
GstRTSPServer *rtspServer = NULL;
GstRTSPMountPoints *rtspMounts = NULL;
GstPad *video_sink_pad[4] = {NULL, NULL, NULL, NULL};
GstPad *audio_sink_pad[4] = {NULL, NULL, NULL, NULL};

//GstElement *splitmuxsink[4];
//GstBus *bus[2];

volatile sig_atomic_t is_interrupted = 0;
volatile sig_atomic_t sigflag = 0;
gchar *fileDateTime = NULL;
gchar *ohtName = NULL;
unsigned char log_level = 7;
unsigned char dbg_level = 6;

typedef enum
{
  FILE0_L =  0,
  FILE0_R =  1,
  FILE1_L =  2,
  FILE1_R =  3,
  RTSP0_L =  4,
  RTSP0_R =  5,
  RTSP1_L =  6,
  RTSP1_R =  7,
  CAPTURE0_L = 8,
  CAPTURE0_R = 9,
  CAPTURE1_L = 10,
  CAPTURE1_R = 11
} PipeNum;

typedef enum
{
  NONE =  0,
  RECORDING =  1,
  STREAMING =  2,
  CAPTURING =  3
} VideoMode;

typedef struct CustomData{
    gchar* file_name;
    guint8 index;
    guint8 ch;
    guint8 min;
    GstElement *appsrc;
    GstElement *appsink;
    GstElement *enc;
    GstElement *vr;
    GstElement *timeoveraly;
    GstBuffer *buf;
    guint16 captureCnt;
    guint16 captureMax;
    VideoMode mode;
    gboolean is_live;
    GstPad *bin_video_pads;
    GstPad *bin_audio_pads;
    gchar *client_ip;
    gboolean firstSplitFlag;
    GstElement *pipeline;
    GstElement *capsfilter;
    //GgstLoop *rtspLoop;
    //GThread *rtspThread;
    //pthread_t m_threadRtsp;
} CustomData;
//CustomData info[4];

typedef struct AudioPipe{
    GstElement *src;
    GstElement *convert;
    GstElement *audiorate;
    GstElement *resample;
    GstElement *encoder;
    GstElement *parse;
    GstElement *queue;
    GstElement *queue2;
    GstElement *tee;
} AudioPipe;

typedef struct VideoPipe
{
    GstElement *crop;
    GstElement *sink;
    GstElement *encoder;
    GstElement *videorate;
    GstElement *queue;
    GstElement *queue2;
    GstElement *parse;
    GstElement *convert;
    GstElement *capsfilter;
    //AudioPipe audioPipe;
    GstElement *bins;
    GstElement *overlay;
    //CustomData *customData;
} VideoPipe;

typedef struct MainPipe
{
    GstElement *pipeline;
    GstElement *src;
    GstElement *tee;
    GstElement *capsfilter;
    GstElement *convert;
    GstElement *queue;
    //GstCaps *caps;
    GstBus *bus;
    guint bus_watch_id;
    gboolean is_live;
    VideoPipe videoPipe[MAX_ENC];
    //AudioPipe audioPipe;
    guint8 index;
    guint8 ch;
} MainPipe;

#ifdef IPC_ENABLE
#include <sys/ipc.h>
#include <sys/msg.h>
#define MSG_Q_KEY	(0x64)
enum MSG_TYPE {
  PMSG_TYPE_UNUSED = 0,
  PMSG_TYPE_IPC
};

typedef struct _RecvBuf
{
    glong type;
    guint8 data[16];
} RecvBuf;

int ipc_init()
{
    gint ret = 0;
    gint msg_id;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);

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
    if(ret < 0)
		__LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);

    
    return ret;
}

int ipc_clear()
{
    gint ret = 0;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

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

    //g_thread_join(ipcThread);

    return ret;
}

int parseIpcRecvData(int msgId, char* data, int len)
{

}

static void ipcLoop(CustomData* data)
{
    RecvBuf recvbuf;
    gint msg_id;

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    ipc_init();

    while(1) 
    {
        usleep(1000);

        if(is_interrupted)
            break;

        msg_id = msgget((key_t)MSG_Q_KEY, IPC_CREAT | 0666);
        if(msg_id == -1) {
            perror("msgget fail");
            __LOG(LOG_CRIT, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, msg_id);
            return;
        }

        int ret = msgrcv(msg_id, &recvbuf, sizeof(recvbuf) - sizeof(long), PMSG_TYPE_IPC, 0);

        if(ret <= 0) {
            perror("msgrcv fail");
            __LOG(LOG_ERR, "[IPC][%s:%d] ret:%d", _FILE_, __LINE__, ret);
            return;
        }
        __LOG(LOG_INFO, "[IPC][%s:%d] recv data msg_id(%d) byte %d", _FILE_, __LINE__, msg_id, ret);

        //for(guint8 i=0; i<16; i++) __LOG(LOG_INFO, "[IPC][%s:%d] data[%d] : 0x%02x", _FILE_, __LINE__, i, recvbuf.data[i]);

        if(recvbuf.data[11] == 'c')
        {
            //captureStart(&data[(recvbuf.data[12]-0x30)*10 + (recvbuf.data[13]-0x30)]);
        }
        else if(recvbuf.data[11] == 'b')
        {
            //changeBitrate(&data[(recvbuf.data[12]-0x30)*10 + (recvbuf.data[13]-0x30)]);
        }
        else if(recvbuf.data[11] == 'f')
        {
            //changeFPS(&data[(recvbuf.data[12]-0x30)*10 + (recvbuf.data[13]-0x30)]);
        }
    }

    ipc_clear();

    return;
}
#endif //IPC_ENABLE

#ifdef TERMINAL_CMD_ENABLE
#include <fcntl.h>
static void check_terminal_input(gpointer data) 
{
    // 여기에서 터미널 입력을 확인하고 처리
    CustomData *customData = (CustomData *)data;
    char buffer[256];

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    int bytesRead;

    guint16 fps;
    GstState state;

    while (1) 
    {
        if(is_interrupted)
            break;

        // 비차단 입력 읽기 시도
        bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));

        if (bytesRead == -1) {
            // 에러 처리
            //perror("read");
        } else if (bytesRead > 0) {
            // 입력이 있을 경우 처리
            buffer[bytesRead] = '\0';
            printf("Input: %s", buffer);

            if (buffer[0] == 'i')
            {
                visibleSRT(FALSE, customData);
            }
            else if (buffer[0] == 'v')
            {
                visibleSRT(TRUE, customData);
            }
            else if (buffer[0] == 'c')
            {
                captureStart(&customData[CAPTURE0_L], (buffer[1]-0x30));
                captureStart(&customData[CAPTURE0_R], (buffer[1]-0x30));
                captureStart(&customData[CAPTURE1_L], (buffer[1]-0x30));
                captureStart(&customData[CAPTURE1_R], (buffer[1]-0x30));
            }
            else if (buffer[0] == '0')
            {
                gst_element_get_state(customData[FILE0_L].pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
                g_message("to : %s", gst_element_state_get_name(state));
            }
            else if (buffer[0] == '1')
            {
                if(customData[FILE1_L].pipeline == NULL)
                {
                    g_print("null\n");
                    return;
                }
                gst_element_get_state(customData[FILE1_L].pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
                g_message("to : %s", gst_element_state_get_name(state));
            }
            else if (buffer[0] == '2')
            {
                if(gst_element_set_state(GST_ELEMENT(customData[FILE0_L].pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
                {
                    __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state change error", _FILE_, __LINE__);
                }
            }
            else if (buffer[0] == '3')
            {
                if(gst_element_set_state(GST_ELEMENT(customData[FILE1_L].pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
                {
                    __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state change error", _FILE_, __LINE__);
                }
            }
            else if(buffer[0] == 'b')
            {
                if(buffer[1] == 'a')
                {
                    if(buffer[2] == 'f')
                    {
                        changeBitrate(&customData[FILE0_L]);
                        changeBitrate(&customData[FILE0_R]);
                        changeBitrate(&customData[FILE1_L]);
                        changeBitrate(&customData[FILE1_R]);
                    }
                    else if(buffer[2 == 'r'])
                    {
                        changeBitrate(&customData[RTSP0_L]);
                        changeBitrate(&customData[RTSP0_R]);
                        changeBitrate(&customData[RTSP1_L]);
                        changeBitrate(&customData[RTSP1_R]);
                    }
                }
                else
                    changeBitrate(&customData[(buffer[1]-0x30)*10 + (buffer[2]-0x30)]);
            }
            else if(buffer[0] == 'f')
            {
                fps = (buffer[3]-0x30)*10+(buffer[4]-0x30);
                if(buffer[1] == 'a')
                {
                    if(buffer[2] == 'f')
                    {
                        changeFPS(&customData[FILE0_L], fps);
                        changeFPS(&customData[FILE0_R], fps);
                        changeFPS(&customData[FILE1_L], fps);
                        changeFPS(&customData[FILE1_R], fps);
                    }
                    else if(buffer[2] == 'r')
                    {
                        changeFPS(&customData[RTSP0_L], fps);
                        changeFPS(&customData[RTSP0_R], fps);
                        changeFPS(&customData[RTSP1_L], fps);
                        changeFPS(&customData[RTSP1_R], fps);
                    }
                }
            }
            else if(!strncmp(buffer, "play", 4))
            {
                g_print("play!\n");
                if(buffer[4] == 0x30)
                {
                    if(gst_element_set_state(GST_ELEMENT(customData[FILE0_L].pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
                    {
                        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline [%c] state change error", _FILE_, __LINE__, buffer[4]);
                    }
                }
                if(buffer[4] == 0x31)
                {
                    if(gst_element_set_state(GST_ELEMENT(customData[FILE1_L].pipeline), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
                    {
                        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline [%c] state change error", _FILE_, __LINE__, buffer[4]);
                    }
                }
            }
            else if(!strncmp(buffer, "pause", 5))
            {
                g_print("pause!\n");
                if(buffer[5] == 0x30)
                {
                    if(gst_element_set_state(GST_ELEMENT(customData[FILE0_L].pipeline), GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE)
                    {
                        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline [%c] state change error", _FILE_, __LINE__, buffer[5]);
                    }
                }
                if(buffer[5] == 0x31)
                {
                    if(gst_element_set_state(GST_ELEMENT(customData[FILE1_L].pipeline), GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE)
                    {
                        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline [%c] state change error", _FILE_, __LINE__, buffer[5]);
                    }
                }
            }
#if 0
            else if (buffer[0] == 'p')
            {
                GstStateChangeReturn ret;
                GstState state;
                gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
                // g_message("to : %s", gst_element_state_get_name(state));
                if (state == GST_STATE_PLAYING)
                {
                    __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state from %s to PAUSED", _FILE_, __LINE__, gst_element_state_get_name(state));
                    ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
                }
                else
                {
                    __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state from %s to PLAYING", _FILE_, __LINE__, gst_element_state_get_name(state));
                    ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
                }

                if (ret == GST_STATE_CHANGE_FAILURE)
                {
                    __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state change error", _FILE_, __LINE__);
                    // gst_object_unref(pipeline);
                }
                else if (ret == GST_STATE_CHANGE_NO_PREROLL)
                {
                    // customData->is_live = TRUE;
                }
            }
            else if (buffer[0] == 'q')
            {
                // sigHandler(SIGTERM);
                gst_element_set_state(pipeline, GST_STATE_NULL);
                // gst_object_unref(pipeline);

#ifdef RTSP_ENABLE
                // g_object_unref(rtspServer);
#endif

                // g_main_loop_unref(gstLoop);
                destroy();
            }
#endif
            else
            {
                // g_print("You pressed '%c'\n", buffer[0]);
            }
        }

        // 다른 작업 수행 가능
        // ...

        usleep(100000); // 작업이 무한 루프를 돌지 않도록 잠시 쉼 (0.1초)
    }
    return;
}
#endif  //TERMINAL_CMD_ENABLE

void mylog( int opt, const char* _szfmt, ... )
{
	va_list va;
	char strTmp[512]; 
	const char *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};

	va_start( va, _szfmt );
	
	vsprintf(strTmp, _szfmt ,va);

	if(opt <= log_level || opt <= LOG_ALERT) {
		//syslog( opt|LOG_LOCAL0, "  [%5ld.%06ld] [%s]%s", ts.tv_sec, ts.tv_nsec/1000, debug_codes[opt], strTmp);
		syslog( opt|LOG_LOCAL0, "%s", strTmp);
	}
	if(opt <= dbg_level || opt <= LOG_ALERT) {
		//timer = time(NULL);
		//localtime_r(&timer, &t);
        GDateTime *datetime = g_date_time_new_now_local();
        //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");

        //gchar *tmp = g_strdup_printf("output_%s.mp4", date_str);
        //memcpy(file_name, tmp, 128);

		const char *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};
		g_print("%s%s %s: [%s]\033[0m", color_codes[opt], date_str, PROGRAM_NAME, debug_codes[opt]);

		vprintf( _szfmt, va );
		printf("\n");
		fflush(stdout);
        g_date_time_unref(datetime);
        g_free(date_str);
	}
	va_end( va );
}

static void destroy(void)
{
    //sleep(3);
    is_interrupted = 1;
    __LOG(LOG_EMERG, "[GST][%s:%d] destroy!!", _FILE_, __LINE__);
    //g_main_loop_unref(gstLoop);
    //sigflag = 0;
    //g_object_unref(rtspServer);

    return;
}

void sigHandler(int sig) 
{
    guint8 i;
    static guint8 count = 0;

    if(sig == SIGKILL || sig== SIGQUIT)
    {
        is_interrupted = 1;
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] sigHandler %d", _FILE_, __LINE__, sig);
    if(++count >= 3)
    {
        destroy();
        return;
    }
    //ipc_clear();
    sigflag = 1;

#if 1
    //if (pipeline)
    {
        for (i = 0; i < MAX_CAM; i++)
        {
            // gst_element_send_event(pipeline[i], gst_event_new_eos());
            {
                if (video_sink_pad[FILE0_L + i])
                {
                    __LOG(LOG_NOTICE, "[GST][%s:%d] sink video pad eos [%d]", _FILE_, __LINE__, i);
                    gst_pad_send_event(video_sink_pad[FILE0_L + i], gst_event_new_eos());
                }
                else
                    __LOG(LOG_NOTICE, "[GST][%s:%d] sink video pad NULL [%d]", _FILE_, __LINE__, i);

                if (audio_sink_pad[FILE0_L + i])
                {
                    __LOG(LOG_NOTICE, "[GST][%s:%d] sink audio pad eos [%d]", _FILE_, __LINE__, i);
                    gst_pad_send_event(audio_sink_pad[FILE0_L + i], gst_event_new_eos());
                }
                else
                    __LOG(LOG_NOTICE, "[GST][%s:%d] sink audio pad NULL [%d]", _FILE_, __LINE__, i);
            }
        }
    }
#endif

    destroy();

	return;
}


void attachInterruptHandlers()
{
    //signal(SIGUSR1, ipcHandler);
    signal(SIGINT, sigHandler);
    signal(SIGKILL, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGQUIT, sigHandler);
}

static void print_tag(const GstTagList * list, const gchar * tag, gpointer unused)
{
    gint i, count;

    count = gst_tag_list_get_tag_size (list, tag);

    for (i = 0; i < count; i++) {
      gchar *str;

      if (gst_tag_get_type (tag) == G_TYPE_STRING) {
        if (!gst_tag_list_get_string_index (list, tag, i, &str))
          g_assert_not_reached ();
      } else {
        str =
          g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
      }

      if (i == 0) {
        g_print ("  %15s: %s\n", gst_tag_get_nick (tag), str);
      } else {
        g_print ("                 : %s\n", str);
      }

      g_free (str);
    }
}

static gboolean my_bus_callback(GstBus *bus, GstMessage *message, gpointer data)
{
    //PipeMain *info = (PipeMain *)data;
    GstElement *pipeline = (GstElement *)data;
    //AudioPipe *info = (AudioPipe *)data;
    //static guint8 cam_cnt = 0;
    //static GstState state = GST_STATE_PLAYING;

    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_QOS) return TRUE;
    if(GST_MESSAGE_TYPE(message) == GST_MESSAGE_STREAM_STATUS) return TRUE;
    //printf("Got %s message\n", GST_MESSAGE_TYPE_NAME(message));
    if(GST_MESSAGE_TYPE(message) != GST_MESSAGE_STATE_CHANGED)
        __LOG(LOG_INFO, "[BUS][%s:%d] Got %s message from %s", _FILE_, __LINE__, GST_MESSAGE_TYPE_NAME(message), GST_OBJECT_NAME (message->src));

    switch(GST_MESSAGE_TYPE(message)) 
    {
        case GST_MESSAGE_STATE_CHANGED:
        {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(message, &old_state, &new_state, &pending_state);
            __LOG(LOG_DEBUG, "[BUS][%s:%d] state changed from %s to %s in %s", _FILE_, __LINE__,  \
                gst_element_state_get_name(old_state), gst_element_state_get_name(new_state), GST_OBJECT_NAME (message->src));
#if 0
            if(state != old_state)
            {
                __LOG(LOG_NOTICE, "[BUS][%s:%d] state changed from %s to %s in %s", _FILE_, __LINE__,  \
                    gst_element_state_get_name(old_state), gst_element_state_get_name(new_state), GST_OBJECT_NAME (message->src));
                state = old_state;
            }
#endif
            break;
        }

        case GST_MESSAGE_ERROR:
        {
            GError *err;
            gchar *debug;
            gst_message_parse_error(message, &err, &debug);
            __LOG(LOG_ERR, "[BUS][%s:%d] error message : %s\n", __FILE__, __LINE__, err->message);
            __LOG(LOG_ERR, "[BUS][%s:%d] error debug : %s\n", __FILE__, __LINE__, (debug)? debug : "none");
            printf("Error : %s\n", err->message);
            printf("Debug : %s\n", (debug)? debug : "none");
            g_error_free(err);
            g_free(debug);
            //destroy();
            gst_element_send_event(pipeline, gst_event_new_eos());
            break;
        }

        case GST_MESSAGE_EOS:
        {
            //printf("GST_MESSAGE_EOS (index:%d sigflag:%d)\n", info->index, sigflag);
            __LOG(LOG_NOTICE, "[BUS][%s:%d] GST_MESSAGE_EOS (sigflag:%d)", _FILE_, __LINE__, sigflag);
            if(sigflag)
            {
                //cam_cnt++;
                //if(cam_cnt >= MAX_PIPELINE) 
                destroy();
            }
            else
            {
                gst_element_set_state(pipeline, GST_STATE_READY);
                //gst_element_get_state(pipeline[info->index], NULL, NULL, GST_CLOCK_TIME_NONE);
                //gst_element_seek(pipeline[info->index], 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, -1);
                //gst_element_set_state(pipeline[info->index], GST_STATE_NULL);
                gst_element_set_state(pipeline, GST_STATE_PLAYING);
                //change_file_datetime(NULL);
            }

            break;
        }

        case GST_MESSAGE_ELEMENT:
        {
            __LOG(LOG_DEBUG, "[BUS][%s:%d] %s", _FILE_, __LINE__, gst_structure_to_string(gst_message_get_structure(message)));
            break;
        }

        case GST_MESSAGE_STREAM_STATUS:
        {
            GstStreamStatusType type;
            GstElement *owner;
            const GValue *val;
            gchar *path;
            GstTask *task = NULL;

            g_message ("received STREAM_STATUS");
            gst_message_parse_stream_status (message, &type, &owner);
            if(type == GST_STREAM_STATUS_TYPE_START) {
                //__LOG(LOG_NOTICE, "[BUS][%s:%d] audio queue flush", _FILE_, __LINE__);
                //gst_element_send_event(info->queue, gst_event_new_flush_start());
            }

            val = gst_message_get_stream_status_object (message);

            g_message ("type:   %d", type);
            path = gst_object_get_path_string (GST_MESSAGE_SRC (message));
            g_message ("source: %s", path);
            g_free (path);
            path = gst_object_get_path_string (GST_OBJECT (owner));
            g_message ("owner:  %s", path);
            g_free (path);
            g_message ("object: type %s, value %p", G_VALUE_TYPE_NAME (val),
            g_value_get_object (val));

            /* see if we know how to deal with this object */
            if (G_VALUE_TYPE (val) == GST_TYPE_TASK) {
                task = g_value_get_object (val);
            }

            switch (type) {
                case GST_STREAM_STATUS_TYPE_CREATE:
                g_message ("created task %p", task);
                break;
                case GST_STREAM_STATUS_TYPE_ENTER:
                /* g_message ("raising task priority"); */
                /* setpriority (PRIO_PROCESS, 0, -10); */
                break;
                case GST_STREAM_STATUS_TYPE_LEAVE:
                break;
                default:
                break;
            }
            break;
        }

        case GST_MESSAGE_QOS:
        {
#if 1
            gboolean live;
            guint64 running_time, stream_time,timestamp,duration;
            gst_message_parse_qos (message,&live,&running_time,&stream_time,&timestamp,&duration);
            g_warning("GOt a QOS event %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT" %"G_GUINT64_FORMAT"", running_time, stream_time, timestamp, duration);
            
            gint64 jitter;
            gdouble prop;
            gint qual;
            gst_message_parse_qos_values(message, &jitter, &prop, &qual);
            g_warning("gotQoSE %"G_GINT64_FORMAT" %f %d", jitter, prop, qual );

            GstFormat format;
            guint64 processed;
            guint64 dropped;
            gst_message_parse_qos_stats(message, &format, &processed, &dropped);

            g_print("QoS Message:\n");
            g_print("Format: %s\n", gst_format_get_name(format));
            g_print("Processed: %"G_GUINT64_FORMAT"\n", processed);
            g_print("Dropped: %"G_GUINT64_FORMAT"\n", dropped);
#endif
            break;
        }

        case GST_MESSAGE_TAG: 
        {
#if 0
            GstTagList *tags = NULL;
            gst_message_parse_tag (message, &tags);
            __LOG(LOG_INFO, "[BUS][%s:%d] Got tags from element %s", _FILE_, __LINE__, GST_OBJECT_NAME (message->src));
            gst_tag_list_foreach (tags, print_tag, NULL);
            gst_tag_list_free (tags);
            //handle_tags (tags);
            //gst_tag_list_unref (tags);
#endif
            break;
        }
#if 0
        case GST_MESSAGE_BUFFERING: 
        {
            gint percent = 0;

            /* If the stream is live, we do not care about buffering. */
            if (info->is_live) break;

            gst_message_parse_buffering (message, &percent);
            g_print ("Buffering (%3d%%)\r", percent);
            /* Wait until buffering is complete before start/resume playing */

            if (percent < 100)
                gst_element_set_state (pipe, GST_STATE_PAUSED);
            else
                gst_element_set_state (pipe, GST_STATE_PLAYING);
            break;

        }
#endif
        case GST_MESSAGE_NEW_CLOCK:
        {
            GstClock *clock;

            gst_message_parse_new_clock (message, &clock);
            __LOG(LOG_INFO, "[BUS][%s:%d] New clock: %s", _FILE_, __LINE__, (clock ? GST_OBJECT_NAME (clock) : "NULL"));
            break;
        }
        case GST_MESSAGE_CLOCK_LOST:
        {
            /* Get a new clock */
            __LOG(LOG_INFO, "[BUS][%s:%d] GST_MESSAGE_CLOCK_LOST", _FILE_, __LINE__);
            gst_element_set_state (pipeline, GST_STATE_PAUSED);
            gst_element_set_state (pipeline, GST_STATE_PLAYING);
            break;
        }

        case GST_MESSAGE_LATENCY:
            {
                // when pipeline latency is changed, this msg is posted on the bus. we then have
                // to explicitly tell the pipeline to recalculate its latency
                // FIXME: this never works!
                __LOG(LOG_INFO, "[BUS][%s:%d] GST_MESSAGE_LATENCY", _FILE_, __LINE__);
#if 1
                if (!gst_bin_recalculate_latency (GST_BIN(pipeline)))
                    __LOG(LOG_INFO, "[BUS][%s:%d] Could not reconfigure latency", _FILE_, __LINE__);
                else
                    __LOG(LOG_INFO, "[BUS][%s:%d] reconfigure latency", _FILE_, __LINE__);
                break;
#endif

            }

      case GST_MESSAGE_APPLICATION:
      {
        const GstStructure *s;
        s = gst_message_get_structure (message);
        if (gst_structure_has_name (s, "GstLaunchInterrupt")) {
          /* this application message is posted when we caught an interrupt and
           * we need to stop the pipeline. */
          g_print ("Interrupt: Stopping pipeline ...\n");
        }
        break;
      }
        default:
            break;

    }

    return TRUE;
}

/* called when a stream has received an RTCP packet from the client */
static void on_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  //GST_INFO ("source %p in session %p is active", source, session);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] source %p in session %p is active", _FILE_, __LINE__, source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    g_print ("structure: %s\n", sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

static void on_sender_ssrc_active (GObject * session, GObject * source, GstRTSPMedia * media)
{
  GstStructure *stats;

  //GST_INFO ("source %p in session %p is active", source, session);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] source %p in session %p is active", _FILE_, __LINE__, source, session);

  g_object_get (source, "stats", &stats, NULL);
  if (stats) {
    gchar *sstr;

    sstr = gst_structure_to_string (stats);
    g_print ("Sender stats:\nstructure: %s\n", sstr);
    g_free (sstr);

    gst_structure_free (stats);
  }
}

/* signal callback when the media is prepared for streaming. We can get the
 * session manager for each of the streams and connect to some signals. */
static void media_prepared_cb (GstRTSPMedia * media)
{
  guint i, n_streams;

  n_streams = gst_rtsp_media_n_streams (media);

  //GST_INFO ("media %p is prepared and has %u streams", media, n_streams);
  __LOG(LOG_NOTICE, "[RTSP][%s:%d] media %p is prepared and has %u streams", _FILE_, __LINE__, media, n_streams);

  for (i = 0; i < n_streams; i++) {
    GstRTSPStream *stream;
    GObject *session;

    stream = gst_rtsp_media_get_stream (media, i);
    if (stream == NULL)
      continue;

    session = gst_rtsp_stream_get_rtpsession (stream);
    //GST_INFO ("watching session %p on stream %u", session, i);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] watching session %p on stream %u", _FILE_, __LINE__, session, i);

    //g_signal_connect (session, "on-ssrc-active", (GCallback) on_ssrc_active, media);
    //g_signal_connect (session, "on-sender-ssrc-active", (GCallback) on_sender_ssrc_active, media);
  }
}

static gboolean captureStart(gpointer data, guint16 max_cnt)
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    //GstElement *pp = (GstElement *)data;
    //gst_element_set_state(pp, GST_STATE_PAUSED);
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    //g_print("%s\n", __FUNCTION__);

    info->file_name = g_strdup_printf("output_%s-ch%d", date_str, info->ch);
    info->captureCnt = 0;
    info->captureMax = max_cnt;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    g_date_time_unref(datetime);
    g_free(date_str);
    return TRUE;
}

static gboolean changeBitrate(gpointer data)
{
    //GstElement *enc = (GstElement *)data;
    CustomData *info = (CustomData *)data;
    gint bitrate;
    g_object_get(info->enc, "bitrate", &bitrate, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d get bitrate : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, bitrate);

    if(bitrate == 4096) g_object_set(info->enc, "bitrate", 1024, NULL);
    if(bitrate == 1024) g_object_set(info->enc, "bitrate", 4096, NULL);

    g_object_get(info->enc, "bitrate", &bitrate, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d set bitrate : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, bitrate);
    
    return TRUE;
}

static gboolean changeFPS(gpointer data, guint16 setfps)
{
    CustomData *info = (CustomData *)data;
    gint getfps;
    g_object_get(info->vr, "max-rate", &getfps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d get fps : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, getfps);

    //if(fps == FILE_FPS) g_object_set(info->vr, "max-rate", RTSP_FPS, NULL);
    //if(fps == RTSP_FPS) g_object_set(info->vr, "max-rate", FILE_FPS, NULL);
    g_object_set(info->vr, "max-rate", setfps, NULL);

    g_object_get(info->vr, "max-rate", &getfps, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (ch%d set fps : %d)", _FILE_, __LINE__, __FUNCTION__, info->ch, getfps);

#if 0
    GstElement *capsfilter = (GstElement *)data;
    GstCaps *caps;

    g_object_get(capsfilter, "caps", &caps, NULL);

    if(caps)
    {
        GstStructure *structure = gst_caps_get_structure(caps, 0);

        // Read the framerate value as a fraction
        gint numerator, denominator;
        gst_structure_get_fraction(structure, "framerate", &numerator, &denominator);

        // Calculate the framerate as an integer
        gint framerate = numerator / denominator;

        // Print the framerate value
        g_print("Framerate: %d\n", framerate);

        // Unreference the caps and clean up
        //gst_caps_unref(caps);

        if(framerate == 15)
        {
            caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, _WIDTH*2,
                                        "height", G_TYPE_INT, _HEIGHT,
                                        "framerate", GST_TYPE_FRACTION, 30, 1,
                                        NULL);
        }
        else if(framerate == 30)
        {
            caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, _WIDTH*2,
                                        "height", G_TYPE_INT, _HEIGHT,
                                        "framerate", GST_TYPE_FRACTION, 15, 1,
                                        NULL);
        }

        g_object_set(capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);
    }
#endif
    return TRUE;
}

// appsink의 "new-sample" 시그널 처리 함수
static GstFlowReturn new_sample_handler(GstElement *sink, gpointer userData) 
{
    GstSample *sample;
    GstBuffer *buffer;
    CustomData *info = (CustomData *)userData;
    GstMapInfo map;
    gchar *path = NULL;
    FILE *file;
    //GstClockTime timestamp;

    // sample을 가져오기
    //g_signal_emit_by_name(appsink, "pull-sample", &sample);
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        //__LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }
    buffer = gst_sample_get_buffer(sample);
    gst_sample_unref(sample);
    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        //gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    
    if(!info->appsrc)
    {
        return GST_FLOW_OK;
    }

    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)){
        g_printerr("Failed to map buffer\n");
        return GST_FLOW_ERROR;
    }

    //GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buffer);
	//GstClockTime dts = GST_BUFFER_DTS(buffer);
	static GstClockTime pts = 0;
	//GstClockTime dur = GST_BUFFER_DURATION(buffer);

    switch(info->mode)
    {

        case RECORDING:
            {
                GST_BUFFER_PTS(buffer) = pts;
#if 0
                if(pts <= GST_SECOND * 60)
                    pts +=  GST_SECOND / 30;
                else
                    pts = 0;
#endif

                //GST_BUFFER_TIMESTAMP(buffer) = timestamp;
                //GST_BUFFER_DURATION(buffer) = dur;

                //path = g_strdup_printf("%s%s_%s-ch%d.mp4", FILE_PATH, ohtName, fileDateTime, info->ch);
                //path = g_strdup_printf("%s%s_%s-ch%d.mp4", FILE_PATH, ohtName, info->file_name, info->ch);
                path = g_strdup_printf("%s%s.mp4", FILE_PATH, info->file_name);

                //path = g_strdup_printf("%s%s.mp4", FILE_PATH, info->file_name);
                //guint buffer_size;
                //guint8 *data;
                //g_print("path : %s\n", path);
                file = fopen(path, "ab");
                if (file) {
                    fwrite(map.data, 1, map.size, file);
                    //fwrite(&pts, sizeof(GstClockTime), 1, file);
                    //if (gst_buffer_extract(buffer, 0, &data, gst_buffer_get_size(buffer)) == TRUE) fwrite(data, 1, gst_buffer_get_size(buffer), path);

                    fclose(file);
                    //g_print("file close\n");

                } else {
                    __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, path);
                } 
            }
            break;
        case STREAMING:
            {
                // Create a new buffer and copy data
                info->buf = gst_buffer_new_and_alloc(map.size);
                gst_buffer_fill(info->buf, 0, map.data, map.size);

                // Unmap the original buffer
                //gst_buffer_unmap(buffer, &map);
                //info->buf = newBuffer;
                    
                // Push the new buffer to the appsrc
                //g_thread_new("data-processing-thread", (GThreadFunc)process_data_thread, info);
                gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);
            }
            break;
        case CAPTURING:
            {
                if(info->captureCnt >= info->captureMax)
                {
                    //__LOG(LOG_DEBUG, "[GST][%s:%d] captureMax", _FILE_, __LINE__);
                    gst_buffer_unmap(buffer, &map);
                    //gst_sample_unref(sample);
                    return GST_FLOW_OK;
                }
                __LOG(LOG_DEBUG, "[GST][%s:%d] captureCnt %d captureMax %d", _FILE_, __LINE__, info->captureCnt, info->captureMax);

                path = g_strdup_printf("%s%s-%d.jpg", FILE_PATH, info->file_name, info->captureCnt++);
                //path = g_strdup_printf("%s%s_%s_%s-%d-%d.jpg", FILE_PATH, info->ohtName, info->date, info->time, info->ch, info->captureCnt++);
                //path = g_strdup_printf("%s%s_%s-ch%d-%d.jpg", FILE_PATH, ohtName, fileDateTime, info->ch, info->captureCnt++);

                //g_print("path : %s\n", path);
                file = fopen(path, "ab");
                if (file) {
                    fwrite(map.data, 1, map.size, file);
                    fclose(file);
                } else {
                    __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, path);
                }
            }
            break;

        default:
            __LOG(LOG_ERR, "[GST][%s:%d] video mode (%d) unknown ", _FILE_, __LINE__, info->mode);
            break;
    }

    gst_buffer_unmap(buffer, &map);
    //gst_sample_unref(sample);
    if(path!=NULL) g_free(path);

    return GST_FLOW_OK;
}

static gboolean change_file_datetime() 
{
    //CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    static gint oldMin = -1;
    gint newMin = g_date_time_get_minute(datetime);
    //g_print("oldMin:%d newMin:%d\n", oldMin, newMin);
    if(newMin == oldMin)
        return FALSE;

    oldMin = newMin;
    //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    
    if(fileDateTime == NULL) 
    {
        fileDateTime = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
    else
    {
        fileDateTime = g_date_time_format(datetime, "%Y%m%d_%H%M00");
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (fileDateTime : %s)", _FILE_, __LINE__, __FUNCTION__, fileDateTime);
    g_date_time_unref(datetime);

    return TRUE;
}

static gboolean change_file_name(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str;

    //gst_element_send_event(info->mux, gst_event_new_eos());
    date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    info->file_name = g_strdup_printf("%s_%s-ch%d", ohtName, date_str, info->ch);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    g_date_time_unref(datetime);
    g_free(date_str);

    return TRUE;
}

static gboolean setSRT(gpointer data) 
{
    CustomData *customData = (CustomData *)data;
    static gint index = 0;
    guint8 i;
    gchar* text;
    //GDateTime *datetime = g_date_time_new_now_local();
    //text = g_strdup_printf("2023-01-27 22:40:02 VD3001, M, A, 34049/174014(1000000), 1298.5678mm/s, 300mV, (?)400mA, 80.5%/71.5%, E696, Level 7, Level 4");
#ifdef TIMEOVERLAY
    text = g_strdup_printf("VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, %dmV, (?)400mA, 80.5%/71.5%, E696, Level 7, Level 4", index++);
#else
    text = g_strdup_printf("%s VD3001, M, A, 34049/174014(1000000), \n1298.5678mm/s, %dmV, (?)400mA, 80.5%%/71.5%%, E696, Level 7, Level 4", \
                        g_date_time_format(g_date_time_new_now_local(), "%Y-%m-%d %H:%M:%S"), index++);
#endif
    //__LOG(LOG_DEBUG, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);
    //g_object_set(info->timeoveraly, "text", g_strdup_printf("test srt num(%d)", i++), NULL);
    for(i=0; i<MAX_CAM; i++)
    {
        
#ifdef RECORD_ENABLE
        g_object_set(customData[FILE0_L+i].timeoveraly, "text", text, NULL);
#endif

#ifdef RTSP_ENABLE
        g_object_set(customData[RTSP0_L+i].timeoveraly, "text", text, NULL);
#endif

    }

    index++;

    return TRUE;
}

static gboolean visibleSRT(gboolean val, gpointer data) 
{
    CustomData *customData = (CustomData *)data;
    guint8 i;
    gboolean visible = !val;

    __LOG(LOG_DEBUG, "[GST][%s:%d] %s (%d)", _FILE_, __LINE__, __FUNCTION__, visible);
    //g_object_set(info->timeoveraly, "text", g_strdup_printf("test srt num(%d)", i++), NULL);
    for(i=0; i<MAX_CAM; i++)
    {
#ifdef RECORD_ENABLE
        g_object_set(customData[FILE0_L+i].timeoveraly, "silent", visible, NULL);
#endif

#ifdef RTSP_ENABLE
        g_object_set(customData[RTSP0_L+i].timeoveraly, "silent", visible, NULL);
#endif

    }

    return TRUE;
}

static gboolean sendEOS(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    //PipeMain *info = (PipeMain *)data;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);

    //gst_element_send_event(pipeline[info->index], gst_event_new_eos());
    gst_pad_send_event(info->bin_video_pads, gst_event_new_eos());
    gst_pad_send_event(info->bin_audio_pads, gst_event_new_eos());
    //gst_pad_send_event(info->sink_pads, gst_event_new_eos());
    //gst_pad_send_event(info->mux_src_pads, gst_event_new_eos());

    //gst_element_set_state(pipeline[info->index], GST_STATE_READY);
    //gst_element_set_state(pipeline[info->index], GST_STATE_PLAYING);

    return TRUE;
}

static gboolean splitNow(gpointer data) 
{
    CustomData *info = (CustomData *)data;
    //PipeMain *info = (PipeMain *)data;
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s (index : %d, ch : %d)", _FILE_, __LINE__, __FUNCTION__, info->index, info->ch);

    g_signal_emit_by_name (info->appsink, "split-now");
    //g_signal_emit_by_name (info->appsink, "split-after");
    //g_signal_emit_by_name (info->appsink, "split-at-running-time");

    return TRUE;
}

static GstFlowReturn new_preroll_handler(GstElement *sink, gpointer data) {
    // Preroll frame 처리 작업 추가 가능
    CustomData *info = (CustomData *)data;
    gchar *sink_name;
    info->appsink = sink;
    info->is_live = TRUE;
    //g_print("Preroll frame\n");
    //__LOG(LOG_NOTICE, "[GST][%s:%d] %s (file_name : %s)", _FILE_, __LINE__, __FUNCTION__, info->file_name);
    sink_name = gst_object_get_name(GST_OBJECT(sink));
#if 0
    if(g_str_equal(sink_name, "sink0") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = FILE_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink1") == TRUE) //|| g_str_equal(sink_name, "sink3") == TRUE)
    {
        info->index = FILE_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
    }
    else if(g_str_equal(sink_name, "sink2") == TRUE) //|| g_str_equal(sink_name, "sink3") == TRUE)
    {
        info->index = RTSP_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
    }
    else if(g_str_equal(sink_name, "sink3") == TRUE) //|| g_str_equal(sink_name, "sink3") == TRUE)
    {
        info->index = RTSP_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC3_NAME);
    }
    else if(g_str_equal(sink_name, "sink4") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = CAPTURE_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink5") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = CAPTURE_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink6") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = FILE2_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink7") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = FILE2_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink8") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = RTSP2_L;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else if(g_str_equal(sink_name, "sink9") == TRUE) //|| g_str_equal(sink_name, "sink2") == TRUE)
    {
        info->index = RTSP2_R;
        //info->appsrc_name = g_strdup_printf("%s", APPSRC2_NAME);
    }
    else
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] sink name %s is not matching", _FILE_, __LINE__, sink_name);
        return GST_FLOW_ERROR;
    }
    //info->ch = info->index%2;
#endif
    __LOG(LOG_NOTICE, "[GST][%s:%d] sink_name:%s channel:%d", _FILE_, __LINE__, sink_name, info->ch);
    g_free (sink_name);

#ifndef RECORD_SPLITMUXSINK_ENABLE
    if(info->mode == RECORDING)
    {
        change_file_name(info);
        g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)change_file_name, info);
    }
#endif

    return GST_FLOW_OK;
}

static void	media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
	//GstElement *src = (GstElement*)user_data;
    CustomData *info = (CustomData*)user_data;
	GstElement *element;
    //GstElement *queue;
    gchar *appsrc_name;
    //gchar *queue_name;
    //static GMutex mutex;

	//g_mutex_lock(&mutex);
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
	element = gst_rtsp_media_get_element(media);
    //name = GST_ELEMENT_NAME(GST_APP_SRC(element));
    //name = gst_object_get_name(GST_OBJECT(element));
    appsrc_name = g_strdup_printf("%s%d", APPSRC_NAME, info->index);
    __LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	info->appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), appsrc_name);

    //queue_name = g_strdup_printf("%s", QUEUE_NAME, info->ch);
    //__LOG(LOG_NOTICE, "[GST][%s:%d] appsrc name : %s", _FILE_, __LINE__, appsrc_name);
	//queue = gst_bin_get_by_name_recurse_up(GST_BIN(element), queue_name);

    //g_object_set (G_OBJECT (queue), 

    g_free(appsrc_name);
    //g_free(queue_name);
	gst_object_unref(element);

    g_signal_connect (media, "prepared", (GCallback) media_prepared_cb, factory);
	//g_mutex_unlock(&mutex);
    return;
}

static gboolean cleanRtspSession(GstRTSPServer *server)
{
  GstRTSPSessionPool *pool;

   //__LOG(LOG_DEBUG, "[GST][%s:%d] rtsp session pool", _FILE_, __LINE__);

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}


static void pad_added_handler (GstElement * src, GstPad * new_pad, gpointer data)
{
  GstElement *sink = (GstElement*)data;
  GstPad *sink_pad = gst_element_get_static_pad (sink, "sink");
  
  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

#if 1
  GstCaps *new_pad_caps = NULL;
  GstPadLinkReturn ret;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps (new_pad);

  gchar *caps_str = gst_caps_to_string(new_pad_caps);
  __LOG(LOG_NOTICE, "[GST][%s:%d] Current caps: %s", _FILE_, __LINE__, caps_str);
  g_free(caps_str);
  
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "video/x-raw")) {
    g_print ("It has type '%s' which is not raw video. Ignoring.\n",
        new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);
#endif
  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}

static void pad_removed_handler (GstElement * src, GstPad * new_pad, gpointer data)
{
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

    return;
}

static void client_closed(GstRTSPClient* client, gpointer user_data)
{
	//CustomData *info = (CustomData*)user_data;
    //gchar *client_ip = (gchar *)user_data;
	const gchar *client_ip = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP : %s", _FILE_, __LINE__, client_ip);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP2 : %s", _FILE_, __LINE__, client_ip_2);

    //if(client_ip)g_free(client_ip);
}

static gboolean handle_client_connected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) 
{
    //CustomData *info = (CustomData*)user_data;
    // This function is called when a new client is connected to the RTSP server.
    // You can handle the client connection here.
    const gchar *client_ip = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));
    //GstRTSPUrl *url = gst_rtsp_connection_get_url(gst_rtsp_client_get_connection(client));
    //GstRTSPMountPoints *mount = gst_rtsp_client_get_mount_points(client);
    //GstRTSPContext  *path = gst_rtsp_client_get_rtsp_context(client);
    //GstRTSPMountPoints *mounts = gst_rtsp_server_get_mount_points(server);

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, client_ip);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, url->host);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %s", _FILE_, __LINE__, url->abspath);
    //__LOG(LOG_NOTICE, "[RTSP][%s:%d] Connect - IP : %d", _FILE_, __LINE__, url->port);

    g_signal_connect(client, "closed", (GCallback)(client_closed), NULL);

    //g_free(client_ip);
    // Initialize session for the new client
    
    return TRUE;
}

static gchararray get_filename(gpointer data)
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    gchararray file_name = g_strdup_printf("%s_%s-ch%d.mp4", ohtName, date_str, info->ch);

    __LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

static gchararray format_location(GstElement *sink, guint arg0, gpointer data)
{
    CustomData *info = (CustomData *)data;
    GDateTime *datetime = g_date_time_new_now_local();
    //gint sec = g_date_time_get_second(datetime);
    gchar *date_str;

#if 1
    if(info->firstSplitFlag == 0)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] ch%d firstSplitFlag", _FILE_, __LINE__, info->ch);
        //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, data);
        info->firstSplitFlag = 1;
        date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
    else
    {
        if(info->is_live)
            date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
        else
            date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    }
#endif

    //date_str = g_date_time_format(datetime, "%Y%m%d_%H%M%S");
    gchararray file_name = g_strdup_printf("%s%s_%s-ch%d.mp4", FILE_PATH, ohtName, date_str, info->ch);
    
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, file_name);

    g_date_time_unref(datetime);
    g_free(date_str);

    return file_name;
}

void sink_added(GstElement *sink, guint arg0, gpointer data)
{
   
    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    __LOG(LOG_DEBUG, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, info->ch);
    sink_name = gst_object_get_name(GST_OBJECT(sink));
    info->is_live = TRUE;
    g_print("sink name : %s\n", sink_name);
#if 0
    if(g_str_equal(sink_name, "appsink0") == TRUE)
    {
        g_print("sink name correct\n");
        info->ch = 0;
    }
    else if(g_str_equal(sink_name, "appsink1") == TRUE)
    {
        g_print("sink name incorrect\n");
        info->ch = 1;
    }
    else
    {
        g_message("sink name err");
    }
#endif
    g_free(sink_name);

    return;
}


void muxer_added(GstElement *sink, guint arg0, gpointer data)
{
    CustomData *info = (CustomData *)data;
    gchar *sink_name;

    __LOG(LOG_DEBUG, "[GST][%s:%d] %s ch : %d", _FILE_, __LINE__, __FUNCTION__, info->ch);
    sink_name = gst_object_get_name(GST_OBJECT(sink));
    g_print("sink name : %s\n", sink_name);
#if 0
    sink_name = gst_object_get_name(GST_OBJECT(sink));
    if(g_str_equal(sink_name, "appsink0") == TRUE)
    {
        g_print("sink name correct\n");
        info->ch = 0;
    }
    else if(g_str_equal(sink_name, "appsink1") == TRUE)
    {
        g_print("sink name incorrect\n");
        info->ch = 1;
    }
    else
    {
        g_message("sink name err");
    }
#endif
    g_free(sink_name);
    return;
}

static gboolean eos_callback(GstAppSink *appsink, gpointer user_data) 
{
    CustomData *info = (CustomData *)user_data;

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s (ch:%d)", _FILE_, __LINE__, __FUNCTION__, info->ch);

    //GstState state;
    //gst_element_get_state(pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
    //g_message("to : %s", gst_element_state_get_name(state));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s end", _FILE_, __LINE__, __FUNCTION__);

    return TRUE;
}

static gboolean underrun_callback(GstElement *queue, gpointer user_data) {
    gchar *name = gst_object_get_name(GST_OBJECT(queue));
    g_print("%s name:%s\n", __FUNCTION__, name);
    g_free(name);
    return FALSE;
}

static gboolean overrun_callback(GstElement *queue, gpointer user_data) {
    gchar *name = gst_object_get_name(GST_OBJECT(queue));
    g_print("%s name:%s\n", __FUNCTION__, name);
    g_free(name);
    return FALSE;
}

static gboolean running_callback(GstElement *queue, gpointer user_data) {
    gchar *name = gst_object_get_name(GST_OBJECT(queue));
    g_print("%s name:%s\n", __FUNCTION__, name);
    g_free(name);
    return FALSE;
}

static gboolean pushing_callback(GstElement *queue, gpointer user_data) {
    gchar *name = gst_object_get_name(GST_OBJECT(queue));
    g_print("%s name:%s\n", __FUNCTION__, name);
    g_free(name);
    return FALSE;
}

gint setRtspPipe(GstElement *pipeline, gpointer data, gpointer user_data, guint8 idx)
{
    // GgstLoop *loop;
    // GstRTSPServer *server;
    // GstRTSPMountPoints *mounts;
    GstRTSPMediaFactory *factory;
    VideoPipe *pipe = (VideoPipe *)data;
    CustomData *info = (CustomData *)user_data;
    //GstElement *pipeline = (GstElement *)mainPipe;
    guint8 ch_num = idx % 4;
    gchar *srcName = g_strdup_printf("%s%d", APPSRC_NAME, idx);
    static gboolean first_f = 0;
    guint8 queue_leaky = LEAKY_DOWNSTREAM;

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] rtsp enable [%d]", _FILE_, __LINE__, ch_num);
    pipe->videorate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", idx));
    pipe->crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", idx));
    pipe->encoder = gst_element_factory_make("vpuenc_h264", g_strdup_printf("vpuenc_h264%d", idx));
    pipe->queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", idx));
    pipe->capsfilter = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter%d", idx));
    pipe->convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("videoconvert%d", idx));
    pipe->sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", idx));

    if (!pipe->videorate || !pipe->crop || !pipe->encoder || !pipe->queue || !pipe->capsfilter || !pipe->convert || !pipe->sink)
    {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp pipe[%d] create error", _FILE_, __LINE__, ch_num);
        // gst_object_unref(pipeline[i]);
        return -1;
    }

    gst_bin_add_many(GST_BIN(pipeline), pipe->crop, pipe->queue, pipe->videorate, pipe->encoder, pipe->capsfilter, pipe->convert, pipe->sink, NULL);
#ifdef OVERLAY_ENABLE

#ifdef TIMEOVERLAY
    pipe->overlay = gst_element_factory_make("timeoverlay", g_strdup_printf("overlay%d", idx));
    g_object_set(pipe->overlay, "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
    g_object_set(pipe->overlay, "show-times-as-dates", TRUE, NULL);
    g_object_set(pipe->overlay, "datetime-epoch", g_date_time_new_now_local(), NULL);
#else
    pipe->overlay = gst_element_factory_make("textoverlay", g_strdup_printf("overlay%d", idx));
#endif

    g_object_set(pipe->overlay, "valignment", 2, NULL);
    g_object_set(pipe->overlay, "halignment", 0, NULL);
    g_object_set(pipe->overlay, "font-desc", "Times New Roman Italic, 10", NULL);

    if(!pipe->overlay)
    {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp pipe overlay[%d] create error", _FILE_, __LINE__, ch_num);
        return -1;
    }

    if(!gst_bin_add(GST_BIN(pipeline), pipe->overlay))
    {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp pipe overlay[%d] add error", _FILE_, __LINE__, ch_num);
        return -1;
    }

    if (!gst_element_link_many(pipe->queue, pipe->crop, pipe->overlay, pipe->videorate, pipe->encoder, pipe->sink, NULL))
#else
    if(!gst_element_link_many(pipe->queue, pipe->crop, pipe->videorate, pipe->capsfilter, pipe->convert, pipe->encoder, pipe->sink, NULL))
    //if(!gst_element_link_many(pipe->queue, pipe->crop, pipe->videorate, pipe->encoder, pipe->sink, NULL))
#endif
    {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp pipe[%d] link error", _FILE_, __LINE__, ch_num);
        // gst_object_unref(pipeline[i]);
        return -1;
    }

    // g_object_set(main[i].videoPipe[dir].overlay, "text", "text", NULL);
    // g_object_set(main[j/2].videoPipe[dir].overlay, "line-alignment", 0, NULL);
    // g_object_set(main[i].videoPipe[dir].overlay, "font-desc", "Arial Bold, 16", NULL);
    // g_object_set(main[i].videoPipe[dir].overlay, "font-desc", "Helvetica Bold Italic, 16", NULL);
    // g_object_set(main[i].videoPipe[dir].overlay, "font-desc", "DejaVu Serif, 16", NULL);
    // g_object_set(main[i].videoPipe[dir].overlay, "x-absolute", 10.0, NULL);
    // g_object_set(main[i].videoPipe[dir].overlay, "y-absolute", 10.0, NULL);
    // g_object_set(main[i].videoPipe[dir].overlay, "text-x", 1000, NULL);

    GstCaps *caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, RTSP_FPS, 1, NULL);
#if 0
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "RGB16",
                                            "width", G_TYPE_INT, _WIDTH,
                                            "height", G_TYPE_INT, _HEIGHT,
                                            "framerate", GST_TYPE_FRACTION, RTSP_FPS, 1,
                                            NULL);
#endif
    g_object_set(pipe->capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (ch_num % 2 == 0)
        g_object_set(pipe->crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
    else
        g_object_set(pipe->crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

    //g_object_set(pipe->videorate, "max-rate", RTSP_FPS, NULL);
    //g_object_set(pipe->videorate, "drop-only", TRUE, NULL);
    g_object_set(pipe->encoder, "bitrate", RTSP_BITRATE, NULL);
    // g_object_set(encoder2, "gop-size", 30, NULL);
    // g_object_set(appsink2, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_object_set(pipe->queue, "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", queue_leaky, NULL);
    //g_object_set(pipe->queue, "flush-on-eos", TRUE, NULL);
#if 0
    g_signal_connect(pipe->queue, "underrun", G_CALLBACK(underrun_callback), NULL);
    g_signal_connect(pipe->queue, "overrun", G_CALLBACK(overrun_callback), NULL);
    g_signal_connect(pipe->queue, "running", G_CALLBACK(running_callback), NULL);
    g_signal_connect(pipe->queue, "pushing", G_CALLBACK(pushing_callback), NULL);
#endif
    g_object_set(pipe->sink, "max-buffers", 60, NULL);
    g_object_set(pipe->sink, "drop", TRUE, NULL);
    g_object_set(pipe->sink, "max-lateness", 100*GST_MSECOND, NULL);
    //g_object_set(pipe->sink, "render-delay", 100*GST_MSECOND, NULL);
    g_object_set(pipe->sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    // g_object_set(appsink2, "enable-last-sample", TRUE, NULL);
    // g_object_set(appsink2, "throttle-time", 100, NULL);
    // g_object_set(appsink2, "processing-deadline", 200, NULL);
    // g_object_set(appsink2, "render-delay", 1000, NULL);
    // g_object_set(appsink2, "wait-on-eos", FALSE, NULL);
    // g_object_set(appsink2, "qos", TRUE, NULL);
    // g_object_set(appsink2, "max-bitrate", 2048, NULL);
    g_signal_connect(pipe->sink, "eos", G_CALLBACK(eos_callback), NULL);
    g_signal_connect(pipe->sink, "new-sample", G_CALLBACK(new_sample_handler), info);
    g_signal_connect(pipe->sink, "new-preroll", G_CALLBACK(new_preroll_handler), info);
    g_signal_connect(pipe->sink, "pad-added", G_CALLBACK(pad_added_handler), pipe->encoder);
    g_signal_connect(pipe->sink, "pad-removed", G_CALLBACK(pad_removed_handler), NULL);
    info->index = idx;
    info->enc = pipe->encoder;
    info->vr = pipe->videorate;
    info->timeoveraly = pipe->overlay;
    info->ch = ch_num;
    info->mode = STREAMING;
    info->is_live = FALSE;
    //info->appsrc = gst_element_factory_make("appsrc", srcName);
    info->appsrc = NULL;
    info->pipeline = pipeline;
    // gst_init (&argc, &argv);
    // g_print("info.ch:%d info.filename:%d\n", info->ch, info->file_name);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s start", _FILE_, __LINE__, __FUNCTION__);

    // GgstLoop *loop = g_main_loop_new (NULL, FALSE);
    // info->rtspLoop = g_main_loop_new (NULL, FALSE);

    /* create a server instance */
    // server = gst_rtsp_server_new ();

    // g_object_set (server, "service", "8554", NULL);
    /* get the mount points for this server, every server has a default object
     * that be used to map uri mount points to media factories */
    // mounts = gst_rtsp_server_get_mount_points (rtspServer);

    /* make a media factory for a test stream. The default media factory can use
     * gst-launch syntax to create pipelines.
     * any launch line works as long as it contains elements named pay%d. Each
     * element with pay%d names will be a stream */
    factory = gst_rtsp_media_factory_new();
    // factory1 = gst_rtsp_media_factory_new ();

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] appsrc name : %s", _FILE_, __LINE__, g_strdup_printf("%s%d", APPSRC_NAME, info->index));
    //gchar *launch_str = g_strdup_printf("( appsrc name=%s is-live=1 ! queue ! h264parse \
                    ! rtph264pay name=pay0 config-interval=-1 )", info->appsrc_name);
    // gchar *launch_str = g_strdup_printf("( appsrc name=%s%d is-live=1 ! queue max-size-time=0 max-size-buffers=1 ! h264parse ! rtph264pay name=pay0 )", APPSRC_NAME, info->ch);
    gchar *launch_str = g_strdup_printf("( appsrc name=%s do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=10 leaky=2 ! h264parse ! rtph264pay name=pay0 config-interval=-1 )", srcName);
    //gchar *launch_str = g_strdup_printf("( appsrc name=%s max-bytes=0 do-timestamp=1 is-live=1 format=3 ! queue max-size-buffers=10 leaky=2 \
			! h264parse ! rtph264pay name=pay0 config-interval=-1 )", info->appsrc_name);
    gst_rtsp_media_factory_set_launch(factory, launch_str);
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    g_free(launch_str);

    // gst_rtsp_media_factory_set_launch(factory1, launch_str);
    // gst_rtsp_media_factory_set_shared(factory1, TRUE);
    /* notify when our media is ready, This is called whenever someone asks for
     * the media and a new pipeline with our appsrc is created */
    g_signal_connect(factory, "media-configure", (GCallback)media_configure, info);
    // g_signal_connect (factory, "client-connected", (GCallback) handle_client_connected, info);
    // g_signal_connect (factory, "media-constructed", (GCallback) media_constructed, info);
    // g_signal_connect(factory, "pad-added", G_CALLBACK(pad_added_handler), NULL);
    // g_signal_connect(factory, "pad-removed", G_CALLBACK(pad_removed_handler), NULL);
#if 1
    //gst_rtsp_media_factory_set_latency(factory, 0);
    //gst_rtsp_media_factory_set_publish_clock_mode(factory, GST_RTSP_PUBLISH_CLOCK_MODE_CLOCK_AND_OFFSET);
    //gst_rtsp_media_factory_set_max_mcast_ttl(factory, 2);
    //gst_rtsp_media_factory_set_bind_mcast_address(factory, TRUE);
    //gst_rtsp_media_factory_set_dscp_qos(factory, 30);
    //gst_rtsp_media_factory_set_eos_shutdown(factory ,TRUE);

    if (first_f == 0)
    {
        __LOG(LOG_INFO, "[RTSP][%s:%d] suspend_mode : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_suspend_mode(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] protocol : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_protocols(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] profile : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_profiles(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] buffer size : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_buffer_size(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] retransmisson time : %" GST_TIME_FORMAT "", _FILE_, __LINE__, gst_rtsp_media_factory_get_retransmission_time(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] do retransmisson  : %s", _FILE_, __LINE__, gst_rtsp_media_factory_get_do_retransmission(factory) ? "TRUE" : "FALSE");
        __LOG(LOG_INFO, "[RTSP][%s:%d] latency  : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_latency(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] transport mode  : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_transport_mode(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] media type  : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_media_gtype(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] clock : %" GST_TIME_FORMAT "", _FILE_, __LINE__, gst_rtsp_media_factory_get_clock(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] publish clock mode : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_publish_clock_mode(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] max mcast ttl : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_max_mcast_ttl(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] bind mcast address : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_bind_mcast_address(factory) ? "TRUE" : "FALSE");
        __LOG(LOG_INFO, "[RTSP][%s:%d] dscp qos : %d", _FILE_, __LINE__, gst_rtsp_media_factory_get_dscp_qos(factory));
        __LOG(LOG_INFO, "[RTSP][%s:%d] eos shutdown : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_eos_shutdown(factory) ? "TRUE" : "FALSE");
        __LOG(LOG_INFO, "[RTSP][%s:%d] stop on disconnect : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_stop_on_disonnect(factory) ? "TRUE" : "FALSE");
        __LOG(LOG_INFO, "[RTSP][%s:%d] shared : %s", _FILE_, __LINE__, gst_rtsp_media_factory_is_shared(factory) ? "TRUE" : "FALSE");
        first_f = 1;
    }
    // gst_rtsp_media_factory_set_suspend_mode(factory, GST_RTSP_SUSPEND_MODE_PAUSE);
#endif
    /* attach the test factory to the /test url */
    gchar *point = g_strdup_printf("/ch%d", info->ch);
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] point : %s", _FILE_, __LINE__, point);

    gst_rtsp_mount_points_add_factory(rtspMounts, point, factory);

    gst_rtsp_media_factory_add_role(factory, "semes",
                                    GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
                                    GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);

#if 0
    GstRTSPUrl *url;
    GstRTSPMedia *media;
    GstElement *pipelineRtsp;

    if(gst_rtsp_url_parse ("rtsp://localhost:8554/ch0", &url) != GST_RTSP_OK) {
        g_error("gst_rtsp_url_parse failed");;
    }

    media = gst_rtsp_media_factory_construct (factory, url);
    if(!GST_IS_RTSP_MEDIA (media)){
        g_error("GST_IS_RTSP_MEDIA failed");
    }

    pipelineRtsp = gst_pipeline_new ("media-pipeline");
    gst_rtsp_media_take_pipeline (media, GST_PIPELINE_CAST (pipelineRtsp));

        GstStateChangeReturn ret = gst_element_set_state(pipelineRtsp, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing error", _FILE_, __LINE__);
            gst_object_unref(pipelineRtsp);
            return -1;
        }
#endif

#if 0
    GstRTSPUrl *url; 

    if(gst_rtsp_url_parse ("rtsp://localhost:8554/ch0", &url) != GST_RTSP_OK) {
        g_error("gst_rtsp_url_parse failed");
    }
    GstElement *pipelineRtsp = gst_rtsp_media_factory_create_element (factory, url);

    g_timeout_add_seconds (10, (GSourceFunc) captureStart, pipelineRtsp);
#endif

    // gst_rtsp_mount_points_add_factory (mounts, "/ch1", factory1);

    /* don't need the ref to the mounts anymore */
    // g_object_unref (mounts);

    /* attach the server to the default maincontext */
    // if (gst_rtsp_server_attach (server, NULL) == 0) goto failed;

    /* start serving */
    // g_print ("stream ready at rtsp://127.0.0.1:8554/ch0\n");
    __LOG(LOG_NOTICE, "[RTSP][%s:%d]stream ready at rtsp://127.0.0.1:%s%s", _FILE_, __LINE__, RTSP_PORT, point);
    // GgstLoop *loop = g_main_loop_new (NULL, FALSE);
    // g_main_loop_run (loop);

    //__LOG(LOG_CRIT, "[GST][%s:%d] thread exit", _FILE_, __LINE__);
    // g_main_loop_unref(loop);
    // g_object_unref(server);
    // g_object_unref(factory);
    g_free(point);
    g_free(srcName);

    return GST_FLOW_OK;

}

void cleanup() {
    //g_message("Cleaning up GStreamer...");
    __LOG(LOG_NOTICE, "[GST][%s:%d] Cleaning up GStreamer", _FILE_, __LINE__);
    gst_deinit();  // GStreamer 해제
}

int rtsp_server_start()
{
    __LOG(LOG_NOTICE, "[RTSP][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
    rtspServer = gst_rtsp_server_new ();
    g_object_set (rtspServer, "service", RTSP_PORT, NULL);

    rtspMounts = gst_rtsp_server_get_mount_points (rtspServer);

    //g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);

    g_signal_connect(rtspServer, "client-connected", G_CALLBACK(handle_client_connected), NULL);
    
    //g_signal_connect(rtspServer, "client-disconnected", G_CALLBACK(handle_client_disconnected), NULL);

    

#ifdef RTSP_AUTH_ENABLE
    /* make a new authentication manager */
    GstRTSPAuth *auth = gst_rtsp_auth_new ();

    /* make user token */
    GstRTSPToken *token =
        gst_rtsp_token_new (GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING,
        "semes", NULL);
    gchar *basic = gst_rtsp_auth_make_basic ("semes", "semes");
    gst_rtsp_auth_add_basic (auth, basic, token);
    g_free (basic);
    gst_rtsp_token_unref (token);
    gst_rtsp_server_set_auth (rtspServer, auth);
    g_object_unref (auth);
    //g_timeout_add_seconds (2, (GSourceFunc) timeout, rtspServer);
#endif

    if (gst_rtsp_server_attach (rtspServer, NULL) == 0) {
        __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp server attach failed", _FILE_, __LINE__);
        return -1;
    }

    return 1;
}

static void splitTimerStart(gpointer data, gint startSec)
{
    CustomData *customData = (CustomData *)data;
    static gboolean timer_flag = 0;
    static gint staticMin = -1;

    if(timer_flag == 1) return;
    
    GDateTime *datetime = g_date_time_new_now_local();
    gint min = g_date_time_get_minute(datetime);
    gint sec = g_date_time_get_second(datetime);
    //gint microsec = g_date_time_get_microsecond(datetime);
    guint i;
    guint idx;

    //g_print("sec:%d microsec:%d\n", sec, microsec);
    //__LOG(LOG_DEBUG, "[GST][%s:%d] sec:%d microsec:%d", _FILE_, __LINE__, sec, microsec);

    //if(sec == 0 && microsec <= 10000)
    if(staticMin == min || sec != startSec+1) {
        g_date_time_unref(datetime);
        return;
    }

    for (i = 0; i < MAX_CAM; i++)
    {
        idx = FILE0_L + i;
        if (customData[idx].firstSplitFlag == 1)
        {
            staticMin = min;
            splitNow(&customData[idx]);
#ifdef SPLIT_TIMER_ENABLE
            //__LOG(LOG_NOTICE, "[GST][%s:%d] split timer start [%d]", _FILE_, __LINE__, idx);
            //g_timeout_add_seconds(FILE_SAVE_DURATION, (GSourceFunc)splitNow, &customData[idx]);
            //timer_flag = 1;
#endif
        }
    }

    g_date_time_unref(datetime);

    return;
}

static gboolean handle_eos_event(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) 
{
    CustomData *data = (CustomData *)user_data;
    static guint8 count = 0;
    GstEventType event = GST_EVENT_TYPE(GST_PAD_PROBE_INFO_DATA(info));
    const gchar *eventName = gst_event_type_get_name(event);
    
    // Print the event name
    //__LOG(LOG_NOTICE, "[GST][%s:%d] ch : %d, Event Type : %s", _FILE_, __LINE__, data->ch, eventName);

    if (event == GST_EVENT_EOS) {
        g_print("Received EOS event on pad: %s\n", GST_PAD_NAME(pad));
        // Add your EOS event handling code here
        __LOG(LOG_NOTICE, "[GST][%s:%d] %s ch[%d] count[%d]", _FILE_, __LINE__, __FUNCTION__, data->ch, count);
        data->is_live = FALSE;
        //splitNow(data);
        //if(++count > 1) destroy();
    }
    else if(event == GST_EVENT_TAG)
    {

    }
    else if(event == GST_EVENT_STREAM_START)
    {
        __LOG(LOG_NOTICE, "[GST][%s:%d] GST_EVENT_STREAM_START ch[%d]", _FILE_, __LINE__, data->ch);
        //__LOG(LOG_NOTICE, "[GST][%s:%d] audio queue flush", _FILE_, __LINE__);
        if(count==0)
        {
#if 0
            GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS; 
            __LOG(LOG_NOTICE, "[GST][%s:%d] pipe state change READY[%d]", _FILE_, __LINE__, data->ch);
            ret = gst_element_set_state(data->audioQueue, GST_STATE_READY);
            //ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
            if (ret == GST_STATE_CHANGE_FAILURE) {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing error", _FILE_, __LINE__);
                gst_object_unref(pipeline);
                return -1;
            } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
                //customData->is_live = TRUE;
            }
            __LOG(LOG_NOTICE, "[GST][%s:%d] pipe state change PLAYING[%d]", _FILE_, __LINE__, data->ch);
            ret = gst_element_sync_state_with_parent(data->audioQueue);
            //ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
            if (ret == GST_STATE_CHANGE_FAILURE) {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing error", _FILE_, __LINE__);
                gst_object_unref(pipeline);
                return -1;
            } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
                //customData->is_live = TRUE;
            }
#endif
        }
        count++;
    }
    else
    {
        g_print("Event Type: %s\n", eventName);
    }

    return GST_PAD_PROBE_OK;
}

static void taskLoop(gpointer data)
{
    CustomData *customData = (CustomData *)data;
#ifdef FILENAME_SEC_ZERO
    //change_file_datetime();
#endif


#if defined(RECORD_ENABLE) && defined(RECORD_SPLITMUXSINK_ENABLE)
    splitTimerStart(customData, 0);
#endif

    usleep(1000);
#if 0
    static guint16 msec_timer = 0;
    static guint16 sec_timer = 0;
    static GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
    __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing wait..(%d)", _FILE_, __LINE__, sec_timer);
    if(ret == GST_STATE_CHANGE_FAILURE)
    {
        msec_timer++;
        if(msec_timer >= 1000)
        {
            sec_timer++;
            __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing wait..(%d)", _FILE_, __LINE__, sec_timer);
            msec_timer = 0;
            if(sec_timer >= 20)
            {
                ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
                if (ret == GST_STATE_CHANGE_FAILURE) {
                    __LOG(LOG_CRIT, "[GST][%s:%d] pipeline playing error", _FILE_, __LINE__);
                    gst_object_unref(pipeline);
                    return -1;
                } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
                    //customData->is_live = TRUE;
                    __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline playing", _FILE_, __LINE__);
                }
            }
        }
    }
#endif

    return;
}

gboolean parseArguments(int argc, char *argv[]) {
    // 인자가 부족한 경우 사용법을 출력하고 프로그램 종료
    if (argc < 1) {
        g_print("Usage: %s <arg1> [arg2] [arg3] ...\n", argv[0]);
        //exit(1);
        return 0;
    }

    // 첫 번째 인자 (프로그램 이름)는 argv[0]에 저장되므로 무시하고 1부터 시작
    for (int i = 1; i < argc; i++) {
        // argv[i]에 저장된 인자를 출력 또는 원하는 작업 수행
        g_print("Argument %d: %s\n", i, argv[i]);
    }

    return 1;
}

gint setAudioPipe(gpointer mainPipe, gpointer data)
{
    GstElement *pipeline = (GstElement *)mainPipe;
    AudioPipe *pipe = (AudioPipe *)data;
    guint8 queue_leaky = LEAKY_DOWNSTREAM;

    __LOG(LOG_NOTICE, "[AUDIO][%s:%d] audio enable", _FILE_, __LINE__);

    pipe->src = gst_element_factory_make("audiotestsrc", "audio-src");
    pipe->convert = gst_element_factory_make("audioconvert", "audio-convert");
    pipe->resample = gst_element_factory_make("audioresample", "audio-resample");
    pipe->audiorate = gst_element_factory_make("audiorate", "audio-rate");
    pipe->encoder = gst_element_factory_make("lamemp3enc", "audio-enc");
    pipe->parse = gst_element_factory_make("mpegaudioparse", "audio-parse");
    pipe->queue = gst_element_factory_make("queue", "audio-queue");
    pipe->queue2 = gst_element_factory_make("queue", "audio-queue2");
    pipe->tee = gst_element_factory_make("tee", "audio-tee");
    if (!pipe->src || !pipe->convert || !pipe->resample || !pipe->audiorate || !pipe->encoder || !pipe->parse || !pipe->queue || !pipe->queue2 || !pipe->tee)
    {
        __LOG(LOG_CRIT, "[AUDIO][%s:%d] audio pipe create error", _FILE_, __LINE__);
        // gst_object_unref(pipeline[i]);
        return -1;
    }
    g_object_set(pipe->src, "do-timestamp", TRUE, NULL);
    g_object_set(pipe->src, "is-live", TRUE, NULL);
    g_object_set(pipe->src, "wave", 5, NULL);
    g_object_set(pipe->src, "tick-interval", 200000000, NULL);
    //g_object_set(pipe->encoder, "perfect-timestamp", TRUE, NULL);
    // g_object_set(audioPipe.src, "apply-tick-ramp", TRUE, NULL);
    // g_object_set(audioPipe.queue, "max-size-buffers", 60, NULL);
    g_object_set(pipe->queue, "max-size-bytes", 0, "max-size-time", 0, "max-size-buffers", 60, "leaky", queue_leaky, NULL);
    //g_object_set(pipe->queue, "max-size-time", 5 * GST_SECOND, "max-size-buffers", 10, "leaky", 1, NULL);
    //g_object_set(pipe->queue2, "max-size-time", 5 * GST_SECOND, "max-size-buffers", 10, "leaky", 1, NULL);

    gst_bin_add_many(GST_BIN(pipeline), pipe->src, pipe->convert, pipe->resample, pipe->audiorate, pipe->encoder, pipe->parse, pipe->queue, pipe->queue2, pipe->tee, NULL);
    if (!gst_element_link_many(pipe->src, pipe->convert, pipe->audiorate, pipe->encoder, pipe->parse, pipe->queue, pipe->tee, NULL))
    {
        __LOG(LOG_CRIT, "[AUDIO][%s:%d] audio ,main pipe link error", _FILE_, __LINE__);
        // gst_object_unref(pipeline[i]);
        return -1;
    }

    return 0;
}

gint setCapturePipe(gpointer mainPipe, gpointer data, gpointer user_data, guint8 idx)
{
    GstElement *pipeline = (GstElement *)mainPipe;
    VideoPipe *pipe = (VideoPipe *)data;
    CustomData *info = (CustomData *)user_data;
    guint8 ch_num = idx % 4;
    guint8 queue_leaky = LEAKY_UPSTREAM;

    __LOG(LOG_NOTICE, "[CAPTURE][%s:%d] capture enable [%d]", _FILE_, __LINE__, ch_num);

    pipe->crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", idx));
    pipe->encoder = gst_element_factory_make("jpegenc", g_strdup_printf("jpegenc%d", idx));
    pipe->queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", idx));
    pipe->sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", idx));

    if (!pipe->crop || !pipe->encoder || !pipe->queue || !pipe->sink)
    {
        __LOG(LOG_CRIT, "[CAPTURE][%s:%d] capture pipe[%d] create error", _FILE_, __LINE__, ch_num);
        // gst_object_unref(pipeline[i]);
        return -1;
    }

    gst_bin_add_many(GST_BIN(pipeline), pipe->crop, pipe->queue, pipe->encoder, pipe->sink, NULL);

    if (!gst_element_link_many(pipe->queue, pipe->crop, pipe->encoder, pipe->sink, NULL))
    {
        __LOG(LOG_CRIT, "[CAPTURE][%s:%d] capture pipe[%d] link error", _FILE_, __LINE__, ch_num);
        // gst_object_unref(pipeline[i]);
        return -1;
    }

    if (ch_num % 2 == 0)
        g_object_set(pipe->crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
    else
        g_object_set(pipe->crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

    g_object_set(pipe->queue, "max-size-time", 0, "max-size-bytes", 0, "max-size-buffers", 60, "leaky", queue_leaky, NULL);
    g_object_set(pipe->sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(pipe->sink, "new-sample", G_CALLBACK(new_sample_handler), info);
    g_signal_connect(pipe->sink, "new-preroll", G_CALLBACK(new_preroll_handler), info);
    info->index = idx;
    info->enc = pipe->encoder;
    info->ch = idx % MAX_CAM;
    info->mode = CAPTURING;
    info->captureMax = CAPTURE_MAX_CNT;
    info->captureCnt = CAPTURE_MAX_CNT;
    // g_timeout_add_seconds(30, (GSourceFunc)captureStart, &customData[tdx]);

    return 0;
}

gint setRecordPipe(GstElement* pipeline, gpointer data, gpointer user_data, guint8 idx)
{
    //GstElement *pipeline = (GstElement *)mainPipe;
    VideoPipe *pipe = (VideoPipe *)data;
    CustomData *info = (CustomData *)user_data;
    guint8 ch_num = idx % 4;
    guint8 queue_leaky = LEAKY_UPSTREAM;

    __LOG(LOG_NOTICE, "[RECORD][%s:%d] record enable [%d]", _FILE_, __LINE__, ch_num);

    pipe->videorate = gst_element_factory_make("videorate", g_strdup_printf("videorate%d", idx));
    pipe->crop = gst_element_factory_make("videocrop", g_strdup_printf("videocrop%d", idx));
    pipe->encoder = gst_element_factory_make("vpuenc_h264", g_strdup_printf("vpuenc_h264%d", idx));
    pipe->queue = gst_element_factory_make("queue", g_strdup_printf("queue%d", idx));
    pipe->queue2 = gst_element_factory_make("queue2", g_strdup_printf("queue2_%d", idx));
    pipe->parse = gst_element_factory_make("h264parse", g_strdup_printf("parse%d", idx));
    pipe->capsfilter = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter%d", idx));
    pipe->convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("videoconvert%d", idx));
#ifdef RECORD_SPLITMUXSINK_ENABLE
    pipe->sink = gst_element_factory_make("splitmuxsink", g_strdup_printf("splitmuxsink%d", idx));
#else
    pipe->sink = gst_element_factory_make("appsink", g_strdup_printf("appsink%d", idx));
#endif

    if (!pipe->videorate || !pipe->crop|| !pipe->encoder || !pipe->queue || !pipe->parse || !pipe->queue2 || !pipe->sink || !pipe->capsfilter || !pipe->convert)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] make error", _FILE_, __LINE__, ch_num);
        // gst_object_unref(pipeline[i]);
        return -1;
    }
    gst_bin_add_many(GST_BIN(pipeline), pipe->crop, pipe->queue, pipe->videorate, pipe->encoder, pipe->parse, pipe->sink, pipe->capsfilter, pipe->convert, NULL);

#ifdef OVERLAY_ENABLE

#ifdef TIMEOVERLAY
    pipe->overlay = gst_element_factory_make("timeoverlay", g_strdup_printf("overlay%d", idx));
    g_object_set(pipe->overlay, "datetime-format", "%Y-%m-%d %H:%M:%S", NULL);
    g_object_set(pipe->overlay, "show-times-as-dates", TRUE, NULL);
    g_object_set(pipe->overlay, "datetime-epoch", g_date_time_new_now_local(), NULL);
#else
    pipe->overlay = gst_element_factory_make("textoverlay", g_strdup_printf("overlay%d", idx));
#endif
    g_object_set(pipe->overlay, "valignment", 2, NULL);
    g_object_set(pipe->overlay, "halignment", 0, NULL);
    g_object_set(pipe->overlay, "font-desc", "Times New Roman Italic, 10", NULL);

    if(!pipe->overlay)
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video pipe overlay[%d] make error", _FILE_, __LINE__, ch_num);
        return -1;
    }
    
    if(!gst_bin_add(GST_BIN(pipeline), pipe->overlay))
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video pipe overlay[%d] add error", _FILE_, __LINE__, ch_num);
        return -1;
    }

    if (!gst_element_link_many(pipe->queue, pipe->crop, pipe->overlay, pipe->videorate, pipe->encoder, pipe->parse, NULL))
#else
    if (!gst_element_link_many(pipe->queue, pipe->crop, pipe->videorate, pipe->capsfilter, pipe->convert, pipe->encoder, pipe->parse, NULL))
#endif
    {
        __LOG(LOG_CRIT, "[GST][%s:%d] video pipe[%d] link error", _FILE_, __LINE__, ch_num);
        // gst_object_unref(pipeline[i]);
        return -1;
    }

    // g_object_set(main[i].videoPipe[idx].mux, "faststart", TRUE, NULL);

    GstCaps *caps = gst_caps_new_simple("video/x-raw", "framerate", GST_TYPE_FRACTION, FILE_FPS, 1, NULL);
    g_object_set(pipe->capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    if (ch_num % 2 == 0)
        g_object_set(pipe->crop, "top", 0, "bottom", 0, "left", _WIDTH, "right", 0, NULL);
    else
        g_object_set(pipe->crop, "top", 0, "bottom", 0, "left", 0, "right", _WIDTH, NULL);

    g_object_set(pipe->parse, "config-interval", -1, NULL);
    //g_object_set(pipe->videorate, "max-rate", FILE_FPS, NULL);
    //g_object_set(pipe->videorate, "drop-only", TRUE, NULL);
    g_object_set(pipe->encoder, "bitrate", FILE_BITRATE, NULL);
    g_object_set(pipe->queue, "max-size-time", 0, "max-size-bytes", 0, "max-size-buffers", 60, "leaky", queue_leaky, NULL);
    //g_object_set(pipe->queue, "max-size-buffers", 60, "leaky", queue_leaky, NULL);
    //g_object_set(pipe->queue, "max-size-time", 5 * GST_SECOND, NULL);
    //g_object_set(pipe->queue, "flush-on-eos", TRUE, NULL);
#if 0
    g_signal_connect(pipe->queue, "underrun", G_CALLBACK(underrun_callback), NULL);
    g_signal_connect(pipe->queue, "overrun", G_CALLBACK(overrun_callback), NULL);
    g_signal_connect(pipe->queue, "running", G_CALLBACK(running_callback), NULL);
    g_signal_connect(pipe->queue, "pushing", G_CALLBACK(pushing_callback), NULL);
#endif

#ifdef RECORD_SPLITMUXSINK_ENABLE
    //g_object_set(main[i].videoPipe[idx].sink, "async-finalize", TRUE, NULL);
    //g_object_set(main[i].videoPipe[idx].sink, "async-handling", TRUE, NULL);
    //g_object_set(main[i].videoPipe[idx].sink, "send-keyframe-requests", TRUE, NULL);
    //g_object_set(main[i].videoPipe[idx].sink, "use-robust-muxing", TRUE, NULL);
    //g_object_set(pipe->sink, "max-size-timecode", "00:01:00:00", NULL);
    //g_object_set(pipe->sink, "max-size-time", 0, NULL);
    g_signal_connect(pipe->sink, "format-location", G_CALLBACK(format_location), info);
    g_signal_connect(pipe->sink, "sink-added", G_CALLBACK(sink_added), info);
    //g_signal_connect(main[i].videoPipe[idx].sink, "muxer-added", G_CALLBACK(muxer_added), &customData[tdx]);
#else
    g_object_set(pipe->sink, "max-buffers", 60, NULL);
    g_object_set(pipe->sink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(pipe->sink, "eos", G_CALLBACK(eos_callback), info);
    g_signal_connect(pipe->sink, "new-sample", G_CALLBACK(new_sample_handler), info);
    g_signal_connect(pipe->sink, "new-preroll", G_CALLBACK(new_preroll_handler), info);
#endif

    info->index = idx;
    info->ch = ch_num;
    info->enc = pipe->encoder;
    info->vr = pipe->videorate;
    info->timeoveraly = pipe->overlay;
    info->mode = RECORDING;
    info->file_name = NULL;
    info->appsink = pipe->sink;
    info->firstSplitFlag = 0;
    info->is_live = TRUE;
    info->pipeline = pipeline;
    // g_object_set(main[i].videoPipe[idx].sink, "location", g_strdup_printf("%s", get_filename(&customData[tdx])), NULL);

#ifdef RECORD_SPLITMUXSINK_ENABLE
    video_sink_pad[info->ch] = gst_element_get_request_pad(pipe->sink, "video_aux_%u");
    gst_pad_add_probe(video_sink_pad[info->ch], GST_PAD_PROBE_TYPE_EVENT_BOTH, handle_eos_event, info, NULL);
    if (gst_pad_link(gst_element_get_static_pad(pipe->parse, "src"), video_sink_pad[info->ch]) != GST_PAD_LINK_OK)
    {
        __LOG(LOG_CRIT, "[RECORD][%s:%d] video pad[%d] link error", _FILE_, __LINE__, ch_num);
    }
#else
    if (!gst_element_link(pipe->parse, pipe->sink))
    {
        __LOG(LOG_CRIT, "[RECORD][%s:%d] video pipe[%d] link error", _FILE_, __LINE__, ch_num);
        // gst_object_unref(pipeline[i]);
        return -1;
    }
#endif

    return 0;
}

gint main(int argc, char *argv[]) 
{
    MainPipe main[MAX_SRC];
    CustomData customData[MAX_ENC*MAX_SRC];
    guint8 idx = 0, src_num, dir;
    GstCaps *caps;
    GstStateChangeReturn ret;
    gint i = 0 ,init_sec = 0;
    guint cleanRtsp_id;
    gint main_fps = MAIN_FPS;
    gboolean rtsp_enable = TRUE;

    atexit(cleanup);
    attachInterruptHandlers();
    if(argc > 1)
    {
        //parseArguments(argc, argv);
        init_sec = atoi(argv[1]);
        __LOG(LOG_NOTICE, "[GST][%s:%d] init_sec : %d", _FILE_, __LINE__, init_sec);
        if(argc > 2)
        {
            main_fps = atoi(argv[2]);
            __LOG(LOG_NOTICE, "[GST][%s:%d] main_fps : %d", _FILE_, __LINE__, main_fps);
            if(argc > 3)
            {
                rtsp_enable = atoi(argv[3]);
                __LOG(LOG_NOTICE, "[GST][%s:%d] rtsp_enable : %s", _FILE_, __LINE__, rtsp_enable? "TRUE":"FALSE");
            }
        }
    }



    gst_init(&argc, &argv);

    ohtName = g_strdup_printf("%s", "output");

    // 파이프라인 생성
    gint start_index = 0;
    gint end_index = MAX_SRC;
    //pipeline = gst_pipeline_new("pipeline");
    //i = 1;
    for(i=start_index; i<end_index; i++)
    {
        //guint8 queue_leaky = LEAKY_DOWNSTREAM;

        __LOG(LOG_NOTICE, "[GST][%s:%d] Main pipeline(%d) create", _FILE_, __LINE__, i);
        main[i].pipeline = gst_pipeline_new( g_strdup_printf("pipeline_%d", i));

        main[i].src = gst_element_factory_make("v4l2src", g_strdup_printf("src_%d", i));
        //GstElement *src = gst_element_factory_make("videotestsrc", "src");
        main[i].convert = gst_element_factory_make("imxvideoconvert_g2d", g_strdup_printf("convert_%d", i));
        main[i].capsfilter = gst_element_factory_make("capsfilter", g_strdup_printf("capsfilter_%d", i));
        main[i].tee = gst_element_factory_make("tee", g_strdup_printf("tee_%d", i));
        main[i].queue = gst_element_factory_make("queue", g_strdup_printf("queue_%d", i));

        //g_object_set(main[i].src, "io-mode", 4, NULL);
        //g_object_set(main[i].src, "do-timestamp", TRUE, NULL);
        if(i == 0) g_object_set(main[i].src, "device", "/dev/video4", NULL);
        if(i == 1) g_object_set(main[i].src, "device", "/dev/video3", NULL);
        //g_object_set(main[i].queue, "max-size-time", 100*GST_MSECOND, "max-size-bytes", 0, "max-size-buffers", 60, "leaky", LEAKY_DOWNSTREAM, NULL);
        //g_object_set(main[i].queue, "max-size-buffers", 60, "leaky", queue_leaky, NULL);
        //g_object_set(main[i].queue, "max-size-time", 5 * GST_SECOND, NULL);
        //g_object_set(main[i].queue, "flush-on-eos", TRUE, NULL);
#if 0
        g_signal_connect(main[i].queue, "underrun", G_CALLBACK(underrun_callback), NULL);
        g_signal_connect(main[i].queue, "overrun", G_CALLBACK(overrun_callback), NULL);
        g_signal_connect(main[i].queue, "running", G_CALLBACK(running_callback), NULL);
        g_signal_connect(main[i].queue, "pushing", G_CALLBACK(pushing_callback), NULL);
#endif
        __LOG(LOG_NOTICE, "[GST][%s:%d] src[%d] : /dev/video%d", _FILE_, __LINE__, i, i+3);
        //g_object_set(main[i].src, "device", "/dev/video3", NULL);

        caps = gst_caps_new_simple("video/x-raw",
                                            "format", G_TYPE_STRING, "NV12",
                                            "width", G_TYPE_INT, _WIDTH*2,
                                            "height", G_TYPE_INT, _HEIGHT,
                                            "framerate", GST_TYPE_FRACTION, main_fps, 1,
                                            NULL);


        g_object_set(main[i].capsfilter, "caps", caps, NULL);
        gst_caps_unref(caps);
        customData[0].capsfilter = main[i].capsfilter;

        if (!main[i].pipeline || !main[i].src || !main[i].capsfilter || !main[i].tee || ! main[i].queue)
        {
            __LOG(LOG_CRIT, "[GST][%s:%d] Main pipe create error", _FILE_, __LINE__);
            return -1;
        }

        // 요소가 생성되지 않은 경우 에러 처리

        // 파이프라인에 요소 추가
        gst_bin_add_many(GST_BIN(main[i].pipeline), main[i].src, main[i].capsfilter, main[i].convert, main[i].tee, NULL);

        __LOG(LOG_NOTICE, "[GST][%s:%d] Main pipe link start", _FILE_, __LINE__);
        // 요소 연결
#ifdef FHD_ENABLE
        if (!gst_element_link_many(main[i].src, main[i].capsfilter, main[i].convert, main[i].tee, NULL)) {
            __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, i);
            gst_object_unref(main[i].pipeline);
            return -1;
        }
#else
        if (!gst_element_link_many(main[i].src, main[i].capsfilter, main[i].tee, NULL)) {
            __LOG(LOG_CRIT, "[GST][%s:%d] pipe[%d] link error", _FILE_, __LINE__, i);
            gst_object_unref(main[i].pipeline);
            return -1;
        }
#endif
    }

#ifdef RECORD_ENABLE
    for (i = start_index*2; i < end_index*2; i++)
    {
        idx = FILE0_L + i;
        src_num = i / MAX_SRC;
        //dir = idx % MAX_SRC;
        dir = FILE0_L / 2 + i % 2;
        __LOG(LOG_NOTICE, "[RECORD][%s:%d] idx:%d ch:%d src_num:%d dir:%d",  _FILE_, __LINE__, idx, i, src_num, dir);
        setRecordPipe(main[src_num].pipeline, &main[src_num].videoPipe[dir], &customData[idx], idx);
        if (!gst_element_link(main[src_num].tee, main[src_num].videoPipe[dir].queue))
        {
            __LOG(LOG_CRIT, "[RECORD][%s:%d] record pipe[%d] link error", _FILE_, __LINE__, i);
        }
    }
#endif

#ifdef AUDIO_ENABLE
    do
    {
        AudioPipe audioPipe;
        setAudioPipe(&audioPipe);

        for (i = start_index; i < end_index*2; i++)
        {
            idx = FILE0_L + i;
            src_num = i / MAX_SRC;
            dir = FILE0_L / MAX_SRC + i % MAX_SRC;
            //__LOG(LOG_NOTICE, "[AUDIO][%s:%d] idx:%d ch:%d src_num:%d dir:%d",  _FILE_, __LINE__, idx, i, src_num, dir);
            // g_object_set(main[i].videoPipe[idx].sink, "async-finalize", TRUE, NULL);
            // g_object_set(main[i].videoPipe[idx].sink, "async-handling", TRUE, NULL);
            audio_sink_pad[customData[idx].ch] = gst_element_get_request_pad(main[src_num].videoPipe[dir].sink, "audio_%u");
            if (gst_pad_link(gst_element_get_request_pad(audioPipe.tee, "src_%u"), audio_sink_pad[customData[idx].ch]) != GST_PAD_LINK_OK)
            {
                __LOG(LOG_CRIT, "[AUDIO][%s:%d] audio pad[%d] link error", _FILE_, __LINE__, i);
            }
        }
    } while (0);
#endif

#ifdef CAPTURE_ENABLE
    for (i = start_index; i < end_index * 2; i++)
    {
        idx = CAPTURE0_L + i;
        src_num = i / MAX_SRC;
        dir = CAPTURE0_L / MAX_SRC + i % MAX_SRC;
        __LOG(LOG_NOTICE, "[CAPTURE][%s:%d] idx:%d ch:%d src_num:%d dir:%d",  _FILE_, __LINE__, idx, ch_num, i, dir);
        setCapturePipe(&main[src_num].videoPipe[dir], &customData[idx], idx);
        // g_timeout_add_seconds(1, (GSourceFunc)setSRT, &customData[tdx]);
        if (!gst_element_link(main[src_num].tee, main[src_num].videoPipe[dir].queue))
        {
            __LOG(LOG_CRIT, "[CAPTURE][%s:%d] capture pipe[%d] link error", _FILE_, __LINE__, i);
        }
    }
#endif

    if(rtsp_enable)
    {
        if (rtsp_server_start())
        {
            for (i = start_index*2; i < end_index*2; i++)
            {
                idx = RTSP0_L + i;
                src_num = i / MAX_SRC;
                dir = RTSP0_L / MAX_SRC + i % MAX_SRC;
                __LOG(LOG_NOTICE, "[RTSP][%s:%d] idx:%d ch:%d src_num:%d dir:%d",  _FILE_, __LINE__, idx, i, src_num, dir);
                setRtspPipe(main[src_num].pipeline, &main[src_num].videoPipe[dir], &customData[idx], idx);
                // g_timeout_add_seconds(1, (GSourceFunc)setSRT, &customData[tdx]);
                if (!gst_element_link(main[src_num].tee, main[src_num].videoPipe[dir].queue))
                {
                    __LOG(LOG_CRIT, "[RTSP][%s:%d] rtsp pipe[%d] link error", _FILE_, __LINE__, i);
                }
            }
        }
        cleanRtsp_id = g_timeout_add_seconds(5, (GSourceFunc)cleanRtspSession, rtspServer);
    }


#if 0
    ret = gst_element_set_state(pipeline, GST_STATE_READY);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state ready error", _FILE_, __LINE__);
        gst_object_unref(pipeline);
        return -1;
    } else {
        //customData->is_live = TRUE;
        __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline state ready (ret : %d)", _FILE_, __LINE__, ret);
    }

    usleep(200*1000);
#endif

    //guint terminalInput_id = g_timeout_add_seconds(1, (GSourceFunc)check_terminal_input, customData);
#ifdef TERMINAL_CMD_ENABLE
    g_thread_new("terminal-thread", (GThreadFunc)check_terminal_input, customData);
#endif

#ifdef IPC_ENABLE
    g_thread_new("ipc-thread", (GThreadFunc)ipcLoop, customData);
#endif

    for (i = start_index; i < end_index; i++)
    {
        //g_print("i:%d\n", i);
        GstBus *bus = gst_element_get_bus(GST_ELEMENT(main[i].pipeline));
        if(!bus) {
            __LOG(LOG_CRIT, "[GST][%s:%d] bus get error from pipeline", _FILE_, __LINE__);
        }

        gst_bus_add_watch(bus, my_bus_callback, NULL);

        gst_object_unref(bus);
        putenv("GST_DEBUG_DUMP_DOT_DIR=/home/user/jhw/dot/");
        //setenv("GST_DEBUG_DUMP_DOT_DIR", "/home/user/jhw/dot/", 1);
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(main[i].pipeline), GST_DEBUG_GRAPH_SHOW_ALL, gst_element_get_name(main[i].pipeline));
    }

    if(init_sec >= 0)
    {
        for (i = start_index; i < end_index; i++)   //for(i=1; i>=0; i--)
        {
            //sleep(1);
            //__LOG(LOG_NOTICE, "[GST][%s:%d] pipe[%d] paused", _FILE_, __LINE__, i);
            ret = gst_element_set_state(GST_ELEMENT(main[i].pipeline), GST_STATE_PAUSED);

            if (ret == GST_STATE_CHANGE_FAILURE) {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state paused error", _FILE_, __LINE__);
                gst_object_unref(main[i].pipeline);
                return -1;
            } else {
                //customData->is_live = TRUE;
                __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline [%d] state paused (ret : %d)", _FILE_, __LINE__, i, ret);
            }
            sleep(1);
        }

    #ifdef OVERLAY_ENABLE
        //guint srtTimer_id = g_timeout_add_seconds(1, (GSourceFunc)setSRT, customData);
        guint srtTimer_id = g_timeout_add(500, (GSourceFunc)setSRT, customData);
    #endif

        //if(init_sec > 0)
        {
            __LOG(LOG_NOTICE, "[GST][%s:%d] standby wait %d sec for playing", _FILE_, __LINE__, init_sec);
            sleep(init_sec);
            //sleep(5);
        }
        

        for (i = start_index; i < end_index; i++)   //for(i=1; i>=0; i--)
        {
            //sleep(1);
            //__LOG(LOG_NOTICE, "[GST][%s:%d] pipe[%d] playing", _FILE_, __LINE__, i);
            ret = gst_element_set_state(GST_ELEMENT(main[i].pipeline), GST_STATE_PLAYING);

            if (ret == GST_STATE_CHANGE_FAILURE) {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipeline state playing error", _FILE_, __LINE__);
                gst_object_unref(main[i].pipeline);
                return -1;
            } else {
                __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline [%d] state playing (ret : %d)", _FILE_, __LINE__, i, ret);
            }
            //sleep(init_sec);
        }
    }
    gstLoop = g_main_loop_new(NULL, FALSE);

	if(!gstLoop) {
        __LOG(LOG_CRIT, "[GST][%s:%d] Main loop create error", _FILE_, __LINE__);
    } else {
        __LOG(LOG_NOTICE, "[GST][%s:%d] Main loop start", _FILE_, __LINE__);
        //g_main_loop_run(gstLoop);
        while (!is_interrupted)
        {
            g_main_context_iteration(g_main_loop_get_context(gstLoop), FALSE);
            taskLoop(customData);
        }
    }

    __LOG(LOG_NOTICE, "[GST][%s:%d] Main loop end", _FILE_, __LINE__);
    g_main_loop_unref(gstLoop);
    //gst_element_get_state(pipeline, NULL, NULL, 2*GST_SECOND);

    sleep(1);

    for(i=start_index; i<end_index; i++)
    {
        if(main[i].pipeline)
        {
            ret = gst_element_set_state(main[i].pipeline, GST_STATE_NULL);
            if (ret == GST_STATE_CHANGE_FAILURE) {
                __LOG(LOG_CRIT, "[GST][%s:%d] pipeline [%d] state null error", _FILE_, __LINE__, i);
                gst_object_unref(main[i].pipeline);
                return -1;
            } else {
                //customData->is_live = TRUE;
                __LOG(LOG_NOTICE, "[GST][%s:%d] pipeline [%d] state null (ret : %d)", _FILE_, __LINE__, i, ret);
            }
            gst_object_unref(main[i].pipeline);
        }
    }

#ifdef RTSP_ENABLE
    g_object_unref(rtspServer);
    g_source_remove(cleanRtsp_id);
#endif

#ifdef OVERLAY_ENABLE
    g_source_remove(srtTimer_id);
#endif
    //g_source_remove(terminalInput_id);
    
    __LOG(LOG_CRIT, "[GST][%s:%d] exit", _FILE_, __LINE__);
    exit(EXIT_SUCCESS);
    

    return 0;
}

#if 0
static void	media_constructed(GstRTSPMediaFactory *factory, GstRTSPMedia *media, gpointer user_data)
{
    PipeMain *info = (PipeMain*)user_data;
	GstElement *element;
    //GstElement *queue;
    gchar *queue_name;
    //info->queue
    __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

    queue_name = g_strdup_printf("%s%d", QUEUE_NAME, info->ch);
    GstElement *queue = gst_bin_get_by_name(GST_BIN(info->pipeline), queue_name);

    g_print("queue_name : %s\n", queue_name);

    if (queue) {
        //gst_element_send_event(queue, gst_event_new_flush_start());
        gst_element_send_event(queue, gst_event_new_flush_stop(TRUE));
        g_print("Queue flushed\n");
    }

    queue_name = g_strdup_printf("%s%d", QUEUE_RTSP_NAME, info->ch);
    queue = gst_bin_get_by_name(GST_BIN(pipeline), queue_name);
    

    //gst_object_unref(queue);
    g_free(queue_name);

    return;
}

static gboolean handle_client_disconnected(GstRTSPServer *server, GstRTSPClient *client, gpointer user_data) {
    // This function is called when a client is disconnected from the RTSP server.
    // You can clean up resources associated with the client session here.
    const gchar *ip_address = gst_rtsp_connection_get_ip(gst_rtsp_client_get_connection(client));

    __LOG(LOG_NOTICE, "[RTSP][%s:%d] Disconnect - IP : %s", _FILE_, __LINE__, ip_address);
    // Clean up session for the disconnected client
    
    return TRUE;
}

// appsink의 "new-sample" 시그널 처리 함수
static GstFlowReturn new_sample_handler_file(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    CustomData *info = (CustomData *)data;
    //GstClockTime timestamp;

    // sample을 가져오기
    //g_signal_emit_by_name(appsink, "pull-sample", &sample);
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample) {
        __LOG(LOG_CRIT, "[GST][%s:%d] sample cannot get from sink", _FILE_, __LINE__);
        return GST_FLOW_ERROR;
    }

    // 버퍼 가져오기
    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        __LOG(LOG_CRIT, "[GST][%s:%d] buffer cannot get from sample", _FILE_, __LINE__);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    //timestamp = gst_clock_get_time(gst_system_clock_obtain());
    //gst_buffer_set_timestamp(buffer, timestamp);
    //GST_BUFFER_PTS(buffer) = timestamp;
    //GST_BUFFER_DTS(buffer) = timestamp;

#if 0
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP(buffer);
    // 녹화 시작 시간을 설정하고, 처음 버퍼를 받았을 때의 타임스탬프를 기준으로 프레임의 타임스탬프를 조정
    static GstClockTime recording_start_time = GST_CLOCK_TIME_NONE;
    if (recording_start_time == GST_CLOCK_TIME_NONE) {
        recording_start_time = timestamp;
    }

    // 녹화 시작 시간을 기준으로 타임스탬프 조정
    timestamp = timestamp - recording_start_time;
    GST_BUFFER_TIMESTAMP(buffer) = timestamp;

    //__LOG(LOG_DEBUG, "[GST][%s:%d] timestamp : %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT "\n", _FILE_, __LINE__, \
                                GST_TIME_ARGS(timestamp), GST_TIME_ARGS(GST_BUFFER_DURATION(buffer)));
    g_print("timestamp : %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(timestamp));
    static GstClockTime prev_timestamp = GST_CLOCK_TIME_NONE;
    static guint frame_counter = 0;
    g_print("frame_counter:%d\n",frame_counter);
    GstClockTime frame_duration = gst_util_uint64_scale_int(1, GST_SECOND, FPS); // 30 fps로 가정
    //g_print("duration : %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(frame_duration));
    //g_print("prev_timestamp: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(prev_timestamp));
    if (prev_timestamp != GST_CLOCK_TIME_NONE) {
        GstClockTime time_difference = timestamp - prev_timestamp;
        g_print("time_difference: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(time_difference));
        guint frames_passed = time_difference / frame_duration;
        frame_counter += frames_passed;
    }
    prev_timestamp = timestamp;
    GST_BUFFER_DTS(buffer) = timestamp;
#endif

#if 0
    static guint64 total_duration = 0;
    GstClockTime duration = GST_BUFFER_DURATION(buffer);
    total_duration += duration;
    if (total_duration >= FILE_SAVE_DURATION * GST_SECOND)
    {
        //change_file_name(info);
        info->file_name = g_strdup_printf("%s", info->file_name_tmp);
        //__LOG(LOG_NOTICE, "[GST][%s:%d] file_name : %s", _FILE_, __LINE__, info->file_name);
        //__LOG(LOG_NOTICE, "[GST][%s:%d] total_duration : %" GST_TIME_FORMAT "\n", _FILE_, __LINE__, GST_TIME_ARGS(total_duration));
        total_duration = 0;
        //GST_BUFFER_TIMESTAMP(buffer) = GST_CLOCK_TIME_NONE;
    }
#endif
    // 파일로 저장
    GstMapInfo map;
    gchar *path;
    gst_buffer_map(buffer, &map, GST_MAP_READ);
    //g_print("file_name:%s\n", info->file_name);

    path = g_strdup_printf("%s%s", FILE_PATH, info->file_name);

    //g_print("path : %s\n", path);
    FILE *file = fopen(path, "ab");
    if (file) {
        fwrite(map.data, 1, map.size, file);
        fclose(file);
    } else {
        __LOG(LOG_ERR, "[GST][%s:%d] %s file open error", _FILE_, __LINE__, info->file_name);
    }
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    g_free(path);

    return GST_FLOW_OK;
}

static GStaticMutex gmutex = G_STATIC_MUTEX_INIT; // Static mutex for synchronization

static void process_buffer(gpointer userData) {
    CustomData *info = (CustomData *)userData;
    // Perform data processing on the buffer
    //g_print("process_buffer\n");
    gst_app_src_push_buffer(GST_APP_SRC(info->appsrc), info->buf);
}

static void process_data_thread(gpointer userData) {
    CustomData *info = (CustomData *)userData;
    // Acquire the mutex before processing the buffer
    //g_static_mutex_lock(&gmutex);
    //g_print("process_data_thread\n");
    process_buffer(info);

    // Release the mutex after processing
    //g_static_mutex_unlock(&gmutex);
}

#endif