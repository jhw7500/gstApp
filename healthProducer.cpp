#include "healthProducer.h"

#include "encoderBin.h"
#include "util.h"

#include <json-c/json.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define HEALTH_DIR_DEFAULT  "/run/pim-camera"
#define HEALTH_BASENAME     "gstApp.json"
#define HEALTH_TMP_SUFFIX   ".tmp"
#define BOOT_ID_PATH        "/proc/sys/kernel/random/boot_id"

/* 출력 디렉터리는 환경변수로 덮어쓸 수 있다. 보드에서는 항상 기본값을 쓰고,
 * 오프라인 시험이 실제 publish() 를 임시 디렉터리로 돌리기 위한 통로다. */
static const gchar *health_dir(void)
{
    const gchar *env = g_getenv("PIM_CAMERA_HEALTH_DIR");
    return (env && *env) ? env : HEALTH_DIR_DEFAULT;
}

#define HEALTH_PERIOD_SEC   1
#define STALL_SEC_DEFAULT   3     /* q_in 무증가 허용 시간 */
#define NO_GROWTH_SEC       180   /* fragment 미커밋 허용 시간. 파일 1개가 1분이므로 3배 */

typedef struct {
    guint64 last_q_in;
    gint64  q_in_moved_us;      /* q_in 이 마지막으로 증가한 시각 */
    gint64  fragment_us;        /* 마지막 fragment 커밋 시각 */
    gboolean fragment_seen;
} ChannelHealth;

static ChannelHealth g_ch[MAX_CHANNEL];
static gchar   g_boot_id[64];
static guint64 g_sequence;
static gint64  g_pipeline_error_us;   /* 0 이면 에러 없음 */
static gchar   g_pipeline_error[192];
static gint    g_stall_sec = STALL_SEC_DEFAULT;
static gint    g_no_growth_sec = NO_GROWTH_SEC;

/* fragment 신호는 "fragment-closed-worker" 스레드에서 들어오고 publish() 는
 * main loop 스레드에서 읽는다. 1초에 한 번 읽고 분당 몇 번 쓰는 정도라
 * 뮤텍스 비용은 무시할 수 있다. */
static GMutex  g_health_lock;

static gint64 now_us(void) { return g_get_monotonic_time(); }

static gboolean read_boot_id(void)
{
    FILE *fp = fopen(BOOT_ID_PATH, "r");
    if (!fp)
        return FALSE;
    /* procfs 는 stat 크기를 0 으로 보고하므로 크기로 판단하지 않고 그냥 읽는다. */
    gchar *p = fgets(g_boot_id, (int)sizeof(g_boot_id), fp);
    fclose(fp);
    if (!p)
        return FALSE;
    g_strstrip(g_boot_id);
    return g_boot_id[0] != '\0';
}

/* observation 하나를 만든다. status 와 code 조합은 aggregator 가 검증하므로
 * OK 는 NONE, N/A 는 DISABLED 로 고정한다. */
static json_object *observation(const gchar *block, guint8 ch,
                                const gchar *status, const gchar *code,
                                const gchar *evidence_name,
                                json_object *evidence_value,
                                const gchar *reason_detail)
{
    json_object *o = json_object_new_object();
    json_object_object_add(o, "block", json_object_new_string(block));

    json_object *scope = json_object_new_object();
    gchar id[8];
    g_snprintf(id, sizeof(id), "ch%u", ch);
    json_object_object_add(scope, "kind", json_object_new_string("channel"));
    json_object_object_add(scope, "id", json_object_new_string(id));
    json_object *chans = json_object_new_array();
    json_object_array_add(chans, json_object_new_int(ch));
    json_object_object_add(scope, "channels", chans);
    json_object_object_add(o, "scope", scope);

    json_object_object_add(o, "status", json_object_new_string(status));
    json_object_object_add(o, "code", json_object_new_string(code));
    json_object_object_add(o, "count",
                           json_object_new_int(g_strcmp0(status, "FAIL") == 0 ? 1 : 0));

    json_object *ev = json_object_new_array();
    json_object *e = json_object_new_object();
    json_object_object_add(e, "name", json_object_new_string(evidence_name));
    json_object_object_add(e, "source", json_object_new_string("gstApp"));
    json_object_object_add(e, "value", evidence_value);
    json_object_array_add(ev, e);
    json_object_object_add(o, "evidence", ev);

    /* root_cause 는 일부러 넣지 않는다. false 로 박으면 상류가 멀쩡한데 여기서만
     * 깨진 경우에도 aggregator 가 근본 원인을 하나도 못 내놓는다. 필드를 비우면
     * aggregator 가 같은 채널의 상류 실패로 설명되는지 보고 스스로 판정한다. */
    if (reason_detail)
        json_object_object_add(o, "reason_detail", json_object_new_string(reason_detail));
    return o;
}

