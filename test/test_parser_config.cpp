#include "../parser.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <glib/gstdio.h>

static int g_checks = 0;
static int g_failures = 0;
static gchar g_fixture_path[512] = {0};
static gchar g_critical_logs[32768] = {0};

#define CHECK(condition)                                                \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(condition)) {                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,   \
                    #condition);                                        \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

void mylog(gint opt, const gchar *format, ...)
{
    (void)opt;
    gchar message[1024];
    va_list args;
    va_start(args, format);
    g_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (opt == LOG_CRIT) {
        g_strlcat(g_critical_logs, message, sizeof(g_critical_logs));
        g_strlcat(g_critical_logs, "\n", sizeof(g_critical_logs));
    }
}

gchar *search_file(const gchar *path, const gchar *prefix,
                   const gchar *suffix)
{
    (void)path;
    (void)prefix;
    (void)suffix;
    return g_fixture_path;
}

/* check_arg() 를 부르면 --gc-sections 가 그 함수를 더 이상 버리지 않으므로,
 * 본체(util.cpp)에만 있던 심볼을 시험이 직접 제공한다. mkdir 은 시험 대상이 아니고 시험이
 * stream_en[STREAM_CAP] 을 끄므로 도달하지 않는다. 링크만 만족시키는 스텁이다. */
CmdArg cmdArg;

int safe_mkdir_p(const char *path, mode_t mode)
{
    (void)path;
    (void)mode;
    return 0;
}

static gchar *channel_json(const gchar *extra)
{
    return g_strdup_printf(
        "{\"enable\":true,\"hflip\":false,\"vflip\":false,"
        "\"ae_on\":true%s%s}",
        extra && extra[0] ? "," : "", extra ? extra : "");
}

static gchar *edgeconf_json(const gchar *rtsp_tune,
                            const gchar *ch0_extra,
                            const gchar *ch3_extra)
{
    gchar *ch0 = channel_json(ch0_extra);
    gchar *ch1 = channel_json("");
    gchar *ch2 = channel_json("");
    gchar *ch3 = channel_json(ch3_extra);
    gchar *json = g_strdup_printf(
        "{\"VHL_CAM\":{"
        "\"vhl_name\":\"parser-test\",\"id\":\"user\","
        "\"tmp_path\":\"/tmp\",\"muxer\":\"mp4\","
        "\"capture\":{\"enable\":false}%s%s,"
        "\"i2c2\":{\"crop_enable\":false,\"ch0\":%s,\"ch1\":%s},"
        "\"i2c1\":{\"crop_enable\":false,\"ch2\":%s,\"ch3\":%s}"
        "}}",
        rtsp_tune && rtsp_tune[0] ? ",\"rtsp_tune\":" : "",
        rtsp_tune ? rtsp_tune : "",
        ch0, ch1, ch2, ch3);
    g_free(ch0);
    g_free(ch1);
    g_free(ch2);
    g_free(ch3);
    return json;
}

static gint parse_fixture(ParserClass *parser, const gchar *contents)
{
    gchar directory[] = "/tmp/gstapp-parser-config-XXXXXX";
    CHECK(g_mkdtemp(directory) != NULL);
    g_snprintf(g_fixture_path, sizeof(g_fixture_path),
               "%s/edgeconf_test.json", directory);
    CHECK(g_file_set_contents(g_fixture_path, contents, -1, NULL));

    gchar appname[] = "gstApp";
    parser->init_arg(appname);
    g_critical_logs[0] = '\0';
    const gint result = parser->json_parser(directory, JSON_CAM_OBJ_NAME);

    CHECK(g_remove(g_fixture_path) == 0);
    CHECK(g_rmdir(directory) == 0);
    g_fixture_path[0] = '\0';
    return result;
}

static void test_malformed_arrays_fail_after_collecting_all_errors(void)
{
    ParserClass parser;
    gchar *json = edgeconf_json(NULL,
                                "\"bps\":[8000],\"gop\":\"bad\"",
                                "\"bps\":[7000,900]");

    CHECK(parse_fixture(&parser, json) < 0);
    CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
    CHECK(parser.arg.cam[0].gop[STREAM_REC] == DEFAULT_GOP_SIZE);
    CHECK(parser.arg.cam[3].bps[STREAM_REC] == 7000);
    CHECK(strstr(g_critical_logs, "ch0 bps") != NULL);
    CHECK(strstr(g_critical_logs, "expected=2") != NULL);
    CHECK(strstr(g_critical_logs, "error=bad-length") != NULL);
    CHECK(strstr(g_critical_logs, "ch0 gop") != NULL);
    CHECK(strstr(g_critical_logs, "error=not-array") != NULL);
    CHECK(strstr(g_critical_logs,
                 "2 fatal edgeconf array error(s)") != NULL);

    g_free(json);
}

