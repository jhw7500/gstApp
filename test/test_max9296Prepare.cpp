#include "../max9296Prepare.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (!(condition)) {                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    #condition);                                           \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static Max9296PrepareInput base_input(void)
{
    Max9296PrepareInput input = {};
    input.width = 1920;
    input.height = 1080;
    input.fps[0] = 15;
    input.fps[1] = 15;
    input.generation = 77;
    return input;
}

static void check_parse_ok(const char *line, Max9296PrepareStatus *status)
{
    CHECK(max9296_prepare_parse_status(line, strlen(line), status) == 0);
}

static void check_parse_bad(const char *line)
{
    Max9296PrepareStatus status = {};
    CHECK(max9296_prepare_parse_status(line, strlen(line), &status) < 0);
}

static void test_builds_dual_and_single_targets(void)
{
    Max9296PrepareInput input = base_input();
    input.channel_enabled[0] = 1;
    input.channel_enabled[1] = 1;
    input.channel_enabled[3] = 1;
    Max9296PrepareTarget target[2] = {};

    CHECK(max9296_prepare_build_targets(&input, target) == 0);
    CHECK(target[0].active);
    CHECK(target[0].csi == 0);
    CHECK(target[0].width == 3840 && target[0].height == 1080);
    CHECK(target[0].enable == 3);
    CHECK(strcmp(target[0].path,
                 "/sys/bus/i2c/devices/2-0048/prepare") == 0);
    CHECK(strcmp(target[0].mode, "fhd") == 0);
    CHECK(strcmp(target[0].table, "dual") == 0);
    CHECK(target[1].active);
    CHECK(target[1].csi == 1);
    CHECK(target[1].width == 1920 && target[1].height == 1080);
    CHECK(target[1].enable == 2);
    CHECK(strcmp(target[1].path,
                 "/sys/bus/i2c/devices/1-0048/prepare") == 0);
    CHECK(strcmp(target[1].mode, "fhd") == 0);
    CHECK(strcmp(target[1].table, "single") == 0);
}

static void test_builds_hd_dual_target(void)
{
    Max9296PrepareInput input = base_input();
    input.width = 1280;
    input.height = 720;
    input.channel_enabled[2] = 1;
    input.channel_enabled[3] = 1;
    Max9296PrepareTarget target[2] = {};

    CHECK(max9296_prepare_build_targets(&input, target) == 0);
    CHECK(!target[0].active);
    CHECK(target[1].active);
    CHECK(target[1].width == 2560 && target[1].height == 720);
    CHECK(target[1].enable == 3);
    CHECK(strcmp(target[1].mode, "hd") == 0);
    CHECK(strcmp(target[1].table, "dual") == 0);
}

static void test_builds_left_and_right_single_targets(void)
{
    for (unsigned csi = 0; csi < 2; ++csi) {
        for (unsigned side = 0; side < 2; ++side) {
            Max9296PrepareInput input = base_input();
            Max9296PrepareTarget target[2] = {};
            input.channel_enabled[csi * 2 + side] = 1;

            CHECK(max9296_prepare_build_targets(&input, target) == 0);
            CHECK(target[csi].active);
            CHECK(target[csi].enable == (1U << side));
            CHECK(target[csi].width == 1920 && target[csi].height == 1080);
            CHECK(strcmp(target[csi].table, "single") == 0);
            CHECK(!target[1 - csi].active);
        }
    }
}

static void test_accepts_disabled_csi_with_unusable_fps(void)
{
    Max9296PrepareInput input = base_input();
    Max9296PrepareTarget target[2] = {};
    input.channel_enabled[0] = 1;
    input.fps[1] = 0;

    CHECK(max9296_prepare_build_targets(&input, target) == 0);
    CHECK(target[0].active);
    CHECK(!target[1].active);
    CHECK(max9296_prepare_path(0) == target[0].path);
    CHECK(max9296_prepare_path(1) == target[1].path);
    CHECK(max9296_prepare_path(2) == NULL);
}

static void test_rejects_invalid_request_tuples(void)
{
    Max9296PrepareInput input = base_input();
    Max9296PrepareTarget target[2] = {};

    CHECK(max9296_prepare_build_targets(NULL, target) == -EINVAL);
    CHECK(max9296_prepare_build_targets(&input, NULL) == -EINVAL);
    input.generation = 0;
    CHECK(max9296_prepare_build_targets(&input, target) == -EINVAL);

    input = base_input();
    input.width = 1024;
    CHECK(max9296_prepare_build_targets(&input, target) == -EINVAL);
    input = base_input();
    input.height = 720;
    CHECK(max9296_prepare_build_targets(&input, target) == -EINVAL);
    input = base_input();
    input.channel_enabled[0] = 2;
    CHECK(max9296_prepare_build_targets(&input, target) == -EINVAL);
    input = base_input();
    input.channel_enabled[0] = 1;
    input.fps[0] = 0;
    CHECK(max9296_prepare_build_targets(&input, target) == -EINVAL);
    input.fps[0] = 121;
    CHECK(max9296_prepare_build_targets(&input, target) == -EINVAL);
    input = base_input();
    input.channel_enabled[0] = 1;
    input.channel_enabled[2] = 1;
    input.fps[1] = 30;
    CHECK(max9296_prepare_build_targets(&input, target) == -EINVAL);
}