/* 녹화 저장소 상태. 마운트가 읽기 전용이거나 가득 찼는지만 본다. */
static const gchar *storage_fault(const gchar *dir)
{
    struct statvfs vfs;
    if (statvfs(dir, &vfs) != 0)
        return NULL;   /* 경로를 못 읽는 것은 저장소 결함의 증거가 아니다 */
    if (vfs.f_flag & ST_RDONLY)
        return "STORAGE_READ_ONLY";
    if (vfs.f_blocks > 0 && vfs.f_bavail == 0)
        return "STORAGE_FULL";
    return NULL;
}

static void publish(void)
{
    gint64 t = now_us();

    /* 교차 스레드 필드는 여기서 한 번만 복사하고, 이후로는 지역 사본만 본다.
     * 한 문서 안에서 채널들이 서로 다른 시점의 상태를 섞어 보고하지 않는다. */
    ChannelHealth ch_snap[MAX_CHANNEL];
    gint64 pipeline_error_us;
    gchar  pipeline_error[sizeof(g_pipeline_error)];
    g_mutex_lock(&g_health_lock);
    memcpy(ch_snap, g_ch, sizeof(ch_snap));
    pipeline_error_us = g_pipeline_error_us;
    g_strlcpy(pipeline_error, g_pipeline_error, sizeof(pipeline_error));
    g_mutex_unlock(&g_health_lock);

    json_object *root = json_object_new_object();
    json_object_object_add(root, "schema", json_object_new_int(1));
    json_object_object_add(root, "producer", json_object_new_string("gstApp"));
    json_object_object_add(root, "boot_id", json_object_new_string(g_boot_id));
    json_object_object_add(root, "pid", json_object_new_int((gint)getpid()));
    json_object_object_add(root, "sequence", json_object_new_int64((gint64)++g_sequence));
    json_object_object_add(root, "observed_monotonic_ms",
                           json_object_new_int64(t / 1000));

    json_object *obs = json_object_new_array();
    gboolean any_fail = FALSE;
    gboolean any_starting = FALSE;

    const gchar *rec_dir = cmdArg.mntDir ? cmdArg.mntDir : "/mnt/sd_cam";
    const gchar *storage = storage_fault(rec_dir);

    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++) {
        if (!cmdArg.cam[ch].enable) {
            json_object_array_add(obs, observation("gstreamer", ch, "N/A", "DISABLED",
                                                   "configured", json_object_new_boolean(FALSE), NULL));
            json_object_array_add(obs, observation("recording", ch, "N/A", "DISABLED",
                                                   "configured", json_object_new_boolean(FALSE), NULL));
            continue;
        }

        /* ── gstreamer ────────────────────────────────────────────────── */
        guint64 q_in = encoderQueueInputTotal(ch);
        if (q_in != ch_snap[ch].last_q_in) {
            ch_snap[ch].last_q_in = q_in;
            ch_snap[ch].q_in_moved_us = t;
            g_mutex_lock(&g_health_lock);
            g_ch[ch].last_q_in = q_in;
            g_ch[ch].q_in_moved_us = t;
            g_mutex_unlock(&g_health_lock);
        }
        gint64 idle_us = t - ch_snap[ch].q_in_moved_us;

        if (pipeline_error_us != 0) {
            json_object_array_add(obs, observation("gstreamer", ch, "FAIL",
                                                   "GSTREAMER_PIPELINE_ERROR",
                                                   "bus_error", json_object_new_string(pipeline_error),
                                                   NULL));
            any_fail = TRUE;
        } else if (idle_us > (gint64)g_stall_sec * G_USEC_PER_SEC) {
            json_object_array_add(obs, observation("gstreamer", ch, "FAIL",
                                                   "GSTREAMER_SOURCE_STALL",
                                                   "enc_queue_input", json_object_new_int64((gint64)q_in),
                                                   "no buffer reached the encoder queue"));
            any_fail = TRUE;
        } else {
            json_object_array_add(obs, observation("gstreamer", ch, "OK", "NONE",
                                                   "enc_queue_input", json_object_new_int64((gint64)q_in),
                                                   NULL));
        }

        /* ── recording ────────────────────────────────────────────────── */
        if (storage) {
            json_object_array_add(obs, observation("recording", ch, "FAIL", storage,
                                                   "record_dir", json_object_new_string(rec_dir),
                                                   NULL));
            any_fail = TRUE;
        } else if (!ch_snap[ch].fragment_seen) {
            /* 아직 첫 파일이 닫히지 않았다. 기동 중이므로 실패로 보지 않는다.
             * STARTING 은 aggregator 에서 RECOVERING 으로 올라가며, 이는 정상
             * 기동 구간을 표현하는 의도된 상태다. */
            json_object_array_add(obs, observation("recording", ch, "STARTING", "NONE",
                                                   "fragment_closed", json_object_new_boolean(FALSE),
                                                   "no fragment committed yet"));
            any_starting = TRUE;
        } else if (t - ch_snap[ch].fragment_us > (gint64)g_no_growth_sec * G_USEC_PER_SEC) {
            json_object_array_add(obs, observation("recording", ch, "FAIL",
                                                   "RECORDING_NO_GROWTH",
                                                   "seconds_since_fragment",
                                                   json_object_new_int64((t - ch_snap[ch].fragment_us) / G_USEC_PER_SEC),
                                                   NULL));
            any_fail = TRUE;
        } else {
            json_object_array_add(obs, observation("recording", ch, "OK", "NONE",
                                                   "seconds_since_fragment",
                                                   json_object_new_int64((t - ch_snap[ch].fragment_us) / G_USEC_PER_SEC),
                                                   NULL));
        }
    }

    json_object_object_add(root, "observations", obs);
    /* 최상위 status 는 관측과 모순되면 안 된다. aggregator 는 status="OK" 인데
     * OK/N/A 가 아닌 관측이 섞여 있으면 스냅샷 전체를 PRODUCER_MALFORMED 로
     * 버린다. 기동 구간의 STARTING 이 정확히 그 경우라 별도로 표기한다. */
    json_object_object_add(root, "status",
                           json_object_new_string(any_fail ? "FAIL"
                                                  : (any_starting ? "STARTING" : "OK")));

    json_object *pd = json_object_new_object();
    json_object_object_add(pd, "stall_sec", json_object_new_int(g_stall_sec));
    json_object_object_add(pd, "no_growth_sec", json_object_new_int(g_no_growth_sec));
    json_object_object_add(root, "producer_data", pd);

    /* 원자적 publish: tmp 에 쓰고 fsync 후 rename. */
    const gchar *dir = health_dir();
    g_mkdir_with_parents(dir, 0750);
    gchar path[256], tmp[288];
    g_snprintf(path, sizeof(path), "%s/%s", dir, HEALTH_BASENAME);
    g_snprintf(tmp, sizeof(tmp), "%s%s", path, HEALTH_TMP_SUFFIX);
    FILE *fp = fopen(tmp, "w");
    if (fp) {
        const char *text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
        if (fputs(text, fp) >= 0 && fputc('\n', fp) != EOF) {
            fflush(fp);
            fsync(fileno(fp));
            fclose(fp);
            chmod(tmp, 0640);
            if (rename(tmp, path) != 0)
                unlink(tmp);
        } else {
            fclose(fp);
            unlink(tmp);
        }
    }
    json_object_put(root);
}

