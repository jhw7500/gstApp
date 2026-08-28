#include "../max9296Prepare.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        __sync_add_and_fetch(&checks, 1);                                  \
        if (!(condition)) {                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,    \
                    #condition);                                           \
            __sync_add_and_fetch(&failures, 1);                            \
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
    CHECK(strcmp(target[0].mode, "dual-wide") == 0);
    CHECK(strcmp(target[0].table, "dual") == 0);
    CHECK(target[1].active);
    CHECK(target[1].csi == 1);
    CHECK(target[1].width == 1920 && target[1].height == 1080);
    CHECK(target[1].enable == 2);
    CHECK(strcmp(target[1].path,
                 "/sys/bus/i2c/devices/1-0048/prepare") == 0);
    CHECK(strcmp(target[1].mode, "single") == 0);
    CHECK(strcmp(target[1].table, "right") == 0);
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
    CHECK(strcmp(target[1].mode, "dual-wide") == 0);
    CHECK(strcmp(target[1].table, "dual") == 0);
}

static void test_builds_360p_dual_and_single_targets(void)
{
    Max9296PrepareInput dual_input = base_input();
    dual_input.width = 640;
    dual_input.height = 360;
    dual_input.channel_enabled[0] = 1;
    dual_input.channel_enabled[1] = 1;
    Max9296PrepareTarget dual_target[2] = {};

    const int dual_result =
        max9296_prepare_build_targets(&dual_input, dual_target);
    CHECK(dual_result == 0);
    if (dual_result == 0) {
        CHECK(dual_target[0].active);
        CHECK(dual_target[0].width == 1280 && dual_target[0].height == 360);
        CHECK(dual_target[0].enable == 3);
        CHECK(strcmp(dual_target[0].mode, "dual-wide") == 0);
        CHECK(strcmp(dual_target[0].table, "dual") == 0);
    }

    for (unsigned side = 0; side < 2; ++side) {
        Max9296PrepareInput single_input = base_input();
        single_input.width = 640;
        single_input.height = 360;
        single_input.channel_enabled[2 + side] = 1;
        Max9296PrepareTarget single_target[2] = {};

        const int single_result =
            max9296_prepare_build_targets(&single_input, single_target);
        CHECK(single_result == 0);
        if (single_result == 0) {
            CHECK(single_target[1].active);
            CHECK(single_target[1].width == 640 &&
                  single_target[1].height == 360);
            CHECK(single_target[1].enable == (1U << side));
            CHECK(strcmp(single_target[1].mode, "single") == 0);
            CHECK(strcmp(single_target[1].table,
                         side == 0 ? "left" : "right") == 0);
        }
    }
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
            CHECK(strcmp(target[csi].mode, "single") == 0);
            CHECK(strcmp(target[csi].table, side == 0 ? "left" : "right") ==
                  0);
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
    "state=READY generation=77 epoch=9 mode=dual-wide table=dual width=3840 "
    "height=1080 fps=15 code=0x2006 enable=3 errno=0 worker_errno=0 "
    "lease=1 match=1\n";

static void test_parses_ready_and_consumed_samples(void)
{
    Max9296PrepareStatus status = {};
    check_parse_ok(ready_sample, &status);
    CHECK(status.state == MAX9296_STATE_READY);
    CHECK(status.generation == 77 && status.epoch == 9);
    CHECK(strcmp(status.mode, "dual-wide") == 0);
    CHECK(strcmp(status.table, "dual") == 0);
    CHECK(status.width == 3840 && status.height == 1080 && status.fps == 15);
    CHECK(status.code == 0x2006 && status.enable == 3);
    CHECK(status.last_errno == 0 && status.worker_errno == 0);
    CHECK(status.lease == 1 && status.match == 1);

    check_parse_ok(
        "state=CONSUMED generation=77 epoch=10 mode=single table=right "
        "width=1920 height=1080 fps=15 code=0x2006 enable=2 errno=-116 "
        "worker_errno=-5 lease=0 match=1", &status);
    CHECK(status.state == MAX9296_STATE_CONSUMED);
    CHECK(strcmp(status.mode, "single") == 0);
    CHECK(strcmp(status.table, "right") == 0);
    CHECK(status.last_errno == -116 && status.worker_errno == -5);
    CHECK(status.lease == 0 && status.match == 1);
}

static void test_parses_reordered_future_and_idle_samples(void)
{
    Max9296PrepareStatus status = {};
    check_parse_ok(
        "future=v2 match=0 worker_errno=0 enable=1 code=8198 fps=15 "
        "height=720 table=future-table epoch=1 mode=future-mode errno=0 width=1280 "
        "generation=4 lease=0 state=STALE", &status);
    CHECK(status.state == MAX9296_STATE_STALE);
    CHECK(status.code == 8198 && status.width == 1280 && status.height == 720);
    CHECK(strcmp(status.mode, "future-mode") == 0);
    CHECK(strcmp(status.table, "future-table") == 0);

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
        "state=READY generation=1 epoch=1 mode=opaque table=opaque width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=1");
    check_parse_bad(
        "state=READY state=READY generation=1 epoch=1 mode=opaque table=opaque "
        "width=1 height=1 fps=1 code=0x2006 enable=3 errno=0 "
        "worker_errno=0 lease=1 match=1");
    check_parse_bad(
        "state=READY generation=18446744073709551616 epoch=1 mode=opaque "
        "table=opaque width=1 height=1 fps=1 code=0x2006 enable=3 errno=0 "
        "worker_errno=0 lease=1 match=1");
    check_parse_bad(
        "state=READY generation=1 epoch=1 mode=opaque table=opaque width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=--1 worker_errno=0 "
        "lease=1 match=1");
    check_parse_bad(
        "state=READY generation=1 epoch=1 mode=opaque table=opaque width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=2 match=1");
    check_parse_bad(
        "state=READY generation=1 epoch=1 mode=opaque table=opaque width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=1 match=2");
    check_parse_bad(
        "state=UNKNOWN generation=1 epoch=1 mode=opaque table=opaque width=1 "
        "height=1 fps=1 code=0x2006 enable=3 errno=0 worker_errno=0 "
        "lease=1 match=1");
}

