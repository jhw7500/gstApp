/*
 *
 * Cantops util.cpp support
 *
 * Copyright (C)2023 Cantops, Inc. All rights reserved.
 *
 * Author:
 *   jhw <hwjo@cantops.biz>, 2023/09/18
 *
 * Description:
 */

#include "util.h"
#include <glib-unix.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

GstElement *pipeline = NULL;
GMainLoop *loop = NULL;
volatile sig_atomic_t is_interrupted = 0;
gboolean is_live = FALSE;
CmdArg cmdArg;

static gboolean quiet = FALSE;
extern volatile gboolean glib_on_error_halt;
guint signal_watch_intr_id = 0;
guint signal_watch_hup_id = 0;
guint signal_watch_term_id = 0;
guint signal_watch_kill_id = 0;
gboolean ch_en_array[MAX_CHANNEL] = { TRUE, TRUE, TRUE, TRUE };

static void fault_restore (void);
static void fault_spin (void);

static void term_handler(int signum) {
    //g_print("Received SIGTERM, sending EOS to pipeline...\n");
    __LOG(LOG_EMERG, "[CFG][%s:%d] Received SIGTERM, sending EOS to pipeline...", _FILE_, __LINE__);
    gst_element_send_event(pipeline, gst_event_new_eos());
    g_main_loop_quit(loop);

    //sleep(5);
    is_interrupted = TRUE;
}

static void kill_handler(int signum) {
    //g_print("Received SIGKILL, sending EOS to pipeline...\n");
    __LOG(LOG_EMERG, "[CFG][%s:%d] Received SIGKILL, sending EOS to pipeline...", _FILE_, __LINE__);
    gst_element_send_event(pipeline, gst_event_new_eos());
    g_main_loop_quit(loop);

    //sleep(5);
    is_interrupted = TRUE;
}

static gboolean intr_handler (gpointer user_data)
{
  GstElement *pipeline = (GstElement *) user_data;
  g_print("handling interrupt.\n");
  //__LOG(LOG_EMERG, "[CFG][%s:%d] Received SIGINTR, sending EOS to pipeline...", _FILE_, __LINE__);
  gst_element_send_event(pipeline, gst_event_new_eos());
  g_main_loop_quit(loop);
  /* post an application specific message */
  gst_element_post_message (GST_ELEMENT (pipeline),
      gst_message_new_application (GST_OBJECT (pipeline),
          gst_structure_new ("GstLaunchInterrupt",
              "message", G_TYPE_STRING, "Pipeline interrupted", NULL)));
  /* remove signal handler */
  signal_watch_intr_id = 0;

  //sleep(5);
  is_interrupted = TRUE;

  return G_SOURCE_REMOVE;
}

static gboolean hup_handler (gpointer user_data)
{
  GstElement *pipeline = (GstElement *) user_data;

  __LOG(LOG_EMERG, "[CFG][%s:%d] Received SIGHUP, sending EOS to pipeline...", _FILE_, __LINE__);

  if (g_getenv ("GST_DEBUG_DUMP_DOT_DIR") != NULL) {
    g_print("SIGHUP: dumping dot file snapshot ...\n");
  } else {
    g_print("SIGHUP: not dumping dot file snapshot, GST_DEBUG_DUMP_DOT_DIR "
        "environment variable not set.\n");
  }
  /* dump graph on hup */
  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-launch.snapshot");

  gst_element_send_event(pipeline, gst_event_new_eos());

  return G_SOURCE_CONTINUE;
}

static void fault_handler_sighandler (int signum)
{
  fault_restore ();
  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (signum) {
    case SIGSEGV:
      fprintf (stderr, "Caught SIGSEGV\n");
      __LOG(LOG_CRIT, "[GST][%s:%d] Caught SIGSEGV", _FILE_, __LINE__);
      break;
    case SIGQUIT:
      if (!quiet) {
        printf ("Caught SIGQUIT\n");
        __LOG(LOG_CRIT, "[GST][%s:%d] Caught SIGQUIT", _FILE_, __LINE__);
      }
      break;
    default:
      fprintf (stderr, "signo:  %d\n", signum);
      __LOG(LOG_CRIT, "[GST][%s:%d] signo %d", _FILE_, __LINE__, signum);
      break;
  }
  fault_spin ();
}

static void fault_spin (void)
{
  int spinning = TRUE;
  glib_on_error_halt = FALSE;
  g_on_error_stack_trace (__FILE__ GST_API_VERSION);
  wait (NULL);
  /* FIXME how do we know if we were run by libtool? */
  fprintf (stderr,
      "Spinning.  Please run 'gdb gstApp " GST_API_VERSION " %d' to "
      "continue debugging, Ctrl-C to quit, or Ctrl-\\ to dump core.\n",
      (gint) getpid ());
  
  __LOG(LOG_CRIT, "[GST][%s:%d] Spinning.  Please run 'gdb gstApp " GST_API_VERSION " %d' to\
 continue debugging, Ctrl-C to quit, or Ctrl-\\ to dump core.", _FILE_, __LINE__, (gint) getpid ());

  while (spinning)
    g_usleep (1000000);
}