static void test_oversized_array_is_fatal(void)
{
    ParserClass parser;
    gchar *json = edgeconf_json(NULL, "\"bps\":[1,2,3]", "");

    CHECK(parse_fixture(&parser, json) < 0);
    CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
    CHECK(strstr(g_critical_logs, "ch0 bps") != NULL);
    CHECK(strstr(g_critical_logs, "error=bad-length") != NULL);

    g_free(json);
}

static void test_object_instead_of_array_is_fatal(void)
{
    ParserClass parser;
    gchar *json = edgeconf_json(
        NULL, "\"profile\":{\"record\":9,\"rtsp\":9}", "");

    CHECK(parse_fixture(&parser, json) < 0);
    CHECK(parser.arg.cam[0].profile[STREAM_REC] == PROFILE_UNSET);
    CHECK(strstr(g_critical_logs, "ch0 profile") != NULL);
    CHECK(strstr(g_critical_logs, "error=not-array") != NULL);

    g_free(json);
}

static void test_explicit_null_array_is_fatal(void)
{
    ParserClass parser;
    gchar *json = edgeconf_json(NULL, "\"bps\":null", "");

    CHECK(parse_fixture(&parser, json) < 0);
    CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
    CHECK(strstr(g_critical_logs, "ch0 bps") != NULL);
    CHECK(strstr(g_critical_logs, "error=not-array") != NULL);

    g_free(json);
}

static void test_missing_optional_arrays_keep_defaults_and_succeed(void)
{
    ParserClass parser;
    gchar *json = edgeconf_json(NULL, "", "");

    CHECK(parse_fixture(&parser, json) == 0);
    CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
    CHECK(parser.arg.cam[0].bps[STREAM_RTSP] == DEFAULT_RTSP_BITRATE);

    g_free(json);
}

static void test_non_integer_array_element_is_fatal(void)
{
    ParserClass parser;
    gchar *json = edgeconf_json(NULL,
                                "\"quant\":[10,\"invalid\"]", "");

    CHECK(parse_fixture(&parser, json) < 0);
    CHECK(parser.arg.cam[0].quant[STREAM_REC] == DEFAULT_QUANT);
    CHECK(strstr(g_critical_logs, "ch0 quant") != NULL);
    CHECK(strstr(g_critical_logs, "error=bad-element") != NULL);

    g_free(json);
}

static void test_existing_recoverable_errors_remain_nonfatal(void)
{
    ParserClass parser;
    gchar *json = edgeconf_json("{\"frame_id_sei\":2}", "", "");

    CHECK(parse_fixture(&parser, json) == 0);
    CHECK(parser.arg.rtsp_frame_id_sei == DEFAULT_RTSP_FRAME_ID_SEI);

    g_free(json);
}

/* 이슈 #102 — -S 는 범위 검사가 없어 0..59 밖 값이 guint8 로 절단된 채 splitCheck() 로
 * 들어갔다. check_arg() 가 stdin 경로와 같은 상한(MAX_SPLIT_SEC)으로 되돌리는지 본다.
 * 반환값이 아니라 클램프된 값을 보는데, 클램프가 check_arg() 의 어떤 return 보다 앞이다.
 * stream_en 을 모두 끄는 이유: check_arg() 끝의 요약 로그가 init_arg() 이 초기화하지 않는
 * 필드(arg.rtsp_passwd)를 %s 로 읽어 미정의 동작이 된다. 클램프는 그 블록들보다 앞이다. */
static void test_out_of_range_split_sec_falls_back_to_default(void)
{
    gchar appname[] = "gstApp";
    const struct { gint in; gint want; } cases[] = {
        {-1, DEFAULT_SPLIT_SEC}, {60, DEFAULT_SPLIT_SEC},
        {255, DEFAULT_SPLIT_SEC}, {300, DEFAULT_SPLIT_SEC},
        {0, 0}, {1, 1}, {30, 30}, {59, 59},
    };
    guint i;

    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        ParserClass parser;
        parser.init_arg(appname);
        parser.arg.stream_en[STREAM_REC] = FALSE;
        parser.arg.stream_en[STREAM_RTSP] = FALSE;
        parser.arg.stream_en[STREAM_CAP] = FALSE;
        parser.arg.split_sec = cases[i].in;
        (void)parser.check_arg();
        if (parser.arg.split_sec != cases[i].want)
            fprintf(stderr, "  case[%u] in=%d want=%d got=%d\n", i, cases[i].in,
                    cases[i].want, parser.arg.split_sec);
        CHECK(parser.arg.split_sec == cases[i].want);
    }
}

int main(void)
{
    test_malformed_arrays_fail_after_collecting_all_errors();
    test_oversized_array_is_fatal();
    test_object_instead_of_array_is_fatal();
    test_explicit_null_array_is_fatal();
    test_non_integer_array_element_is_fatal();
    test_missing_optional_arrays_keep_defaults_and_succeed();
    test_existing_recoverable_errors_remain_nonfatal();
    test_out_of_range_split_sec_falls_back_to_default();

    printf("\nparser config test: %d checks, %d failures -> %s\n",
           g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
    return g_failures ? 1 : 0;
}