static Max9296PrepareTarget classification_target(void)
{
    Max9296PrepareTarget target = {};
    target.active = true;
    target.csi = 0;
    target.path = "/prepare";
    target.width = 3840;
    target.height = 1080;
    target.fps = 15;
    target.enable = 3;
    target.mode = "dual-wide";
    target.table = "dual";
    return target;
}

static Max9296PrepareStatus current_status(Max9296PrepareState state)
{
    Max9296PrepareStatus status = {};
    status.state = state;
    status.generation = 77;
    status.epoch = 9;
    strcpy(status.mode, "dual-wide");
    strcpy(status.table, "dual");
    status.width = 3840;
    status.height = 1080;
    status.fps = 15;
    status.code = 0x2006;
    status.enable = 3;
    status.lease = 0;
    status.match = 1;
    return status;
}

static void test_owner_lock_is_exclusive_until_released(void)
{
    char path[] = "/tmp/max9296-prepare-lock-XXXXXX";
    const int seed = mkstemp(path);
    CHECK(seed >= 0);
    if (seed < 0)
        return;
    CHECK(close(seed) == 0);
    CHECK(unlink(path) == 0);

    const int first = max9296_prepare_acquire_owner_lock(path);
    CHECK(first >= 0);
    CHECK(max9296_prepare_acquire_owner_lock(path) == -EWOULDBLOCK);
    max9296_prepare_release_owner_lock(first);

    const int third = max9296_prepare_acquire_owner_lock(path);
    CHECK(third >= 0);
    max9296_prepare_release_owner_lock(third);
    CHECK(unlink(path) == 0);
}

struct ClassificationCase {
    const char *name;
    Max9296PrepareStatus status;
    int expected_result;
    Max9296PrepareDisposition expected_disposition;
};

static void test_classifies_prepare_status_truth_table(void)
{
    Max9296PrepareStatus consumed = current_status(MAX9296_STATE_CONSUMED);
    Max9296PrepareStatus consumed_mismatch = consumed;
    consumed_mismatch.width = 1920;
    Max9296PrepareStatus consumed_not_matched = consumed;
    consumed_not_matched.match = 0;
    Max9296PrepareStatus consumed_worker_error = consumed;
    consumed_worker_error.worker_errno = -EIO;

    Max9296PrepareStatus ready = current_status(MAX9296_STATE_READY);
    ready.lease = 1;
    ready.last_errno = -ESTALE;
    Max9296PrepareStatus ready_mismatch = ready;
    strcpy(ready_mismatch.mode, "single");
    Max9296PrepareStatus ready_not_matched = ready;
    ready_not_matched.match = 0;
    Max9296PrepareStatus ready_worker_error = ready;
    ready_worker_error.worker_errno = -EIO;
    Max9296PrepareStatus ready_unleased = ready;
    ready_unleased.lease = 0;
    Max9296PrepareStatus ready_zero_generation = ready;
    ready_zero_generation.generation = 0;
    Max9296PrepareStatus ready_zero_epoch = ready;
    ready_zero_epoch.epoch = 0;

    Max9296PrepareStatus stale_leased = current_status(MAX9296_STATE_STALE);
    stale_leased.lease = 1;
    Max9296PrepareStatus idle = current_status(MAX9296_STATE_IDLE);
    idle.generation = 0;
    idle.epoch = 0;
    strcpy(idle.mode, "none");
    strcpy(idle.table, "none");
    idle.width = 0;
    idle.height = 0;
    idle.fps = 0;
    idle.code = 0;
    idle.enable = 0;
    idle.match = 0;
    Max9296PrepareStatus failed = idle;
    failed.state = MAX9296_STATE_FAILED;
    Max9296PrepareStatus expired = idle;
    expired.state = MAX9296_STATE_EXPIRED;
    Max9296PrepareStatus stale = idle;
    stale.state = MAX9296_STATE_STALE;
    Max9296PrepareStatus preparing = current_status(MAX9296_STATE_PREPARING);

    const ClassificationCase cases[] = {
        {"consumed matching fingerprint is warm", consumed, 0,
         MAX9296_DISPOSITION_WARM},
        {"consumed fingerprint mismatch needs prepare", consumed_mismatch, 0,
         MAX9296_DISPOSITION_NEW_PREPARE},
        {"consumed match flag clear needs prepare", consumed_not_matched, 0,
         MAX9296_DISPOSITION_NEW_PREPARE},
        {"consumed worker failure is terminal", consumed_worker_error, 0,
         MAX9296_DISPOSITION_FAIL},
        {"ready matching lease refreshes despite stale errno", ready, 0,
         MAX9296_DISPOSITION_REFRESH_READY},
        {"ready fingerprint mismatch fails", ready_mismatch, 0,
         MAX9296_DISPOSITION_FAIL},
        {"ready match flag clear fails", ready_not_matched, 0,
         MAX9296_DISPOSITION_FAIL},
        {"ready worker failure fails", ready_worker_error, 0,
         MAX9296_DISPOSITION_FAIL},
        {"ready without lease fails", ready_unleased, 0,
         MAX9296_DISPOSITION_FAIL},
        {"ready lease with zero generation fails", ready_zero_generation, 0,
         MAX9296_DISPOSITION_FAIL},
        {"ready lease with zero epoch fails", ready_zero_epoch, 0,
         MAX9296_DISPOSITION_FAIL},
        {"stale lease violates protocol", stale_leased, -EPROTO,
         MAX9296_DISPOSITION_FAIL},
        {"idle needs new prepare", idle, 0, MAX9296_DISPOSITION_NEW_PREPARE},
        {"failed needs new prepare", failed, 0,
         MAX9296_DISPOSITION_NEW_PREPARE},
        {"expired needs new prepare", expired, 0,
         MAX9296_DISPOSITION_NEW_PREPARE},
        {"stale needs new prepare", stale, 0,
         MAX9296_DISPOSITION_NEW_PREPARE},
        {"preparing is busy", preparing, -EBUSY, MAX9296_DISPOSITION_FAIL},
    };
    const Max9296PrepareTarget target = classification_target();

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Max9296PrepareDisposition disposition = MAX9296_DISPOSITION_WARM;
        const int result = max9296_prepare_classify(&target, &cases[i].status,
                                                    &disposition);
        CHECK(result == cases[i].expected_result);
        CHECK(disposition == cases[i].expected_disposition);
        if (result != cases[i].expected_result)
            fprintf(stderr, "classification case failed: %s\n", cases[i].name);
    }
}