static gboolean health_tick(gpointer data)
{
    (void)data;
    publish();
    return G_SOURCE_CONTINUE;
}

/* 임계값 환경변수. 설정하지 않으면 기본값을 쓴다. 오프라인 시험이 180초를
 * 기다리지 않고 no-growth 경로를 밟을 수 있어야 해서 열어 둔다. */
static void read_threshold(const gchar *name, gint *out)
{
    const gchar *env = g_getenv(name);
    if (!env || !*env)
        return;
    gchar *end = NULL;
    gint64 value = g_ascii_strtoll(env, &end, 10);
    if (end && *end == '\0' && value > 0 && value <= G_MAXINT)
        *out = (gint)value;
}

void healthProducerStart(void)
{
    read_threshold("PIM_CAMERA_HEALTH_STALL_SEC", &g_stall_sec);
    read_threshold("PIM_CAMERA_HEALTH_NO_GROWTH_SEC", &g_no_growth_sec);
    if (!read_boot_id()) {
        __LOG(LOG_ERR, "[GST][%s:%d] health producer disabled: boot ID unavailable",
              _FILE_, __LINE__);
        return;
    }
    gint64 t = now_us();
    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++) {
        g_ch[ch].q_in_moved_us = t;
        g_ch[ch].fragment_us = t;
    }
    g_timeout_add_seconds(HEALTH_PERIOD_SEC, health_tick, NULL);
    __LOG(LOG_NOTICE, "[GST][%s:%d] health producer started -> %s/%s (period %ds)",
          _FILE_, __LINE__, health_dir(), HEALTH_BASENAME, HEALTH_PERIOD_SEC);
}

void healthProducerNotePipelineError(const gchar *detail)
{
    g_mutex_lock(&g_health_lock);
    g_pipeline_error_us = now_us();
    g_strlcpy(g_pipeline_error, detail ? detail : "pipeline error",
              sizeof(g_pipeline_error));
    g_mutex_unlock(&g_health_lock);
}

void healthProducerNoteFragmentClosed(gint ch, const gchar *location)
{
    (void)location;
    if (ch < 0 || ch >= MAX_CHANNEL)
        return;
    g_mutex_lock(&g_health_lock);
    g_ch[ch].fragment_us = now_us();
    g_ch[ch].fragment_seen = TRUE;
    g_mutex_unlock(&g_health_lock);
}
