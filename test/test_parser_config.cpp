#include "../parser.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <glib/gstdio.h>

static int g_checks = 0;
static int g_failures = 0;
static gint g_json_open_count = 0;
static gchar g_critical_logs[32768] = {0};

enum BorrowedField {
    BORROWED_VHL_NAME,
    BORROWED_RTSP_ID,
    BORROWED_MOUNT_PATH,
    BORROWED_MUXER,
    BORROWED_CAPTURE_PATH,
    BORROWED_CAPTURE_ENCODER,
    BORROWED_AWB_0,
    BORROWED_AWB_1,
    BORROWED_AWB_2,
    BORROWED_AWB_3,
    BORROWED_FIELD_COUNT,
};

enum ParseExpectation {
    EXPECT_RELEASE_ONLY,
    EXPECT_OWNED_STRINGS,
    EXPECT_NO_VHL_EXTRACTION,
};

struct ParseProbe {
    ParserClass *parser;
    gint roots_created;
    gint roots_finalized;
    guint alias_mask;
    guint borrowed_count;
    gboolean borrowed_values_distinct;
    const gchar *borrowed[BORROWED_FIELD_COUNT];
};

static ParseProbe *g_active_probe = NULL;

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

static json_object *object_member(json_object *object, const gchar *name)
{
    if (object == NULL || json_object_get_type(object) != json_type_object)
        return NULL;
    return json_object_object_get(object, name);
}

static const gchar *string_member(json_object *object, const gchar *name)
{
    json_object *value = object_member(object, name);
    if (value == NULL || json_object_get_type(value) != json_type_string)
        return NULL;
    return json_object_get_string(value);
}

static void capture_borrowed_strings(ParseProbe *probe, json_object *root)
{
    json_object *vhl = object_member(root, "VHL_CAM");
    json_object *capture = object_member(vhl, "capture");
    json_object *i2c2 = object_member(vhl, "i2c2");
    json_object *i2c1 = object_member(vhl, "i2c1");

    probe->borrowed[BORROWED_VHL_NAME] = string_member(vhl, "vhl_name");
    probe->borrowed[BORROWED_RTSP_ID] = string_member(vhl, "id");
    probe->borrowed[BORROWED_MOUNT_PATH] = string_member(vhl, "tmp_path");
    probe->borrowed[BORROWED_MUXER] = string_member(vhl, "muxer");
    probe->borrowed[BORROWED_CAPTURE_PATH] = string_member(capture, "path");
    probe->borrowed[BORROWED_CAPTURE_ENCODER] =
        string_member(capture, "encoder");
    probe->borrowed[BORROWED_AWB_0] =
        string_member(object_member(i2c2, "ch0"), "awb");
    probe->borrowed[BORROWED_AWB_1] =
        string_member(object_member(i2c2, "ch1"), "awb");
    probe->borrowed[BORROWED_AWB_2] =
        string_member(object_member(i2c1, "ch2"), "awb");
    probe->borrowed[BORROWED_AWB_3] =
        string_member(object_member(i2c1, "ch3"), "awb");

    probe->borrowed_values_distinct = TRUE;
    for (guint i = 0; i < BORROWED_FIELD_COUNT; ++i) {
        if (probe->borrowed[i] == NULL)
            continue;
        ++probe->borrowed_count;
        for (guint j = 0; j < i; ++j) {
            if (probe->borrowed[j] != NULL &&
                g_strcmp0(probe->borrowed[i], probe->borrowed[j]) == 0)
                probe->borrowed_values_distinct = FALSE;
        }
    }
}