struct FakeReadStep {
    std::string text;
    ssize_t result;
};

struct FakeDomain {
    std::vector<FakeReadStep> reads;
    std::vector<ssize_t> prepare_results;
    ssize_t cancel_result;
    size_t read_index;
    size_t prepare_index;
    std::vector<std::string> writes;

    FakeDomain()
        : cancel_result(SSIZE_MAX), read_index(0), prepare_index(0)
    {
    }
};

struct FakeIo {
    FakeDomain domain[2];
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    unsigned barrier_target;
    unsigned barrier_entered;
    unsigned max_barrier_entered;
    bool barrier_released;
    unsigned sleep_calls;
    unsigned create_calls;
    unsigned join_calls;
    unsigned fail_create_call;
    int fail_create_error;
    unsigned fail_join_call;
    int fail_join_error;
    bool hold_prepare_for_join_retry;
    bool release_held_prepare;
    unsigned active_prepare_writes;
    bool read_before_quiescence;
    pthread_t last_created_thread;
    bool last_created_valid;
    bool last_created_joined;
    bool prepare_committed_on_error;
    uint64_t now;

    FakeIo()
        : barrier_target(0), barrier_entered(0), max_barrier_entered(0),
          barrier_released(false), sleep_calls(0), create_calls(0), join_calls(0),
          fail_create_call(0), fail_create_error(EAGAIN), fail_join_call(0),
          fail_join_error(EIO), hold_prepare_for_join_retry(false),
          release_held_prepare(false), active_prepare_writes(0),
          read_before_quiescence(false), last_created_thread(),
          last_created_valid(false), last_created_joined(false),
          prepare_committed_on_error(false), now(1000)
    {
        CHECK(pthread_mutex_init(&mutex, NULL) == 0);
        CHECK(pthread_cond_init(&condition, NULL) == 0);
    }

    ~FakeIo()
    {
        CHECK(pthread_cond_destroy(&condition) == 0);
        CHECK(pthread_mutex_destroy(&mutex) == 0);
    }
};

static int fake_domain_index(const char *path)
{
    if (strcmp(path, max9296_prepare_path(0)) == 0)
        return 0;
    if (strcmp(path, max9296_prepare_path(1)) == 0)
        return 1;
    return -1;
}

static void fake_add_read(FakeIo *fake, unsigned domain,
                          const std::string &text)
{
    FakeReadStep step = {text, SSIZE_MAX};
    fake->domain[domain].reads.push_back(step);
}

static void fake_add_read_error(FakeIo *fake, unsigned domain, int error)
{
    FakeReadStep step = {"", -error};
    fake->domain[domain].reads.push_back(step);
}

static ssize_t fake_read_file(void *context, const char *path, char *buffer,
                              size_t capacity)
{
    FakeIo *const fake = static_cast<FakeIo *>(context);
    const int index = fake_domain_index(path);
    if (index < 0)
        return -ENOENT;

    CHECK(pthread_mutex_lock(&fake->mutex) == 0);
    if (fake->hold_prepare_for_join_retry &&
        fake->active_prepare_writes != 0 &&
        !fake->release_held_prepare) {
        fake->read_before_quiescence = true;
        fake->release_held_prepare = true;
        pthread_cond_broadcast(&fake->condition);
        while (fake->active_prepare_writes != 0)
            CHECK(pthread_cond_wait(&fake->condition, &fake->mutex) == 0);
    }
    FakeDomain &domain = fake->domain[index];
    if (domain.read_index >= domain.reads.size()) {
        CHECK(false);
        CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
        return -EIO;
    }
    const FakeReadStep step = domain.reads[domain.read_index++];
    CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
    if (step.result != SSIZE_MAX)
        return step.result;
    if (step.text.size() > capacity)
        return -ENOSPC;
    memcpy(buffer, step.text.data(), step.text.size());
    return static_cast<ssize_t>(step.text.size());
}

