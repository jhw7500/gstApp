/* camera health v1 producer 오프라인 시험.
 *
 * healthProducer.cpp 의 실제 publish() 를 그대로 돌린다. 손으로 만든 fixture 가
 * 아니라 제품 코드가 내보내는 문서를 검사해야 하는 이유가 있다. 이전 판은
 * 기동 구간에 STARTING 관측을 내면서 최상위 status 를 "OK" 로 적었는데,
 * pim 쪽 aggregator 는 그 조합을 PRODUCER_MALFORMED 로 버린다. 문서를 눈으로
 * 읽어서는 드러나지 않고 실제 출력을 소비자에게 먹여봐야 드러나는 종류다.
 *
 * gstApp 본체는 링크하지 않는다. healthProducer.cpp 가 실제로 참조하는 심볼
 * (cmdArg / encoderQueueInputTotal / mylog) 만 여기서 정의한다.
 *
 * 출력 검증은 이 프로그램이 하지 않는다. 여기서는 시나리오별 문서를 파일로
 * 남기고, run-health-producer-test.sh 가 pim-package 의 camera_healthd.py 로
 * 판정한다. 소비자가 진짜 소비자여야 의미가 있기 때문이다.
 */

#include "../healthProducer.h"
#include "../util.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ── gstApp 심볼 스텁 ──────────────────────────────────────────────────── */

CmdArg cmdArg;

static guint64 g_fake_q_in[MAX_CHANNEL];

guint64 encoderQueueInputTotal(guint8 ch)
{
    if (ch >= MAX_CHANNEL)
        return 0;
    return g_fake_q_in[ch];
}

void mylog(gint opt, const gchar *fmt, ...)
{
    (void)opt;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ── 시나리오 ──────────────────────────────────────────────────────────── */

typedef struct {
    const gchar *name;
    /* publish 직전에 상태를 만든다. tick 은 지금까지 흐른 publish 횟수. */
    void (*arrange)(guint tick);
    guint ticks;            /* 이 시나리오에서 돌릴 publish 횟수 */
} Scenario;

static void arrange_ok(guint tick)
{
    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++) {
        g_fake_q_in[ch] += 30;                 /* 인코더로 버퍼가 계속 들어온다 */
        healthProducerNoteFragmentClosed(ch, "/mnt/rec/x.mp4");
    }
    (void)tick;
}

static void arrange_starting(guint tick)
{
    /* fragment 를 한 번도 알리지 않는다. 기동 직후 상태. */
    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++)
        g_fake_q_in[ch] += 30;
    (void)tick;
}

static void arrange_disabled(guint tick)
{
    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++)
        cmdArg.cam[ch].enable = FALSE;
    (void)tick;
}

static void arrange_stall(guint tick)
{
    /* q_in 을 움직이지 않는다. stall_sec 를 넘기면 FAIL 이 되어야 한다. */
    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++)
        healthProducerNoteFragmentClosed(ch, "/mnt/rec/x.mp4");
    (void)tick;
}

static void arrange_bus_error(guint tick)
{
    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++) {
        g_fake_q_in[ch] += 30;
        healthProducerNoteFragmentClosed(ch, "/mnt/rec/x.mp4");
    }
    if (tick == 0)
        healthProducerNotePipelineError("Internal data stream error.");
}

static void arrange_no_growth(guint tick)
{
    /* fragment 를 처음에 한 번만 알리고 이후로는 침묵한다. */
    for (guint8 ch = 0; ch < MAX_CHANNEL; ch++)
        g_fake_q_in[ch] += 30;
    if (tick == 0) {
        for (guint8 ch = 0; ch < MAX_CHANNEL; ch++)
            healthProducerNoteFragmentClosed(ch, "/mnt/rec/x.mp4");
    }
}

/* 순서에 의미가 있다. producer 는 프로세스 하나로 계속 돌기 때문에 상태가
 * 시나리오를 가로질러 남는다.
 *   - starting 은 fragment 를 한 번도 못 본 상태라 반드시 맨 앞이어야 한다.
 *   - buserr 는 파이프라인 에러를 래치하고 풀지 않으므로 반드시 맨 뒤여야
 *     한다. 앞에 두면 이후 시나리오의 gstreamer 가 전부 FAIL 로 덮인다.
 * 실제 운용에서도 같다 - 버스 에러가 나면 프로세스는 재시작 대상이다. */
static const Scenario SCENARIOS[] = {
    { "starting",  arrange_starting,  2 },
    { "ok",        arrange_ok,        2 },
    { "disabled",  arrange_disabled,  2 },
    { "stall",     arrange_stall,     5 },   /* stall_sec=2 를 넘기도록 */
    { "nogrow",    arrange_no_growth, 5 },   /* no_growth_sec=2 를 넘기도록 */
    { "buserr",    arrange_bus_error, 2 },
};

/* ── 구동 ──────────────────────────────────────────────────────────────── */

typedef struct {
    const Scenario *scenario;
    guint tick;
    GMainLoop *loop;
    const gchar *out_dir;
} RunState;

static gboolean drive(gpointer data)
{
    RunState *st = (RunState *)data;

    if (st->tick >= st->scenario->ticks) {
        /* 마지막 publish 결과를 시나리오 이름으로 보존한다. */
        gchar *src = g_build_filename(st->out_dir, "gstApp.json", NULL);
        gchar *dst = g_strdup_printf("%s/%s.json", st->out_dir, st->scenario->name);
        gchar *body = NULL;
        gsize len = 0;
        if (g_file_get_contents(src, &body, &len, NULL))
            g_file_set_contents(dst, body, (gssize)len, NULL);
        else
            g_printerr("scenario %s: no document published\n", st->scenario->name);
        g_free(body);
        g_free(src);
        g_free(dst);
        g_main_loop_quit(st->loop);
        return G_SOURCE_REMOVE;
    }

    st->scenario->arrange(st->tick);
    st->tick++;
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        g_printerr("usage: %s <output-dir>\n", argv[0]);
        return 2;
    }
    const gchar *out_dir = argv[1];
    g_setenv("PIM_CAMERA_HEALTH_DIR", out_dir, TRUE);
    g_setenv("PIM_CAMERA_HEALTH_STALL_SEC", "2", TRUE);
    g_setenv("PIM_CAMERA_HEALTH_NO_GROWTH_SEC", "2", TRUE);

    cmdArg.mntDir = "/tmp";
    healthProducerStart();

    for (gsize i = 0; i < G_N_ELEMENTS(SCENARIOS); i++) {
        const Scenario *sc = &SCENARIOS[i];

        /* 시나리오마다 상태를 초기화한다. disabled 다음에 ok 가 오면 안 되므로
         * enable 을 매번 되돌린다. */
        for (guint8 ch = 0; ch < MAX_CHANNEL; ch++) {
            cmdArg.cam[ch].enable = TRUE;
            g_fake_q_in[ch] = 0;
        }

        RunState st;
        st.scenario = sc;
        st.tick = 0;
        st.out_dir = out_dir;
        st.loop = g_main_loop_new(NULL, FALSE);

        /* publish 와 같은 1초 주기로 돈다. producer 타이머가 먼저 등록돼 있어
         * 같은 초에는 publish 가 앞선다. 즉 N 초의 문서는 N-1 초의 arrange 를
         * 반영한다. 마지막 arrange 이후 한 틱을 더 돌고 나서 파일을 걷는다. */
        g_timeout_add(1000, drive, &st);
        g_main_loop_run(st.loop);
        g_main_loop_unref(st.loop);

        g_print("scenario %s: %u ticks\n", sc->name, sc->ticks);
    }
    return 0;
}