static void fault_restore (void)
{
  struct sigaction action;
  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;
  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}

void fault_setup (void)
{
  struct sigaction action;

  __LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);

  memset (&action, 0, sizeof (action));
  action.sa_handler = fault_handler_sighandler;
  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}

void sigHandler(int sig)
{
  static guint8 cnt = 0;
  __LOG(LOG_EMERG, "[GST][%s:%d] sigHandler(%d)", _FILE_, __LINE__, sig);
  //is_interrupted = TRUE;

  gst_element_send_event(pipeline, gst_event_new_eos());
  //sleep(1);

  if(cnt++ > 3)
  {
    g_main_loop_unref(loop);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
  }

#if 0
  for(guint8 i=0; i<MAX_CHANNEL; i++)
  {
    gst_pad_send_event(muxSinkBin[i].getBinVideoSinkPad(), gst_event_new_eos());
#ifdef AUDIOBIN_ENABLE
    gst_pad_send_event(muxSinkBin[i].getBinAudioSinkPad(), gst_event_new_eos());
#endif
  }
#endif
}

void addSignalHandler()
{
  signal_watch_intr_id = g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
  signal_watch_hup_id = g_unix_signal_add (SIGHUP, (GSourceFunc) hup_handler, pipeline);
  //signal_watch_term_id = g_unix_signal_add (SIGTERM, (GSourceFunc) term_handler, pipeline);
  //signal_watch_kill_id = g_unix_signal_add (SIGKILL, (GSourceFunc) kill_handler, pipeline);
}

void removeSignalHandler()
{
  //__LOG(LOG_NOTICE, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  if (signal_watch_intr_id > 0) g_source_remove(signal_watch_intr_id);
  if (signal_watch_hup_id > 0) g_source_remove(signal_watch_hup_id);
  //if (signal_watch_term_id > 0) g_source_remove(signal_watch_term_id);
  //if (signal_watch_kill_id > 0) g_source_remove(signal_watch_kill_id);
}

void attachInterruptHandlers()
{
  //signal(SIGUSR1, ipcHandler);
  //signal(SIGINT, sigHandler);
  signal(SIGKILL, kill_handler);
  signal(SIGTERM, term_handler);
}

void cleanup()
{
  //g_message("Cleaning up GStreamer...");
  __LOG(LOG_INFO, "[GST][%s:%d] %s", _FILE_, __LINE__, __FUNCTION__);
  gst_deinit();  // GStreamer 해제
}

void log_once(gint opt, const gchar *message) 
{
  static gchar lastMessage[256][256];
  static guint8 ptr = 0;

  for(guint8 i=0; i<ptr; i++)
  {
    if (strcmp(message, lastMessage[i]) == 0) {
      return;
    }
  }

  if(opt <= cmdArg.log_level || opt <= LOG_ALERT)
  {
    syslog( opt|LOG_LOCAL0, "%s", message);
  }

  if(opt <= cmdArg.dbg_level || opt <= LOG_ALERT)
  {
    GDateTime *datetime = g_date_time_new_now_local();
    gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");
    gint usec = g_date_time_get_microsecond(datetime);
    const gchar *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
		const gchar *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};

		g_print("%s%s:%03d %s: [%s]\033[0m", color_codes[opt], date_str, usec/1000, PROGRAM_NAME, debug_codes[opt]);
    g_print("%s\n", message);
    g_date_time_unref(datetime);
    g_free(date_str);
  }

    //strncpy(lastMessage[ptr], message, sizeof(lastMessage));
  strncpy(lastMessage[ptr], message, sizeof(lastMessage[ptr]));
  ptr++;
}