static void fake_barrier_wait(FakeIo *fake)
{
    CHECK(pthread_mutex_lock(&fake->mutex) == 0);
    ++fake->barrier_entered;
    if (fake->barrier_entered > fake->max_barrier_entered)
        fake->max_barrier_entered = fake->barrier_entered;
    if (fake->barrier_entered >= fake->barrier_target)
        fake->barrier_released = true;
    pthread_cond_broadcast(&fake->condition);

    struct timespec deadline = {};
    CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_nsec += 300000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    while (!fake->barrier_released) {
        const int result = pthread_cond_timedwait(&fake->condition,
                                                  &fake->mutex, &deadline);
        if (result == ETIMEDOUT)
            break;
        CHECK(result == 0);
    }
    --fake->barrier_entered;
    pthread_cond_broadcast(&fake->condition);
    CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
}

static ssize_t fake_write_file(void *context, const char *path,
                               const char *buffer, size_t length)
{
    FakeIo *const fake = static_cast<FakeIo *>(context);
    const int index = fake_domain_index(path);
    if (index < 0)
        return -ENOENT;

    const std::string command(buffer, length);
    CHECK(pthread_mutex_lock(&fake->mutex) == 0);
    FakeDomain &domain = fake->domain[index];
    domain.writes.push_back(command);
    ssize_t result = SSIZE_MAX;
    const bool cancel = command == "0\n";
    if (cancel) {
        result = domain.cancel_result;
    } else if (domain.prepare_index < domain.prepare_results.size()) {
        result = domain.prepare_results[domain.prepare_index++];
    }
    const bool wait = !cancel && result == SSIZE_MAX &&
                      fake->barrier_target != 0;
    const bool hold = !cancel && result == SSIZE_MAX &&
                      fake->hold_prepare_for_join_retry;
    if (hold) {
        ++fake->active_prepare_writes;
        pthread_cond_broadcast(&fake->condition);
    }
    CHECK(pthread_mutex_unlock(&fake->mutex) == 0);

    if (wait)
        fake_barrier_wait(fake);
    if (hold) {
        CHECK(pthread_mutex_lock(&fake->mutex) == 0);
        while (!fake->release_held_prepare)
            CHECK(pthread_cond_wait(&fake->condition, &fake->mutex) == 0);
        --fake->active_prepare_writes;
        pthread_cond_broadcast(&fake->condition);
        CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
    }
    return result == SSIZE_MAX ? static_cast<ssize_t>(length) : result;
}

static ssize_t fake_write_file_with_commit(void *context, const char *path,
                                           const char *buffer, size_t length,
                                           bool *committed)
{
    FakeIo *const fake = static_cast<FakeIo *>(context);
    const ssize_t result = fake_write_file(context, path, buffer, length);
    *committed = result == static_cast<ssize_t>(length) ||
                 (fake->prepare_committed_on_error &&
                  std::string(buffer, length) != "0\n");
    return result;
}

static uint64_t fake_monotonic_ns(void *context)
{
    FakeIo *const fake = static_cast<FakeIo *>(context);
    CHECK(pthread_mutex_lock(&fake->mutex) == 0);
    fake->now += 100;
    const uint64_t result = fake->now;
    CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
    return result;
}

static void fake_sleep_ms(void *context, unsigned milliseconds)
{
    FakeIo *const fake = static_cast<FakeIo *>(context);
    CHECK(milliseconds == 100);
    CHECK(pthread_mutex_lock(&fake->mutex) == 0);
    ++fake->sleep_calls;
    CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
}

static int fake_thread_create(void *context, pthread_t *thread,
                              void *(*entry)(void *), void *argument)
{
    FakeIo *const fake = static_cast<FakeIo *>(context);
    CHECK(pthread_mutex_lock(&fake->mutex) == 0);
    const unsigned call = ++fake->create_calls;
    const bool fail = call == fake->fail_create_call;
    const int failure = fake->fail_create_error;
    CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
    if (fail)
        return failure;
    const int result = pthread_create(thread, NULL, entry, argument);
    if (result == 0) {
        CHECK(pthread_mutex_lock(&fake->mutex) == 0);
        fake->last_created_thread = *thread;
        fake->last_created_valid = true;
        fake->last_created_joined = false;
        CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
    }
    return result;
}

static int fake_thread_join(void *context, pthread_t thread, void **result)
{
    FakeIo *const fake = static_cast<FakeIo *>(context);
    CHECK(pthread_mutex_lock(&fake->mutex) == 0);
    const unsigned call = ++fake->join_calls;
    const bool fail = call == fake->fail_join_call;
    const int failure = fake->fail_join_error;
    while (fail && fake->hold_prepare_for_join_retry &&
           fake->active_prepare_writes == 0)
        CHECK(pthread_cond_wait(&fake->condition, &fake->mutex) == 0);
    if (!fail && fake->hold_prepare_for_join_retry) {
        fake->release_held_prepare = true;
        pthread_cond_broadcast(&fake->condition);
    }
    CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
    if (fail)
        return failure;
    const int join_result = pthread_join(thread, result);
    if (join_result == 0) {
        CHECK(pthread_mutex_lock(&fake->mutex) == 0);
        fake->last_created_joined = true;
        CHECK(pthread_mutex_unlock(&fake->mutex) == 0);
    }
    return join_result;
}