static const char ready_sample[] =
    "state=READY generation=77 epoch=9 mode=fhd table=dual width=3840 "
    "height=1080 fps=15 code=0x2006 enable=3 errno=0 worker_errno=0 "
    "lease=1 match=1\n";

static void test_parses_ready_and_consumed_samples(void)
{
    Max9296PrepareStatus status = {};
    check_parse_ok(ready_sample, &status);
    CHECK(status.state == MAX9296_STATE_READY);
    CHECK(status.generation == 77 && status.epoch == 9);
    CHECK(strcmp(status.mode, "fhd") == 0);
    CHECK(strcmp(status.table, "dual") == 0);
    CHECK(status.width == 3840 && status.height == 1080 && status.fps == 15);
    CHECK(status.code == 0x2006 && status.enable == 3);
    CHECK(status.last_errno == 0 && status.worker_errno == 0);
    CHECK(status.lease == 1 && status.match == 1);

    check_parse_ok(
        "state=CONSUMED generation=77 epoch=10 mode=fhd table=single "
        "width=1920 height=1080 fps=15 code=0x2006 enable=2 errno=-116 "
        "worker_errno=-5 lease=0 match=1", &status);
    CHECK(status.state == MAX9296_STATE_CONSUMED);
    CHECK(status.last_errno == -116 && status.worker_errno == -5);
    CHECK(status.lease == 0 && status.match == 1);
}

static void test_parses_reordered_future_and_idle_samples(void)
{
    Max9296PrepareStatus status = {};
    check_parse_ok(
        "future=v2 match=0 worker_errno=0 enable=1 code=8198 fps=15 "
        "height=720 table=single epoch=1 mode=hd errno=0 width=1280 "
        "generation=4 lease=0 state=STALE", &status);
    CHECK(status.state == MAX9296_STATE_STALE);
    CHECK(status.code == 8198 && status.width == 1280 && status.height == 720);

    check_parse_ok(
        "state=IDLE generation=0 epoch=0 mode=none table=none width=0 "
        "height=0 fps=0 code=0 enable=0 errno=0 worker_errno=0 lease=0 "
        "match=0", &status);
    CHECK(status.state == MAX9296_STATE_IDLE);
    CHECK(strcmp(status.mode, "none") == 0 && strcmp(status.table, "none") == 0);
}

static void test_rejects_malformed_status_samples(void)
{
    Max9296PrepareStatus status = {};
    CHECK(max9296_prepare_parse_status(NULL, 1, &status) == -EINVAL);
    CHECK(max9296_prepare_parse_status(ready_sample, strlen(ready_sample), NULL) ==
          -EINVAL);
    check_parse_bad(
        "state=READY generation=1 epoch=1 mode=fhd table=dual width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=1");
    check_parse_bad(
        "state=READY state=READY generation=1 epoch=1 mode=fhd table=dual "
        "width=1 height=1 fps=1 code=0x2006 enable=3 errno=0 "
        "worker_errno=0 lease=1 match=1");
    check_parse_bad(
        "state=READY generation=18446744073709551616 epoch=1 mode=fhd "
        "table=dual width=1 height=1 fps=1 code=0x2006 enable=3 errno=0 "
        "worker_errno=0 lease=1 match=1");
    check_parse_bad(
        "state=READY generation=1 epoch=1 mode=fhd table=dual width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=--1 worker_errno=0 "
        "lease=1 match=1");
    check_parse_bad(
        "state=READY generation=1 epoch=1 mode=fhd table=dual width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=2 match=1");
    check_parse_bad(
        "state=READY generation=1 epoch=1 mode=fhd table=dual width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=1 match=2");
    check_parse_bad(
        "state=UNKNOWN generation=1 epoch=1 mode=fhd table=dual width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=1 match=1");
}

int main(void)
{
    test_builds_dual_and_single_targets();
    test_builds_hd_dual_target();
    test_builds_left_and_right_single_targets();
    test_accepts_disabled_csi_with_unusable_fps();
    test_rejects_invalid_request_tuples();
    test_parses_ready_and_consumed_samples();
    test_parses_reordered_future_and_idle_samples();
    test_rejects_malformed_status_samples();
    printf("max9296 prepare test: %d checks, %d failures -> %s\n", checks,
           failures, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