static void root_finalized(json_object *root, void *userdata)
{
    (void)root;
    ParseProbe *probe = static_cast<ParseProbe *>(userdata);
    ++probe->roots_finalized;

    const gchar *parser_values[BORROWED_FIELD_COUNT] = {
        probe->parser->arg.ohtName,
        probe->parser->arg.rtsp_id,
        probe->parser->arg.mntDir,
        probe->parser->arg.muxer,
        probe->parser->arg.cap.dir,
        probe->parser->arg.cap.encoder,
        probe->parser->arg.cam[0].awb,
        probe->parser->arg.cam[1].awb,
        probe->parser->arg.cam[2].awb,
        probe->parser->arg.cam[3].awb,
    };

    for (guint i = 0; i < BORROWED_FIELD_COUNT; ++i) {
        if (probe->borrowed[i] != NULL &&
            parser_values[i] == probe->borrowed[i])
            probe->alias_mask |= (1U << i);
    }
}

extern "C" json_object *json_object_from_file(const char *filename)
{
    ++g_json_open_count;

    gchar *contents = NULL;
    if (!g_file_get_contents(filename, &contents, NULL, NULL))
        return NULL;

    json_object *object = json_tokener_parse(contents);
    g_free(contents);
    if (object != NULL && g_active_probe != NULL) {
        ++g_active_probe->roots_created;
        capture_borrowed_strings(g_active_probe, object);
        json_object_set_userdata(object, g_active_probe, root_finalized);
    }
    return object;
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

static gchar *channel_json(const gchar *extra, const gchar *awb)
{
    return g_strdup_printf(
        "{\"enable\":true,\"hflip\":false,\"vflip\":false,"
        "\"ae_on\":true,\"awb\":\"%s\"%s%s}",
        awb, extra && extra[0] ? "," : "", extra ? extra : "");
}

static gchar *runtime_json(const gchar *rtsp_tune,
                           const gchar *ch0_extra,
                           const gchar *ch3_extra,
                           gboolean srt_enable = TRUE)
{
    gchar *ch0 = channel_json(ch0_extra, "owned-awb-0");
    gchar *ch1 = channel_json("", "owned-awb-1");
    gchar *ch2 = channel_json("", "owned-awb-2");
    gchar *ch3 = channel_json(ch3_extra, "owned-awb-3");
    gchar *json = g_strdup_printf(
        "{\"VHL_CAM\":{"
        "\"vhl_name\":\"owned-vhl-name\",\"id\":\"owned-rtsp-id\","
        "\"tmp_path\":\"/tmp/owned-mount\",\"muxer\":\"owned-muxer\","
        "\"capture\":{\"enable\":true,\"delay\":0,\"timeout\":200,"
        "\"encoder\":\"owned-capture-encoder\","
        "\"path\":\"/tmp/owned-capture\",\"record\":false,"
        "\"rtsp\":false,\"quality\":85,\"queue_size\":30,"
        "\"response\":true,\"instant\":0}%s%s,"
        "\"i2c2\":{\"crop_enable\":false,\"ch0\":%s,\"ch1\":%s},"
        "\"i2c1\":{\"crop_enable\":false,\"ch2\":%s,\"ch3\":%s}"
        "},\"ORD\":{\"port_num\":10007},"
        "\"VCM\":{\"port_num\":10009,\"srt_enable\":%s}}",
        rtsp_tune && rtsp_tune[0] ? ",\"rtsp_tune\":" : "",
        rtsp_tune ? rtsp_tune : "",
        ch0, ch1, ch2, ch3, srt_enable ? "true" : "false");
    g_free(ch0);
    g_free(ch1);
    g_free(ch2);
    g_free(ch3);
    return json;
}

static gint parse_fixture(ParserClass *parser, const gchar *contents,
                          ParseExpectation expectation = EXPECT_RELEASE_ONLY)
{
    gchar directory[] = "/tmp/gstapp-parser-config-XXXXXX";
    gchar fixture_path[512] = {0};
    gchar expected_path[512] = {0};
    CHECK(g_mkdtemp(directory) != NULL);
    g_snprintf(fixture_path, sizeof(fixture_path),
               "%s/pim_runtime.json", directory);
    g_strlcpy(expected_path, fixture_path, sizeof(expected_path));
    CHECK(g_file_set_contents(fixture_path, contents, -1, NULL));

    gchar appname[] = "gstApp";
    parser->init_arg(appname);
    g_critical_logs[0] = '\0';
    g_json_open_count = 0;
    ParseProbe probe = {};
    probe.parser = parser;
    g_active_probe = &probe;
    const gint result = parser->json_parser(fixture_path, JSON_CAM_OBJ_NAME);
    g_active_probe = NULL;

    CHECK(g_json_open_count == 1);
    CHECK(probe.roots_created == 1);
    CHECK(probe.roots_finalized == 1);
    if (expectation == EXPECT_OWNED_STRINGS) {
        if (probe.alias_mask != 0)
            fprintf(stderr, "  borrowed alias mask at root finalization: 0x%x\n",
                    probe.alias_mask);
        CHECK(probe.borrowed_count == BORROWED_FIELD_COUNT);
        CHECK(probe.borrowed_values_distinct == TRUE);
        CHECK(probe.alias_mask == 0);
    } else if (expectation == EXPECT_NO_VHL_EXTRACTION) {
        CHECK((probe.alias_mask & (1U << BORROWED_VHL_NAME)) == 0);
    }
    CHECK(parser->arg.json_file != fixture_path);
    CHECK(g_strcmp0(parser->arg.json_file, expected_path) == 0);

    CHECK(g_remove(fixture_path) == 0);
    CHECK(g_rmdir(directory) == 0);
    fixture_path[0] = '\0';
    CHECK(g_strcmp0(parser->arg.json_file, expected_path) == 0);
    return result;
}

static void check_owned_string_contents(const ParserClass *parser)
{
    CHECK(g_strcmp0(parser->arg.ohtName, "owned-vhl-name") == 0);
    CHECK(g_strcmp0(parser->arg.rtsp_id, "owned-rtsp-id") == 0);
    CHECK(g_strcmp0(parser->arg.mntDir, "/tmp/owned-mount") == 0);
    CHECK(g_strcmp0(parser->arg.muxer, "owned-muxer") == 0);
    CHECK(g_strcmp0(parser->arg.cap.dir, "/tmp/owned-capture") == 0);
    CHECK(g_strcmp0(parser->arg.cap.encoder, "owned-capture-encoder") == 0);
    CHECK(g_strcmp0(parser->arg.cam[0].awb, "owned-awb-0") == 0);
    CHECK(g_strcmp0(parser->arg.cam[1].awb, "owned-awb-1") == 0);
    CHECK(g_strcmp0(parser->arg.cam[2].awb, "owned-awb-2") == 0);
    CHECK(g_strcmp0(parser->arg.cam[3].awb, "owned-awb-3") == 0);
}

static gchar *required_section_case(const gchar *section, gboolean missing)
{
    gchar *contents = runtime_json(NULL, "", "");
    json_object *root = json_tokener_parse(contents);
    g_free(contents);
    CHECK(root != NULL);

    if (missing)
        json_object_object_del(root, section);
    else
        json_object_object_add(root, section, json_object_new_string("invalid"));

    gchar *result = g_strdup(
        json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
    json_object_put(root);
    return result;
}

static void test_merged_runtime_uses_exact_path_once_and_reads_vcm(void)
{
    ParserClass enabled_parser;
    gchar *enabled = runtime_json(NULL, "", "", TRUE);
    CHECK(parse_fixture(&enabled_parser, enabled, EXPECT_OWNED_STRINGS) == 0);
    CHECK(enabled_parser.arg.srt_en == TRUE);
    check_owned_string_contents(&enabled_parser);
    g_free(enabled);

    ParserClass disabled_parser;
    gchar *disabled = runtime_json(NULL, "", "", FALSE);
    CHECK(parse_fixture(&disabled_parser, disabled, EXPECT_OWNED_STRINGS) == 0);
    CHECK(disabled_parser.arg.srt_en == FALSE);
    check_owned_string_contents(&disabled_parser);
    g_free(disabled);
}

static void test_required_top_level_objects_fail_closed(void)
{
    const gchar *sections[] = {"VHL_CAM", "ORD", "VCM"};

    for (guint i = 0; i < G_N_ELEMENTS(sections); ++i) {
        ParserClass missing_parser;
        gchar *missing = required_section_case(sections[i], TRUE);
        CHECK(parse_fixture(&missing_parser, missing,
                            EXPECT_NO_VHL_EXTRACTION) < 0);
        g_free(missing);

        ParserClass non_object_parser;
        gchar *non_object = required_section_case(sections[i], FALSE);
        CHECK(parse_fixture(&non_object_parser, non_object,
                            EXPECT_NO_VHL_EXTRACTION) < 0);
        g_free(non_object);
    }
}

static void test_non_object_and_unreadable_roots_fail_closed(void)
{
    ParserClass non_object_parser;
    CHECK(parse_fixture(&non_object_parser, "[]") < 0);

    gchar directory[] = "/tmp/gstapp-parser-config-missing-XXXXXX";
    gchar missing_path[512] = {0};
    gchar expected_path[512] = {0};
    CHECK(g_mkdtemp(directory) != NULL);
    g_snprintf(missing_path, sizeof(missing_path),
               "%s/pim_runtime.json", directory);
    g_strlcpy(expected_path, missing_path, sizeof(expected_path));

    ParserClass unreadable_parser;
    gchar appname[] = "gstApp";
    unreadable_parser.init_arg(appname);
    g_json_open_count = 0;
    ParseProbe probe = {};
    probe.parser = &unreadable_parser;
    g_active_probe = &probe;
    CHECK(unreadable_parser.json_parser(missing_path, JSON_CAM_OBJ_NAME) < 0);
    g_active_probe = NULL;
    CHECK(g_json_open_count == 1);
    CHECK(probe.roots_created == 0);
    CHECK(probe.roots_finalized == 0);
    CHECK(unreadable_parser.arg.json_file != missing_path);
    CHECK(g_strcmp0(unreadable_parser.arg.json_file, expected_path) == 0);
    CHECK(g_rmdir(directory) == 0);
    missing_path[0] = '\0';
    CHECK(g_strcmp0(unreadable_parser.arg.json_file, expected_path) == 0);
}

static void test_malformed_arrays_fail_after_collecting_all_errors(void)
{
    ParserClass parser;
    gchar *json = runtime_json(NULL,
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
    gchar *json = runtime_json(NULL, "\"bps\":[1,2,3]", "");

    CHECK(parse_fixture(&parser, json) < 0);
    CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
    CHECK(strstr(g_critical_logs, "ch0 bps") != NULL);
    CHECK(strstr(g_critical_logs, "error=bad-length") != NULL);

    g_free(json);
}

static void test_object_instead_of_array_is_fatal(void)
{
    ParserClass parser;
    gchar *json = runtime_json(
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
    gchar *json = runtime_json(NULL, "\"bps\":null", "");

    CHECK(parse_fixture(&parser, json) < 0);
    CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
    CHECK(strstr(g_critical_logs, "ch0 bps") != NULL);
    CHECK(strstr(g_critical_logs, "error=not-array") != NULL);

    g_free(json);
}

static void test_missing_optional_arrays_keep_defaults_and_succeed(void)
{
    ParserClass parser;
    gchar *json = runtime_json(NULL, "", "");

    CHECK(parse_fixture(&parser, json) == 0);
    CHECK(parser.arg.cam[0].bps[STREAM_REC] == DEFAULT_RECORD_BITRATE);
    CHECK(parser.arg.cam[0].bps[STREAM_RTSP] == DEFAULT_RTSP_BITRATE);

    g_free(json);
}

static void test_non_integer_array_element_is_fatal(void)
{
    ParserClass parser;
    gchar *json = runtime_json(NULL,
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
    gchar *json = runtime_json("{\"frame_id_sei\":2}", "", "");

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
    test_merged_runtime_uses_exact_path_once_and_reads_vcm();
    test_required_top_level_objects_fail_closed();
    test_non_object_and_unreadable_roots_fail_closed();
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