static Max9296PrepareIo fake_io(FakeIo *fake)
{
    Max9296PrepareIo io = {};
    io.read_file = fake_read_file;
    io.write_file = fake_write_file;
    io.write_file_with_commit = fake_write_file_with_commit;
    io.monotonic_ns = fake_monotonic_ns;
    io.sleep_ms = fake_sleep_ms;
    io.thread_create = fake_thread_create;
    io.thread_join = fake_thread_join;
    io.context = fake;
    return io;
}

static Max9296PrepareInput dual_input(void)
{
    Max9296PrepareInput input = base_input();
    input.channel_enabled[0] = 1;
    input.channel_enabled[1] = 1;
    input.channel_enabled[2] = 1;
    input.channel_enabled[3] = 1;
    return input;
}

static std::string status_text(const char *state, uint64_t generation,
                               uint64_t epoch, const char *mode,
                               const char *table, uint32_t width,
                               uint32_t height, uint32_t fps, uint32_t enable,
                               int last_errno, int worker_errno,
                               uint32_t lease, uint32_t match)
{
    char text[256];
    const int length = snprintf(
        text, sizeof(text),
        "state=%s generation=%llu epoch=%llu mode=%s table=%s width=%u "
        "height=%u fps=%u code=0x2006 enable=%u errno=%d worker_errno=%d "
        "lease=%u match=%u\n",
        state, static_cast<unsigned long long>(generation),
        static_cast<unsigned long long>(epoch), mode, table, width, height,
        fps, enable, last_errno, worker_errno, lease, match);
    CHECK(length > 0 && static_cast<size_t>(length) < sizeof(text));
    return std::string(text, static_cast<size_t>(length));
}

static std::string idle_status(void)
{
    return status_text("IDLE", 0, 0, "none", "none", 0, 0, 0, 0, 0, 0,
                       0, 0);
}

static std::string dual_ready(uint64_t generation, uint64_t epoch,
                              int last_errno = 0, int worker_errno = 0,
                              uint32_t lease = 1, uint32_t match = 1)
{
    return status_text("READY", generation, epoch, "dual-wide", "dual", 3840,
                       1080, 15, 3, last_errno, worker_errno, lease, match);
}

static std::string hd_dual_ready(uint64_t generation, uint64_t epoch)
{
    return status_text("READY", generation, epoch, "dual-wide", "dual", 2560,
                       720, 15, 3, 0, 0, 1, 1);
}

static std::string single_ready(uint64_t generation, uint64_t epoch,
                                uint32_t enable = 1)
{
    return status_text("READY", generation, epoch, "single",
                       enable == 1 ? "left" : "right", 1920, 1080, 15,
                       enable, 0, 0, 1, 1);
}

static std::string dual_consumed(uint64_t generation, uint64_t epoch,
                                 uint32_t width = 3840)
{
    return status_text("CONSUMED", generation, epoch, "dual-wide", "dual",
                       width, 1080, 15, 3, 0, 0, 0, 1);
}

static void test_cold_domains_write_in_parallel_with_one_generation(void)
{
    FakeIo fake;
    fake.barrier_target = 2;
    for (unsigned i = 0; i < 2; ++i) {
        fake_add_read(&fake, i, idle_status());
        fake_add_read(&fake, i, dual_ready(77, 9));
    }
    Max9296PrepareReport report = {};
    Max9296PrepareInput input = dual_input();
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == MAX9296_PREPARE_OK);
    CHECK(report.generation == 77 && report.error == 0);
    CHECK(fake.create_calls == 2 && fake.join_calls == 2);
    CHECK(fake.max_barrier_entered == 2);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(fake.domain[i].writes.size() == 1);
        if (!fake.domain[i].writes.empty())
            CHECK(fake.domain[i].writes[0] == "1 77 3840 1080 15 3\n");
        CHECK(report.domain[i].action == MAX9296_ACTION_COLD_PREPARED);
        CHECK(report.domain[i].rollback_owned);
        CHECK(report.domain[i].elapsed_ns > 0);
    }
}

static void test_one_active_domain_creates_one_worker(void)
{
    for (unsigned side = 0; side < 2; ++side) {
        FakeIo fake;
        const uint32_t enable = 1U << side;
        fake_add_read(&fake, 0, idle_status());
        fake_add_read(&fake, 0, single_ready(77, 4, enable));
        Max9296PrepareInput input = base_input();
        input.channel_enabled[side] = 1;
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);

        CHECK(max9296_prepare_all(&input, &report, &io) == 0);
        CHECK(fake.create_calls == 1 && fake.join_calls == 1);
        CHECK(fake.domain[1].reads.empty() && fake.domain[1].writes.empty());
        CHECK(report.domain[1].action == MAX9296_ACTION_SKIPPED);
    }
}