void mylog(gint opt, const gchar* _szfmt, ... )
{
	va_list va;
	gchar strTmp[512];

	va_start( va, _szfmt );
	vsprintf(strTmp, _szfmt ,va);

	if(opt <= cmdArg.log_level || opt <= LOG_ALERT) {
		//syslog( opt|LOG_LOCAL0, "  [%5ld.%06ld] [%s]%s", ts.tv_sec, ts.tv_nsec/1000, debug_codes[opt], strTmp);
		syslog( opt|LOG_LOCAL0, "%s", strTmp);
	}
	if(opt <= cmdArg.dbg_level || opt <= LOG_ALERT) {
		//timer = time(NULL);
		//localtime_r(&timer, &t);
    GDateTime *datetime = g_date_time_new_now_local();
    //gchar *date_str = g_date_time_format(datetime, "%Y%m%d_%H%M00");
    gchar *date_str = g_date_time_format(datetime, "%Y-%m-%d %H:%M:%S");
    gint usec = g_date_time_get_microsecond(datetime);

    //gchar *tmp = g_strdup_printf("output_%s.mp4", date_str);
    //memcpy(file_name, tmp, 128);
    const gchar *debug_codes[] = {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"};
		const gchar *color_codes[] = {"\033[1;34m", "\033[0;34m", "\033[1;31m", "\033[1;35m", "\033[1;33m", "\033[1;32m", "\033[1;36m", "\033[0m"};
		g_print("%s%s:%03d %s: [%s]\033[0m", color_codes[opt], date_str, usec/1000, PROGRAM_NAME, debug_codes[opt]);

		vprintf( _szfmt, va );
		printf("\n");
		fflush(stdout);
    g_date_time_unref(datetime);
    g_free(date_str);
	}
	va_end( va );
}

gint charArrayToInt(gchar *arr)
{
    guint8 i;
    gint result = 0;

    if(arr == NULL) return -1;

    for(i=0; (arr[i]!='\n' && arr[i]!='\r' && arr[i]!='\0' && arr[i]!=' '); i++)
    {
      //g_print("arr[%d] : %c %d\n", i, arr[i], arr[i]);
      if(arr[i] < '0' || arr[i] > '9') return -1;

      result = (result * 10) + (arr[i] - '0');
    }

    //g_print("result : %d\n", result);

    return result;
}

gboolean compareBuf(const gchar *cmp1, const gchar *cmp2, guint8 len)
{
	guint8 i;

  if(cmp1 == NULL || cmp2 == NULL) return 0;

	for(i=0;i<len;i++)
	{
		if(cmp1[i] != cmp2[i])
			return 0;
	}
	return 1;
}

GstPadProbeReturn probe_function(GstPad *pad, GstPadProbeInfo *info, gpointer user_data) 
{
  GstElement *data = (GstElement *)user_data;
  GstClockTime timestamp = GST_BUFFER_PTS(info->data);
  gchar *name;
  g_object_get(data, "name", &name, NULL);
  g_message("%s[%s]Timestamp: %" GST_TIME_FORMAT "\n", name, gst_pad_get_direction(pad)==1? "SRC":"SINK", GST_TIME_ARGS(timestamp));

  g_free(name);
  return GST_PAD_PROBE_OK;
}

gboolean print_delay(GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
  // 현재 시간 가져오기
  static GstClockTime prev_time = GST_CLOCK_TIME_NONE;
  GstClockTime current_time = GST_BUFFER_PTS(buffer);

  if (prev_time != GST_CLOCK_TIME_NONE) {
    // 이전 시간이 존재하면 현재 시간과의 차이 계산
    GstClockTimeDiff delay = current_time - prev_time;

    // 지연 출력
    g_print("Delay: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(delay));
  }

  // 이전 시간 업데이트
  prev_time = current_time;

  return TRUE;
}

gchar *search_file(const gchar* path, const gchar* prefix, const gchar* suffix)
{
	FILE *fp;
	static gchar str[128];

  sprintf(str, "ls -ptr %s/%s*%s | grep -v '/$' | grep '\\%s$' | tail -1 | tr -d '\r\n'", path, prefix, suffix, suffix);
  //sprintf(str, "find %s/ -maxdepth 1 -type f -name \"%s*%s\" -printf '%%T+ %%p\\n' | sort -r | head -n 1 | cut -d\" \" -f2- | tr -d '\\r\\n'", path, prefix, suffix);
  fp = popen(str, "r");
  if (NULL == fp)
  {
    perror("popen() fail");
    __LOG(LOG_CRIT, "[CFG][%s:%d] popen fail", _FILE_, __LINE__);
  }
  while (fgets(str, 128, fp));
	__LOG(LOG_INFO, "[CFG][%s:%d] search_json_file : %s", _FILE_, __LINE__, str);
	//Eliminate(str, '\n');

	return str;
}

void print_tag(const GstTagList * list, const gchar * tag, gpointer unused)
{
  gint i, count;

  count = gst_tag_list_get_tag_size (list, tag);

  for (i = 0; i < count; i++) {
    gchar *str;

    if (gst_tag_get_type (tag) == G_TYPE_STRING) {
      if (!gst_tag_list_get_string_index (list, tag, i, &str))
        g_assert_not_reached ();
    } else {
      str = g_strdup_value_contents (gst_tag_list_get_value_index (list, tag, i));
    }

    if (i == 0) {
      g_print ("  %15s: %s\n", gst_tag_get_nick (tag), str);
    } else {
      g_print ("                 : %s\n", str);
    }

    g_free (str);
  }
}

void makeDir(const char* path)
{
    __LOG(LOG_NOTICE, "[CFG][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, path);
    mkdir(path, 0755);
    return;
}

void convert_data_to_hex(const char *data, int len, char *buffer, int buffer_size)
{
    int offset = 0;

    for (int i = 0; i < len; i++) {
        if (offset + 2 >= buffer_size) break; // 버퍼 초과 방지
        offset += snprintf(buffer + offset, buffer_size - offset, "%02x", (unsigned char)data[i]);
    }
    buffer[buffer_size - 1] = '\0'; // Null-terminate
}

int check_sd_mount_flag(void)
{
    FILE *fp;
    char buf[32];

    // 1) 파일 존재 체크
    if (access(SD_MOUNT_FLAG, F_OK) != 0)
        return 0;   // 파일 없음

    // 2) 파일 열기
    fp = fopen(SD_MOUNT_FLAG, "r");
    if (!fp)
        return 0;   // 열기 실패

    // 3) 한 줄 읽기
    if (!fgets(buf, sizeof(buf), fp)) {
        fclose(fp);
        return 0;   // 읽기 실패
    }
    fclose(fp);

    //__LOG(LOG_NOTICE, "[MNT][%s:%d] %s : %s", _FILE_, __LINE__, __FUNCTION__, buf);

    // 5) 값 비교
    if (strstr(buf, "1"))
        return 1;
    if (strstr(buf, "2"))
        return 2;

    return 0;   // 그 외 값
}

//-------------------------------------------------------------------------
// Safe file write - replaces system("echo value > file")
int safe_write_file(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        __LOG(LOG_ERR, "[UTIL][%s:%d] Cannot open file for writing: %s", _FILE_, __LINE__, path);
        return -1;
    }
    if (fprintf(fp, "%s", content) < 0) {
        __LOG(LOG_ERR, "[UTIL][%s:%d] Failed to write to file: %s", _FILE_, __LINE__, path);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

//-------------------------------------------------------------------------
// Safe recursive mkdir - replaces system("mkdir -p path")
int safe_mkdir_p(const char *path, mode_t mode)
{
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    // Remove trailing slash
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    // Create directories one by one
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                __LOG(LOG_ERR, "[UTIL][%s:%d] Failed to create directory: %s", _FILE_, __LINE__, tmp);
                return -1;
            }
            *p = '/';
        }
    }

    // Create final directory
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        __LOG(LOG_ERR, "[UTIL][%s:%d] Failed to create directory: %s", _FILE_, __LINE__, tmp);
        return -1;
    }

    return 0;
}

