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
    EXPECT_RETAINED_STRINGS,
    EXPECT_RETAINED,
    EXPECT_RELEASED,
};

struct ParseProbe {
    gint roots_created;
    gint roots_finalized;
    guint borrowed_count;
    gboolean borrowed_values_distinct;
    const gchar *borrowed[BORROWED_FIELD_COUNT];
};

/* 문서는 이제 ~ParserClass() 에서 해제된다. 즉 root_finalized() 가 parse_fixture()
 * 가 반환한 뒤에 불리므로, 프로브는 스택이 아니라 파일 스코프에 두어야 한다. */
static ParseProbe g_probe;
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
    /* 파서가 이미 소멸 중일 수 있으므로 여기서 파서를 건드리지 않는다. */
    ParseProbe *probe = static_cast<ParseProbe *>(userdata);
    ++probe->roots_finalized;
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
                          ParseExpectation expectation = EXPECT_RETAINED)
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
    g_probe = ParseProbe();
    g_active_probe = &g_probe;
    const gint result = parser->json_parser(fixture_path, JSON_CAM_OBJ_NAME);
    g_active_probe = NULL;

    CHECK(g_json_open_count == 1);
    CHECK(g_probe.roots_created == 1);
    if (expectation == EXPECT_RETAINED_STRINGS) {
        CHECK(g_probe.roots_finalized == 0);
        CHECK(g_probe.borrowed_count == BORROWED_FIELD_COUNT);
        CHECK(g_probe.borrowed_values_distinct == TRUE);
        /* 붙잡은 문서를 그대로 가리키는 것이 올바른 결과다. */
        CHECK(parser->arg.ohtName == g_probe.borrowed[BORROWED_VHL_NAME]);
        CHECK(parser->arg.rtsp_id == g_probe.borrowed[BORROWED_RTSP_ID]);
        CHECK(parser->arg.mntDir == g_probe.borrowed[BORROWED_MOUNT_PATH]);
        CHECK(parser->arg.muxer == g_probe.borrowed[BORROWED_MUXER]);
        CHECK(parser->arg.cap.dir == g_probe.borrowed[BORROWED_CAPTURE_PATH]);
        CHECK(parser->arg.cap.encoder ==
              g_probe.borrowed[BORROWED_CAPTURE_ENCODER]);
        CHECK(parser->arg.cam[0].awb == g_probe.borrowed[BORROWED_AWB_0]);
        CHECK(parser->arg.cam[1].awb == g_probe.borrowed[BORROWED_AWB_1]);
        CHECK(parser->arg.cam[2].awb == g_probe.borrowed[BORROWED_AWB_2]);
        CHECK(parser->arg.cam[3].awb == g_probe.borrowed[BORROWED_AWB_3]);
    } else if (expectation == EXPECT_RETAINED) {
        CHECK(g_probe.roots_finalized == 0);
    } else {
        CHECK(g_probe.roots_finalized == 1);
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

/* 정상 문서를 만들되 키 하나만 변형한다. parent 가 NULL 이면 최상위에서,
 * 아니면 그 자식 객체 안에서 key 를 찾는다. missing 이면 지우고, 아니면
 * 문자열 "invalid" 로 치환해 타입 검사 경로를 태운다. */
static gchar *mutated_runtime_json(const gchar *parent, const gchar *key,
                                   gboolean missing)
{
    gchar *contents = runtime_json(NULL, "", "");
    json_object *root = json_tokener_parse(contents);
    g_free(contents);
    CHECK(root != NULL);

    json_object *owner = root;
    if (parent != NULL) {
        owner = json_object_object_get(root, parent);
        CHECK(owner != NULL);
    }

    if (missing)
        json_object_object_del(owner, key);
    else
        json_object_object_add(owner, key, json_object_new_string("invalid"));

    gchar *result = g_strdup(
        json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
    json_object_put(root);
    return result;
}

static void test_merged_runtime_uses_exact_path_once_and_reads_vcm(void)
{
    ParserClass enabled_parser;
    gchar *enabled = runtime_json(NULL, "", "", TRUE);
    CHECK(parse_fixture(&enabled_parser, enabled,
                        EXPECT_RETAINED_STRINGS) == 0);
    CHECK(enabled_parser.arg.srt_en == TRUE);
    check_owned_string_contents(&enabled_parser);
    g_free(enabled);

    ParserClass disabled_parser;
    gchar *disabled = runtime_json(NULL, "", "", FALSE);
    CHECK(parse_fixture(&disabled_parser, disabled,
                        EXPECT_RETAINED_STRINGS) == 0);
    CHECK(disabled_parser.arg.srt_en == FALSE);
    check_owned_string_contents(&disabled_parser);
    g_free(disabled);
}

static void test_required_top_level_objects_fail_closed(void)
{
    const gchar *sections[] = {"VHL_CAM", "ORD", "VCM"};

    for (guint i = 0; i < G_N_ELEMENTS(sections); ++i) {
        ParserClass missing_parser;
        gchar *missing = mutated_runtime_json(NULL, sections[i], TRUE);
        CHECK(parse_fixture(&missing_parser, missing, EXPECT_RELEASED) < 0);
        g_free(missing);

        ParserClass non_object_parser;
        gchar *non_object = mutated_runtime_json(NULL, sections[i], FALSE);
        CHECK(parse_fixture(&non_object_parser, non_object,
                            EXPECT_RELEASED) < 0);
        g_free(non_object);
    }
}

static void test_non_object_and_unreadable_roots_fail_closed(void)
{
    ParserClass non_object_parser;
    CHECK(parse_fixture(&non_object_parser, "[]", EXPECT_RELEASED) < 0);

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
    g_critical_logs[0] = '\0';
    g_probe = ParseProbe();
    g_active_probe = &g_probe;
    CHECK(unreadable_parser.json_parser(missing_path, JSON_CAM_OBJ_NAME) < 0);
    g_active_probe = NULL;
    CHECK(g_json_open_count == 1);
    CHECK(strstr(g_critical_logs, "json file open fail") != NULL);
    CHECK(g_probe.roots_created == 0);
    CHECK(g_probe.roots_finalized == 0);
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

    CHECK(parse_fixture(&parser, json, EXPECT_RETAINED_STRINGS) == 0);
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

    CHECK(parse_fixture(&parser, json, EXPECT_RETAINED_STRINGS) == 0);
    CHECK(parser.arg.rtsp_frame_id_sei == DEFAULT_RTSP_FRAME_ID_SEI);

    g_free(json);
}

/* 붙잡은 문서는 파서가 살아 있는 동안 유지되다가, 소멸 시점에 정확히 한 번 해제된다. */
static void test_retained_root_is_released_once_when_parser_is_destroyed(void)
{
    gchar *json = runtime_json(NULL, "", "");

    {
        ParserClass parser;
        CHECK(parse_fixture(&parser, json, EXPECT_RETAINED_STRINGS) == 0);
        CHECK(g_probe.roots_finalized == 0);
    }
    CHECK(g_probe.roots_finalized == 1);

    g_free(json);
}

/* 리뷰어 지적 B-R1-002 — 폐기된 per-channel dz 는 치명 오류이고, 그 실패 경로도
 * cleanup 을 거쳐야 한다. 문서를 붙잡은 뒤 파서 소멸 시 해제하는지 본다. */
static void test_deprecated_dz_is_fatal_and_still_releases_root(void)
{
    gchar *json = runtime_json(NULL, "\"dz\":1", "");

    {
        ParserClass parser;
        CHECK(parse_fixture(&parser, json, EXPECT_RETAINED) < 0);
        CHECK(g_probe.roots_finalized == 0);
        CHECK(strstr(g_critical_logs, "no longer supported") != NULL);
    }
    CHECK(g_probe.roots_finalized == 1);

    g_free(json);
}

/* 이슈 #112 — 재파싱이 이전 문서를 해제하면, 2차 문서가 빠뜨린 키의 필드가 해제된
 * 문서를 가리킨다. json_get_string() 은 키가 없으면 대상을 건드리지 않기
 * 때문이다. init_arg() 없이 연달아 두 번 파싱해 이전 문서가 살아 있는지 본다.
 *
 * 이전 문서는 의도적으로 해제하지 않으므로 재파싱 1회당 문서 1개가 샌다. 그 대가로
 * use-after-free 가 사라진다. 아래 마지막 어서션이 그 누수를 명시적으로 고정한다. */
static void test_reparse_keeps_previous_root_when_key_is_omitted(void)
{
    gchar *full = runtime_json(NULL, "", "");
    gchar *without_name = mutated_runtime_json("VHL_CAM", "vhl_name", TRUE);

    /* parse_fixture 가 1차 문서의 임시 디렉터리를 스스로 지우므로 2차 문서는
     * 이 테스트가 따로 만든다. */
    gchar directory[] = "/tmp/gstapp-parser-reparse-XXXXXX";
    gchar second_path[512] = {0};
    CHECK(g_mkdtemp(directory) != NULL);
    g_snprintf(second_path, sizeof(second_path), "%s/pim_runtime.json", directory);
    CHECK(g_file_set_contents(second_path, without_name, -1, NULL));

    {
        ParserClass parser;
        CHECK(parse_fixture(&parser, full, EXPECT_RETAINED_STRINGS) == 0);
        const gchar *first_name = parser.arg.ohtName;
        CHECK(g_strcmp0(first_name, "owned-vhl-name") == 0);

        /* 2차 파싱: init_arg() 를 다시 부르지 않는다. vhl_name 이 없으므로
         * arg.ohtName 은 1차 문서를 계속 가리킨 채 남는다. */
        g_probe = ParseProbe();
        g_active_probe = &g_probe;
        CHECK(parser.json_parser(second_path, JSON_CAM_OBJ_NAME) == 0);
        g_active_probe = NULL;
        CHECK(g_probe.roots_created == 1);
        /* 1차 문서를 해제했다면 그 파이널라이저가 여기서 1 을 만든다 =
         * arg.ohtName 은 dangling. 0 이어야 한다. */
        CHECK(g_probe.roots_finalized == 0);
        /* 재바인딩되지 않았음을 포인터 동일성으로 본다. 여기서 역참조하지
         * 않는 것은 의도적이다 — CHECK 는 비치명적이라 위 어서션이 실패해도
         * 실행이 계속되고, 그 실패는 곧 1차 문서가 해제됐다는 뜻이므로 값을
         * 읽으면 use-after-free 가 된다. 값 확인은 1차 파싱 직후에 이미 했다. */
        CHECK(parser.arg.ohtName == first_name);
    }
    /* 소멸자는 마지막 문서 하나만 해제한다. 1차 문서는 샌 채로 남는다. */
    CHECK(g_probe.roots_finalized == 1);

    CHECK(g_remove(second_path) == 0);
    CHECK(g_rmdir(directory) == 0);
    g_free(full);
    g_free(without_name);
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

/* ───────────────────────────── 이슈 #116 ─────────────────────────────
 * 옛 json_object_get_value() 는 목적지 타입이 아니라 JSON 값의 타입으로 분기해
 * data 를 캐스팅했다. 호출자가 무엇을 기대하는지 함수에 전달되지 않으므로,
 * 설정의 값 타입이 틀리면 크기가 다른 타입으로 목적지에 썼다.
 *
 * 아래 어서션은 목적지 포인터를 절대 역참조하지 않는다 — 수정 전에는 그것이
 * 야생 포인터라 역참조 자체가 미정의 동작이다. 보존 여부만 본다.
 */

/* 정본 문서에서 경로 하나의 값을 교체한다. path 는 NULL 로 끝나는 키 배열이고
 * 마지막 원소가 교체 대상 키다. value 의 소유권은 이 함수가 가져간다. */
static gchar *runtime_json_with(const gchar *const *path, json_object *value)
{
    gchar *contents = runtime_json(NULL, "", "");
    json_object *root = json_tokener_parse(contents);
    g_free(contents);
    CHECK(root != NULL);

    json_object *owner = root;
    gsize last = 0;
    while (path[last + 1] != NULL) {
        owner = json_object_object_get(owner, path[last]);
        CHECK(owner != NULL);
        ++last;
    }
    json_object_object_add(owner, path[last], value);

    gchar *result = g_strdup(
        json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));
    json_object_put(root);
    return result;
}

/* parse_fixture() 와 달리 init_arg() 뒤, json_parser() 앞에 문자열 목적지에
 * 표식을 심는다. arg.ohtName 의 기본값은 arg.appname 이고 그것은 호출자의
 * 스택을 가리켜 비교 대상으로 불안정하므로, 정적 수명의 표식을 직접 세운다. */
static gint parse_marked(ParserClass *parser, const gchar *contents,
                         const gchar *marker)
{
    gchar directory[] = "/tmp/gstapp-parser-116-XXXXXX";
    gchar fixture_path[512] = {0};
    CHECK(g_mkdtemp(directory) != NULL);
    g_snprintf(fixture_path, sizeof(fixture_path), "%s/pim_runtime.json",
               directory);
    CHECK(g_file_set_contents(fixture_path, contents, -1, NULL));

    gchar appname[] = "gstApp";
    parser->init_arg(appname);
    g_critical_logs[0] = '\0';
    g_json_open_count = 0;
    g_probe = ParseProbe();
    g_active_probe = &g_probe;

    if (marker != NULL)
        parser->arg.ohtName = marker;

    const gint result = parser->json_parser(fixture_path, JSON_CAM_OBJ_NAME);
    g_active_probe = NULL;

    CHECK(g_remove(fixture_path) == 0);
    CHECK(g_rmdir(directory) == 0);
    return result;
}

/* 경우 1 — 문자열 자리에 정수. 옛 json_type_int 갈래가 8바이트 목적지에
 * 4바이트만 써서 하위 절반이 포화 int 로 바뀌고 상위는 옛 포인터를 물려받았다. */
static void test_wrong_typed_string_leaves_destination_untouched(void)
{
    static const gchar *const path[] = {"VHL_CAM", "vhl_name", NULL};
    static const gchar marker[] = "gstapp-116-vhl-name-marker";
    ParserClass parser;
    gchar *json =
        runtime_json_with(path, json_object_new_int64(1234567890000000LL));

    CHECK(parse_marked(&parser, json, marker) == 0);
    CHECK(parser.arg.ohtName == marker);

    g_free(json);
}

/* 경우 2 — 불리언 자리에 문자열. 옛 json_type_string 갈래가 4바이트 gboolean 에
 * 8바이트 포인터를 써서 인접 ae_gain 까지 덮었다. */
static void test_wrong_typed_bool_leaves_destination_untouched(void)
{
    static const gchar *const path[] = {"VHL_CAM", "i2c2", "ch0", "ae_on",
                                        NULL};
    ParserClass parser;
    gchar *json = runtime_json_with(path, json_object_new_string("yes"));

    CHECK(parse_marked(&parser, json, NULL) == 0);
    CHECK(parser.arg.cam[0].ae_on == TRUE);              /* init_arg 기본값 */
    CHECK(parser.arg.cam[0].ae_gain == DEFAULT_AE_GAIN); /* 인접 필드 무사 */

    g_free(json);
}

/* 같은 결함의 최악 갈래 — 배열은 원소 수만큼 연속으로 쓴다. 스칼라 목적지에
 * 오면 필드 경계를 넘는 쓰기가 되고, 길이는 설정 파일이 정한다. */
static void test_array_in_scalar_slot_leaves_destination_untouched(void)
{
    static const gchar *const path[] = {"VHL_CAM", "i2c2", "ch0", "ae_on",
                                        NULL};
    ParserClass parser;
    json_object *arr = json_object_new_array();
    json_object_array_add(arr, json_object_new_int(11));
    json_object_array_add(arr, json_object_new_int(22));
    json_object_array_add(arr, json_object_new_int(33));
    json_object_array_add(arr, json_object_new_int(44));
    gchar *json = runtime_json_with(path, arr);

    CHECK(parse_marked(&parser, json, NULL) == 0);
    CHECK(parser.arg.cam[0].ae_on == TRUE);
    CHECK(parser.arg.cam[0].ae_gain == DEFAULT_AE_GAIN);

    g_free(json);
}

/* 보존 검사 — 수정 전후 모두 통과해야 한다. gboolean 은 gint 라 정수 0/1 은
 * 옛 코드에서도 크기가 맞아 정상 동작했다. 엄격화가 이걸 깨뜨리면 회귀다. */
static void test_integer_zero_one_still_accepted_for_bool(void)
{
    static const gchar *const on[] = {"VHL_CAM", "i2c2", "ch0", "enable",
                                      NULL};
    static const gchar *const off[] = {"VHL_CAM", "i2c1", "ch3", "enable",
                                       NULL};
    {
        ParserClass parser;
        gchar *json = runtime_json_with(on, json_object_new_int(1));
        CHECK(parse_marked(&parser, json, NULL) == 0);
        CHECK(parser.arg.cam[0].enable == TRUE);
        g_free(json);
    }
    {
        ParserClass parser;
        gchar *json = runtime_json_with(off, json_object_new_int(0));
        CHECK(parse_marked(&parser, json, NULL) == 0);
        CHECK(parser.arg.cam[3].enable == FALSE);
        g_free(json);
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
    test_retained_root_is_released_once_when_parser_is_destroyed();
    test_deprecated_dz_is_fatal_and_still_releases_root();
    test_reparse_keeps_previous_root_when_key_is_omitted();
    test_out_of_range_split_sec_falls_back_to_default();
    test_wrong_typed_string_leaves_destination_untouched();
    test_wrong_typed_bool_leaves_destination_untouched();
    test_array_in_scalar_slot_leaves_destination_untouched();
    test_integer_zero_one_still_accepted_for_bool();

    printf("\nparser config test: %d checks, %d failures -> %s\n",
           g_checks, g_failures, g_failures ? "FAILED" : "PASSED");
    return g_failures ? 1 : 0;
}