static void test_hd_dual_coordinator_uses_driver_fingerprint(void)
{
    FakeIo fake;
    fake_add_read(&fake, 1, idle_status());
    fake_add_read(&fake, 1, hd_dual_ready(77, 4));
    Max9296PrepareInput input = base_input();
    input.width = 1280;
    input.height = 720;
    input.channel_enabled[2] = 1;
    input.channel_enabled[3] = 1;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == 0);
    CHECK(fake.domain[1].writes.size() == 1);
    if (!fake.domain[1].writes.empty())
        CHECK(fake.domain[1].writes[0] == "1 77 2560 720 15 3\n");
    CHECK(report.domain[1].action == MAX9296_ACTION_COLD_PREPARED);
}

static void test_legacy_requires_all_active_paths_missing(void)
{
    {
        FakeIo fake;
        fake_add_read_error(&fake, 0, ENOENT);
        fake_add_read_error(&fake, 1, ENOENT);
        Max9296PrepareInput input = dual_input();
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);
        CHECK(max9296_prepare_all(&input, &report, &io) ==
              MAX9296_PREPARE_LEGACY);
        CHECK(report.legacy_fallback && report.error == 0);
        CHECK(fake.create_calls == 0);
        CHECK(report.domain[0].action == MAX9296_ACTION_LEGACY);
        CHECK(report.domain[1].action == MAX9296_ACTION_LEGACY);
        CHECK(fake.domain[0].writes.empty() && fake.domain[1].writes.empty());
    }
    {
        FakeIo fake;
        fake_add_read_error(&fake, 0, ENOENT);
        fake_add_read(&fake, 1, idle_status());
        Max9296PrepareInput input = dual_input();
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);
        CHECK(max9296_prepare_all(&input, &report, &io) == -ENOENT);
        CHECK(!report.legacy_fallback && report.error == -ENOENT);
        CHECK(fake.create_calls == 0);
        CHECK(fake.domain[0].writes.empty() && fake.domain[1].writes.empty());
    }
}

static void test_warm_consumed_is_reread_without_a_worker(void)
{
    FakeIo fake;
    const std::string warm = status_text(
        "CONSUMED", 45, 8, "dual-wide", "dual", 3840, 1080, 15, 3,
        -ESTALE, 0, 0, 1);
    fake_add_read(&fake, 0, warm);
    fake_add_read(&fake, 0, warm);
    Max9296PrepareInput input = dual_input();
    input.channel_enabled[2] = input.channel_enabled[3] = 0;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == 0);
    CHECK(fake.domain[0].read_index == 2);
    CHECK(fake.create_calls == 0 && fake.domain[0].writes.empty());
    CHECK(report.domain[0].action == MAX9296_ACTION_WARM_REUSED);
    CHECK(report.domain[0].after.last_errno == -ESTALE);
}

static void test_nonwarm_consumed_write_results_propagate(void)
{
    const ssize_t results[] = {SSIZE_MAX, -EBUSY, -ESTALE};
    const int expected[] = {0, -EBUSY, -ESTALE};
    for (unsigned case_index = 0; case_index < 3; ++case_index) {
        FakeIo fake;
        fake_add_read(&fake, 0, dual_consumed(45, 8, 1920));
        fake_add_read(&fake, 0,
                      case_index == 0 ? dual_ready(77, 9) : idle_status());
        fake.domain[0].prepare_results.push_back(results[case_index]);
        Max9296PrepareInput input = dual_input();
        input.channel_enabled[2] = input.channel_enabled[3] = 0;
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);
        CHECK(max9296_prepare_all(&input, &report, &io) ==
              expected[case_index]);
        CHECK(report.error == expected[case_index]);
        CHECK(fake.domain[0].read_index == 2);
    }
}

static void test_ready_refresh_keeps_generation_and_not_rollback_ownership(void)
{
    FakeIo fake;
    fake_add_read(&fake, 0, dual_ready(45, 8, -ESTALE));
    fake_add_read(&fake, 0, dual_ready(45, 9));
    Max9296PrepareInput input = dual_input();
    input.channel_enabled[2] = input.channel_enabled[3] = 0;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == 0);
    CHECK(fake.domain[0].writes.size() == 1);
    if (!fake.domain[0].writes.empty())
        CHECK(fake.domain[0].writes[0] == "1 45 3840 1080 15 3\n");
    CHECK(report.domain[0].action == MAX9296_ACTION_READY_REFRESHED);
    CHECK(!report.domain[0].rollback_owned);
    CHECK(report.domain[0].after.last_errno == 0);
}

static void test_peer_failure_rolls_back_only_newly_published_lease(void)
{
    FakeIo fake;
    for (unsigned i = 0; i < 2; ++i)
        fake_add_read(&fake, i, idle_status());
    fake_add_read(&fake, 0, dual_ready(77, 9));
    fake_add_read(&fake, 1, idle_status());
    fake.domain[1].prepare_results.push_back(-EIO);
    Max9296PrepareInput input = dual_input();
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == -EIO);
    CHECK(fake.join_calls == 2);
    CHECK(fake.domain[0].writes.size() == 2);
    if (fake.domain[0].writes.size() >= 2)
        CHECK(fake.domain[0].writes[1] == "0\n");
    CHECK(fake.domain[1].writes.size() == 1);
    CHECK(report.domain[0].rollback_owned);
    CHECK(!report.domain[1].rollback_owned);
}