//-------------------------------------------------------------------------
// Safe I2C command execution - replaces system("i2cwrite/i2cread ...")
// Uses fork/exec instead of system() to avoid shell injection
int safe_exec_i2c(const char *cmd, int bus, int addr, int reg, int value, char *output, size_t output_size)
{
    int pipefd[2];
    pid_t pid;
    int status;

    if (output && output_size > 0) {
        if (pipe(pipefd) == -1) {
            __LOG(LOG_ERR, "[UTIL][%s:%d] Failed to create pipe", _FILE_, __LINE__);
            return -1;
        }
    }

    pid = fork();
    if (pid == -1) {
        __LOG(LOG_ERR, "[UTIL][%s:%d] Failed to fork", _FILE_, __LINE__);
        return -1;
    }

    if (pid == 0) {
        // Child process
        char bus_str[16], addr_str[16], reg_str[16], value_str[16];

        if (output && output_size > 0) {
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
        }

        snprintf(bus_str, sizeof(bus_str), "%d", bus);
        snprintf(addr_str, sizeof(addr_str), "0x%02x", addr);
        snprintf(reg_str, sizeof(reg_str), "0x%04x", reg);

        if (strcmp(cmd, "i2cwrite") == 0) {
            snprintf(value_str, sizeof(value_str), "0x%04x", value);
            execlp(cmd, cmd, bus_str, addr_str, reg_str, value_str, (char *)NULL);
        } else if (strcmp(cmd, "i2cread") == 0) {
            snprintf(value_str, sizeof(value_str), "%d", value);  // size for read
            execlp(cmd, cmd, bus_str, addr_str, reg_str, value_str, (char *)NULL);
        } else {
            _exit(127);
        }

        // If execlp fails
        _exit(127);
    }

    // Parent process
    if (output && output_size > 0) {
        close(pipefd[1]);
        ssize_t n = read(pipefd[0], output, output_size - 1);
        if (n > 0) output[n] = '\0';
        else output[0] = '\0';
        close(pipefd[0]);
    }

    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}