static void test_final_read_failure_cancels_and_preserves_first_error(void)
{
    FakeIo fake;
    fake_add_read(&fake, 0, idle_status());
    fake_add_read_error(&fake, 0, EIO);
    fake.domain[0].cancel_result = -EROFS;
    Max9296PrepareInput input = dual_input();
    input.channel_enabled[2] = input.channel_enabled[3] = 0;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == -EIO);
    CHECK(report.error == -EIO);
    CHECK(report.domain[0].error == -EIO);
    CHECK(report.domain[0].rollback_error == -EROFS);
    CHECK(fake.domain[0].writes.size() == 2);
    if (fake.domain[0].writes.size() >= 2)
        CHECK(fake.domain[0].writes[1] == "0\n");
}

static void test_final_status_mismatches_fail_and_cancel(void)
{
    const std::string bad[] = {
        dual_ready(76, 9),
        status_text("READY", 77, 9, "dual-wide", "dual", 1920, 1080, 15,
                    3, 0, 0, 1, 1),
        status_text("READY", 77, 9, "single", "dual", 3840, 1080, 15, 3,
                    0, 0, 1, 1),
        status_text("READY", 77, 9, "dual-wide", "left", 3840, 1080, 15,
                    3, 0, 0, 1, 1),
        dual_ready(77, 9, 0, -EIO),
        dual_ready(77, 9, 0, 0, 0, 1),
        dual_ready(77, 9, 0, 0, 1, 0),
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        FakeIo fake;
        fake_add_read(&fake, 0, idle_status());
        fake_add_read(&fake, 0, bad[i]);
        Max9296PrepareInput input = dual_input();
        input.channel_enabled[2] = input.channel_enabled[3] = 0;
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);
        CHECK(max9296_prepare_all(&input, &report, &io) < 0);
        CHECK(report.error < 0 && report.domain[0].error < 0);
        CHECK(fake.domain[0].writes.size() == 2);
        if (fake.domain[0].writes.size() >= 2)
            CHECK(fake.domain[0].writes[1] == "0\n");
    }
}

static void test_final_epoch_must_be_nonzero_and_shared(void)
{
    const uint64_t second_epochs[] = {10, 0};
    for (unsigned case_index = 0; case_index < 2; ++case_index) {
        FakeIo fake;
        for (unsigned i = 0; i < 2; ++i)
            fake_add_read(&fake, i, idle_status());
        fake_add_read(&fake, 0, dual_ready(77, 9));
        fake_add_read(&fake, 1, dual_ready(77, second_epochs[case_index]));
        Max9296PrepareInput input = dual_input();
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);
        CHECK(max9296_prepare_all(&input, &report, &io) < 0);
        CHECK(!fake.domain[0].writes.empty());
        CHECK(!fake.domain[1].writes.empty());
        if (!fake.domain[0].writes.empty())
            CHECK(fake.domain[0].writes.back() == "0\n");
        if (!fake.domain[1].writes.empty())
            CHECK(fake.domain[1].writes.back() == "0\n");
    }
}

static void test_eagain_retries_twice_and_never_attempts_four(void)
{
    FakeIo fake;
    fake_add_read_error(&fake, 0, EAGAIN);
    fake_add_read_error(&fake, 0, EAGAIN);
    fake_add_read(&fake, 0, idle_status());
    fake_add_read(&fake, 0, single_ready(77, 9));
    fake.domain[0].prepare_results.push_back(-EAGAIN);
    fake.domain[0].prepare_results.push_back(-EAGAIN);
    fake.domain[0].prepare_results.push_back(SSIZE_MAX);
    fake.domain[0].prepare_results.push_back(-EIO);
    Max9296PrepareInput input = base_input();
    input.channel_enabled[0] = 1;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == 0);
    CHECK(fake.domain[0].read_index == 4);
    CHECK(fake.domain[0].prepare_index == 3);
    CHECK(fake.domain[0].writes.size() == 3);
    CHECK(fake.sleep_calls == 4);
}

static void test_short_positive_write_fails_without_rollback_ownership(void)
{
    FakeIo fake;
    fake_add_read(&fake, 0, idle_status());
    fake_add_read(&fake, 0, idle_status());
    fake.domain[0].prepare_results.push_back(1);
    Max9296PrepareInput input = base_input();
    input.channel_enabled[0] = 1;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == -EIO);
    CHECK(!report.domain[0].rollback_owned);
    CHECK(fake.domain[0].writes.size() == 1);
}

static void test_second_create_failure_joins_first_and_rolls_it_back(void)
{
    FakeIo fake;
    for (unsigned i = 0; i < 2; ++i)
        fake_add_read(&fake, i, idle_status());
    fake_add_read(&fake, 0, dual_ready(77, 9));
    fake_add_read(&fake, 1, idle_status());
    fake.fail_create_call = 2;
    fake.fail_create_error = EAGAIN;
    Max9296PrepareInput input = dual_input();
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == -EAGAIN);
    CHECK(fake.create_calls == 2 && fake.join_calls == 1);
    CHECK(fake.domain[0].writes.size() == 2);
    if (fake.domain[0].writes.size() >= 2)
        CHECK(fake.domain[0].writes[1] == "0\n");
    CHECK(fake.domain[1].writes.empty());
}

static void test_join_failure_retries_until_worker_is_quiescent(void)
{
    FakeIo fake;
    fake.fail_join_call = 1;
    fake.fail_join_error = EIO;
    fake.hold_prepare_for_join_retry = true;
    fake_add_read(&fake, 0, idle_status());
    fake_add_read(&fake, 0, single_ready(77, 9));
    Max9296PrepareInput input = base_input();
    input.channel_enabled[0] = 1;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == -EIO);
    CHECK(report.error == -EIO);
    CHECK(fake.join_calls == 2);
    CHECK(fake.last_created_joined);
    CHECK(!fake.read_before_quiescence);
    CHECK(fake.active_prepare_writes == 0);
    CHECK(fake.domain[0].writes.size() == 2);
    if (fake.domain[0].writes.size() >= 2)
        CHECK(fake.domain[0].writes[1] == "0\n");

    if (fake.last_created_valid && !fake.last_created_joined) {
        CHECK(pthread_join(fake.last_created_thread, NULL) == 0);
        fake.last_created_joined = true;
    }
}

static void test_nonwarm_final_ready_requires_zero_errno(void)
{
    {
        FakeIo fake;
        fake_add_read(&fake, 0, idle_status());
        fake_add_read(&fake, 0, single_ready(77, 9));
        fake.domain[0].reads[1].text = status_text(
            "READY", 77, 9, "single", "left", 1920, 1080, 15, 1,
            -ESTALE, 0, 1, 1);
        Max9296PrepareInput input = base_input();
        input.channel_enabled[0] = 1;
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);

        CHECK(max9296_prepare_all(&input, &report, &io) == -ESTALE);
        CHECK(report.error == -ESTALE);
        CHECK(report.domain[0].after.last_errno == -ESTALE);
        CHECK(report.domain[0].rollback_owned);
        CHECK(fake.domain[0].writes.size() == 2);
        if (fake.domain[0].writes.size() >= 2)
            CHECK(fake.domain[0].writes[1] == "0\n");
    }
    {
        FakeIo fake;
        fake_add_read(&fake, 0, dual_ready(45, 8, -ESTALE));
        fake_add_read(&fake, 0, dual_ready(45, 9, -ESTALE));
        Max9296PrepareInput input = dual_input();
        input.channel_enabled[2] = input.channel_enabled[3] = 0;
        Max9296PrepareReport report = {};
        const Max9296PrepareIo io = fake_io(&fake);

        CHECK(max9296_prepare_all(&input, &report, &io) == -ESTALE);
        CHECK(report.error == -ESTALE);
        CHECK(report.domain[0].after.last_errno == -ESTALE);
        CHECK(!report.domain[0].rollback_owned);
        CHECK(fake.domain[0].writes.size() == 1);
    }
}

static void test_committed_store_close_error_is_owned_and_rolled_back(void)
{
    FakeIo fake;
    fake.prepare_committed_on_error = true;
    fake.domain[0].prepare_results.push_back(-EAGAIN);
    fake_add_read(&fake, 0, idle_status());
    fake_add_read(&fake, 0, single_ready(77, 9));
    Max9296PrepareInput input = base_input();
    input.channel_enabled[0] = 1;
    Max9296PrepareReport report = {};
    const Max9296PrepareIo io = fake_io(&fake);

    CHECK(max9296_prepare_all(&input, &report, &io) == -EAGAIN);
    CHECK(report.error == -EAGAIN);
    CHECK(report.domain[0].error == -EAGAIN);
    CHECK(report.domain[0].rollback_owned);
    CHECK(report.domain[0].rollback_error == 0);
    CHECK(fake.domain[0].writes.size() == 2);
    CHECK(fake.domain[0].prepare_index == 1);
    CHECK(fake.sleep_calls == 0);
    if (fake.domain[0].writes.size() >= 2)
        CHECK(fake.domain[0].writes[1] == "0\n");
}

static void test_generation_source_is_nonzero_and_changes(void)
{
    const uint64_t first = max9296_prepare_generate_generation();
    const uint64_t second = max9296_prepare_generate_generation();
    CHECK(first != 0 && second != 0 && first != second);
}

int main(void)
{
    test_builds_dual_and_single_targets();
    test_builds_hd_dual_target();
    test_builds_360p_dual_and_single_targets();
    test_builds_left_and_right_single_targets();
    test_accepts_disabled_csi_with_unusable_fps();
    test_rejects_invalid_request_tuples();
    test_parses_ready_and_consumed_samples();
    test_parses_reordered_future_and_idle_samples();
    test_rejects_malformed_status_samples();
    test_owner_lock_is_exclusive_until_released();
    test_classifies_prepare_status_truth_table();
    test_cold_domains_write_in_parallel_with_one_generation();
    test_one_active_domain_creates_one_worker();
    test_hd_dual_coordinator_uses_driver_fingerprint();
    test_legacy_requires_all_active_paths_missing();
    test_warm_consumed_is_reread_without_a_worker();
    test_nonwarm_consumed_write_results_propagate();
    test_ready_refresh_keeps_generation_and_not_rollback_ownership();
    test_peer_failure_rolls_back_only_newly_published_lease();
    test_final_read_failure_cancels_and_preserves_first_error();
    test_final_status_mismatches_fail_and_cancel();
    test_final_epoch_must_be_nonzero_and_shared();
    test_eagain_retries_twice_and_never_attempts_four();
    test_short_positive_write_fails_without_rollback_ownership();
    test_second_create_failure_joins_first_and_rolls_it_back();
    test_join_failure_retries_until_worker_is_quiescent();
    test_nonwarm_final_ready_requires_zero_errno();
    test_committed_store_close_error_is_owned_and_rolled_back();
    test_generation_source_is_nonzero_and_changes();
    printf("max9296 prepare test: %d checks, %d failures -> %s\n", checks,
           failures, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
