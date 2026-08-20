#include "max9296Prepare.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

namespace {

const char *const kPreparePaths[2] = {
    "/sys/bus/i2c/devices/2-0048/prepare",
    "/sys/bus/i2c/devices/1-0048/prepare",
};

enum StatusField {
    FIELD_STATE,
    FIELD_GENERATION,
    FIELD_EPOCH,
    FIELD_MODE,
    FIELD_TABLE,
    FIELD_WIDTH,
    FIELD_HEIGHT,
    FIELD_FPS,
    FIELD_CODE,
    FIELD_ENABLE,
    FIELD_ERRNO,
    FIELD_WORKER_ERRNO,
    FIELD_LEASE,
    FIELD_MATCH,
    FIELD_COUNT,
    FIELD_UNKNOWN = -1,
};

static bool token_equals(const char *text, size_t length, const char *value)
{
    return strlen(value) == length && memcmp(text, value, length) == 0;
}

static StatusField status_field(const char *key, size_t length)
{
    if (token_equals(key, length, "state")) return FIELD_STATE;
    if (token_equals(key, length, "generation")) return FIELD_GENERATION;
    if (token_equals(key, length, "epoch")) return FIELD_EPOCH;
    if (token_equals(key, length, "mode")) return FIELD_MODE;
    if (token_equals(key, length, "table")) return FIELD_TABLE;
    if (token_equals(key, length, "width")) return FIELD_WIDTH;
    if (token_equals(key, length, "height")) return FIELD_HEIGHT;
    if (token_equals(key, length, "fps")) return FIELD_FPS;
    if (token_equals(key, length, "code")) return FIELD_CODE;
    if (token_equals(key, length, "enable")) return FIELD_ENABLE;
    if (token_equals(key, length, "errno")) return FIELD_ERRNO;
    if (token_equals(key, length, "worker_errno")) return FIELD_WORKER_ERRNO;
    if (token_equals(key, length, "lease")) return FIELD_LEASE;
    if (token_equals(key, length, "match")) return FIELD_MATCH;
    return FIELD_UNKNOWN;
}

static bool copy_token(char *destination, size_t capacity,
                       const char *source, size_t length)
{
    if (length == 0 || length >= capacity)
        return false;
    memcpy(destination, source, length);
    destination[length] = '\0';
    return true;
}

static bool parse_unsigned(const char *source, size_t length, bool code,
                           uint64_t *value)
{
    char text[32];
    int base = 10;

    if (!source || !value || length == 0 || length >= sizeof(text))
        return false;
    if (code && length > 2 && source[0] == '0' &&
        (source[1] == 'x' || source[1] == 'X')) {
        base = 16;
        for (size_t i = 2; i < length; ++i)
            if (!isxdigit(static_cast<unsigned char>(source[i])))
                return false;
    } else {
        for (size_t i = 0; i < length; ++i)
            if (!isdigit(static_cast<unsigned char>(source[i])))
                return false;
    }

    memcpy(text, source, length);
    text[length] = '\0';
    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = strtoull(text, &end, base);
    if (errno == ERANGE || end != text + length)
        return false;
    *value = static_cast<uint64_t>(parsed);
    return true;
}

static bool parse_uint32(const char *source, size_t length, bool code,
                         uint32_t *value)
{
    uint64_t parsed = 0;
    if (!parse_unsigned(source, length, code, &parsed) ||
        parsed > UINT32_MAX)
        return false;
    *value = static_cast<uint32_t>(parsed);
    return true;
}

static bool parse_int(const char *source, size_t length, int *value)
{
    char text[32];
    size_t first_digit = 0;

    if (!source || !value || length == 0 || length >= sizeof(text))
        return false;
    if (source[0] == '-') {
        if (length == 1)
            return false;
        first_digit = 1;
    }
    for (size_t i = first_digit; i < length; ++i)
        if (!isdigit(static_cast<unsigned char>(source[i])))
            return false;

    memcpy(text, source, length);
    text[length] = '\0';
    errno = 0;
    char *end = NULL;
    const long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end != text + length || parsed < INT_MIN ||
        parsed > INT_MAX)
        return false;
    *value = static_cast<int>(parsed);
    return true;
}

static bool parse_state(const char *source, size_t length,
                        Max9296PrepareState *state)
{
    if (token_equals(source, length, "IDLE")) {
        *state = MAX9296_STATE_IDLE;
    } else if (token_equals(source, length, "PREPARING")) {
        *state = MAX9296_STATE_PREPARING;
    } else if (token_equals(source, length, "READY")) {
        *state = MAX9296_STATE_READY;
    } else if (token_equals(source, length, "STALE")) {
        *state = MAX9296_STATE_STALE;
    } else if (token_equals(source, length, "CONSUMED")) {
        *state = MAX9296_STATE_CONSUMED;
    } else if (token_equals(source, length, "FAILED")) {
        *state = MAX9296_STATE_FAILED;
    } else if (token_equals(source, length, "EXPIRED")) {
        *state = MAX9296_STATE_EXPIRED;
    } else {
        return false;
    }
    return true;
}

static bool has_current_fingerprint(const Max9296PrepareTarget *target,
                                    const Max9296PrepareStatus *status)
{
    return target->mode && target->table &&
           strcmp(status->mode, target->mode) == 0 &&
           strcmp(status->table, target->table) == 0 &&
           status->width == target->width && status->height == target->height &&
           status->fps == target->fps && status->code == 0x2006 &&
           status->enable == target->enable;
}

static ssize_t posix_read_file(void *, const char *path, char *buffer,
                               size_t capacity)
{
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    const ssize_t result = read(fd, buffer, capacity);
    const int operation_error = result < 0 ? errno : 0;
    const int close_result = close(fd);
    if (result < 0)
        return -operation_error;
    if (close_result != 0)
        return -errno;
    return result;
}

static ssize_t posix_write_file_with_commit(void *, const char *path,
                                            const char *buffer, size_t length,
                                            bool *committed)
{
    *committed = false;
    const int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    const ssize_t result = write(fd, buffer, length);
    const int operation_error = result < 0 ? errno : 0;
    if (result == static_cast<ssize_t>(length))
        *committed = true;
    const int close_result = close(fd);
    if (result < 0)
        return -operation_error;
    if (close_result != 0)
        return -errno;
    return result;
}

static ssize_t posix_write_file(void *context, const char *path,
                                const char *buffer, size_t length)
{
    bool committed = false;
    return posix_write_file_with_commit(context, path, buffer, length,
                                        &committed);
}

static uint64_t posix_monotonic_ns(void *)
{
    struct timespec value = {};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return 0;
    return static_cast<uint64_t>(value.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(value.tv_nsec);
}

static void posix_sleep_ms(void *, unsigned milliseconds)
{
    struct timespec remaining = {};
    remaining.tv_sec = milliseconds / 1000;
    remaining.tv_nsec = static_cast<long>(milliseconds % 1000) * 1000000L;
    while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
    }
}

static int posix_thread_create(void *, pthread_t *thread,
                               void *(*entry)(void *), void *argument)
{
    return pthread_create(thread, NULL, entry, argument);
}

static int posix_thread_join(void *, pthread_t thread, void **result)
{
    return pthread_join(thread, result);
}

static const Max9296PrepareIo kDefaultIo = {
    posix_read_file,
    posix_write_file,
    posix_monotonic_ns,
    posix_sleep_ms,
    posix_thread_create,
    posix_thread_join,
    NULL,
    posix_write_file_with_commit,
};

static bool valid_io(const Max9296PrepareIo *io)
{
    return io && io->read_file &&
           (io->write_file || io->write_file_with_commit) &&
           io->monotonic_ns &&
           io->sleep_ms && io->thread_create && io->thread_join;
}

static int pthread_error(int result)
{
    return result > 0 ? -result : result;
}

struct WriteResult {
    ssize_t result;
    bool committed;
};

static WriteResult retry_write(const Max9296PrepareIo *io, const char *path,
                               const char *buffer, size_t length)
{
    WriteResult outcome = {-EIO, false};
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        bool committed = false;
        outcome.result = io->write_file_with_commit
                             ? io->write_file_with_commit(
                                   io->context, path, buffer, length,
                                   &committed)
                             : io->write_file(io->context, path, buffer,
                                              length);
        outcome.committed = committed ||
                            outcome.result == static_cast<ssize_t>(length);
        if (outcome.committed || outcome.result != -EAGAIN)
            return outcome;
        if (attempt != 2)
            io->sleep_ms(io->context, 100);
    }
    return outcome;
}

static int read_status(const Max9296PrepareIo *io, const char *path,
                       Max9296PrepareStatus *status)
{
    char buffer[1024];
    ssize_t result = -EIO;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        result = io->read_file(io->context, path, buffer, sizeof(buffer));
        if (result != -EAGAIN)
            break;
        if (attempt != 2)
            io->sleep_ms(io->context, 100);
    }
    if (result < 0)
        return static_cast<int>(result);
    if (static_cast<size_t>(result) > sizeof(buffer))
        return -EOVERFLOW;
    return max9296_prepare_parse_status(buffer, static_cast<size_t>(result),
                                        status);
}

struct PrepareWorker {
    const Max9296PrepareIo *io;
    const Max9296PrepareTarget *target;
    Max9296PrepareDomainReport *report;
    uint64_t generation;
    bool preexisting_lease;
};

static void *prepare_worker_entry(void *argument)
{
    PrepareWorker *const worker = static_cast<PrepareWorker *>(argument);
    char command[128];
    const int command_length = snprintf(
        command, sizeof(command), "1 %llu %u %u %u %u\n",
        static_cast<unsigned long long>(worker->generation),
        worker->target->width, worker->target->height, worker->target->fps,
        worker->target->enable);
    if (command_length <= 0 ||
        static_cast<size_t>(command_length) >= sizeof(command)) {
        worker->report->error = -EOVERFLOW;
        worker->report->action = MAX9296_ACTION_FAILED;
        return NULL;
    }

    const uint64_t started = worker->io->monotonic_ns(worker->io->context);
    const WriteResult write_result = retry_write(
        worker->io, worker->target->path, command,
        static_cast<size_t>(command_length));
    const uint64_t finished = worker->io->monotonic_ns(worker->io->context);
    worker->report->elapsed_ns = finished >= started ? finished - started : 0;

    if (!worker->preexisting_lease && write_result.committed)
        worker->report->rollback_owned = true;
    if (write_result.result != command_length) {
        worker->report->error = write_result.result < 0
                                    ? static_cast<int>(write_result.result)
                                    : -EIO;
        worker->report->action = MAX9296_ACTION_FAILED;
    } else {
        worker->report->action = worker->preexisting_lease
                                     ? MAX9296_ACTION_READY_REFRESHED
                                     : MAX9296_ACTION_COLD_PREPARED;
    }
    return NULL;
}

static int failed_disposition_error(const Max9296PrepareStatus *status)
{
    if (status->worker_errno != 0)
        return status->worker_errno < 0 ? status->worker_errno : -EIO;
    return -ESTALE;
}

static int validate_final_status(const Max9296PrepareTarget *target,
                                 const Max9296PrepareStatus *before,
                                 const Max9296PrepareStatus *after,
                                 Max9296PrepareDisposition disposition,
                                 uint64_t generation)
{
    const bool warm = disposition == MAX9296_DISPOSITION_WARM;
    const uint64_t expected_generation =
        disposition == MAX9296_DISPOSITION_NEW_PREPARE
            ? generation
            : before->generation;
    if (!warm && after->last_errno != 0)
        return after->last_errno < 0 ? after->last_errno : -EIO;
    if ((warm && after->state != MAX9296_STATE_CONSUMED) ||
        (!warm && after->state != MAX9296_STATE_READY) ||
        after->generation != expected_generation || after->epoch == 0 ||
        !has_current_fingerprint(target, after) ||
        after->worker_errno != 0 || after->lease != (warm ? 0U : 1U) ||
        after->match != 1)
        return after->worker_errno != 0
                   ? (after->worker_errno < 0 ? after->worker_errno : -EIO)
                   : -ESTALE;
    return 0;
}

static void remember_error(int error, int *first_error)
{
    if (error < 0 && *first_error == 0)
        *first_error = error;
}

}  // namespace

const char *max9296_prepare_path(unsigned csi)
{
    return csi < 2 ? kPreparePaths[csi] : NULL;
}

int max9296_prepare_build_targets(const Max9296PrepareInput *input,
                                  Max9296PrepareTarget targets[2])
{
    if (!input || !targets || input->generation == 0 ||
        !((input->width == 1280 && input->height == 720) ||
          (input->width == 1920 && input->height == 1080)))
        return -EINVAL;

    for (unsigned channel = 0; channel < 4; ++channel)
        if (input->channel_enabled[channel] > 1)
            return -EINVAL;

    for (unsigned csi = 0; csi < 2; ++csi) {
        const uint32_t enable = input->channel_enabled[csi * 2] |
                                (input->channel_enabled[csi * 2 + 1] << 1);
        Max9296PrepareTarget &target = targets[csi];
        target.active = enable != 0;
        target.csi = csi;
        target.path = max9296_prepare_path(csi);
        target.width = enable == 3 ? input->width * 2 : input->width;
        target.height = input->height;
        target.fps = input->fps[csi];
        target.enable = enable;
        target.mode = enable == 3 ? "dual-wide"
                                  : (enable == 0 ? "none" : "single");
        target.table = enable == 3 ? "dual"
                                   : (enable == 1 ? "left"
                                                  : (enable == 2 ? "right"
                                                                 : "none"));
        if (target.active && (target.fps == 0 || target.fps > 120))
            return -EINVAL;
    }

    if (targets[0].active && targets[1].active &&
        targets[0].fps != targets[1].fps)
        return -EINVAL;
    return 0;
}

int max9296_prepare_parse_status(const char *line, size_t length,
                                 Max9296PrepareStatus *status)
{
    if (!line || !status)
        return -EINVAL;

    Max9296PrepareStatus parsed = {};
    bool seen[FIELD_COUNT] = {};
    size_t offset = 0;
    while (offset < length) {
        while (offset < length && isspace(static_cast<unsigned char>(line[offset])))
            ++offset;
        if (offset == length)
            break;

        const size_t token_start = offset;
        while (offset < length && !isspace(static_cast<unsigned char>(line[offset])))
            ++offset;
        const size_t token_end = offset;
        size_t equal = token_start;
        while (equal < token_end && line[equal] != '=')
            ++equal;
        if (equal == token_start || equal == token_end)
            return -EINVAL;

        const StatusField field = status_field(line + token_start,
                                               equal - token_start);
        if (field == FIELD_UNKNOWN)
            continue;
        if (seen[field])
            return -EINVAL;
        seen[field] = true;

        const char *const value = line + equal + 1;
        const size_t value_length = token_end - equal - 1;
        bool valid = false;
        switch (field) {
        case FIELD_STATE:
            valid = parse_state(value, value_length, &parsed.state);
            break;
        case FIELD_GENERATION:
            valid = parse_unsigned(value, value_length, false,
                                   &parsed.generation);
            break;
        case FIELD_EPOCH:
            valid = parse_unsigned(value, value_length, false, &parsed.epoch);
            break;
        case FIELD_MODE:
            valid = copy_token(parsed.mode, sizeof(parsed.mode), value,
                               value_length);
            break;
        case FIELD_TABLE:
            valid = copy_token(parsed.table, sizeof(parsed.table), value,
                               value_length);
            break;
        case FIELD_WIDTH:
            valid = parse_uint32(value, value_length, false, &parsed.width);
            break;
        case FIELD_HEIGHT:
            valid = parse_uint32(value, value_length, false, &parsed.height);
            break;
        case FIELD_FPS:
            valid = parse_uint32(value, value_length, false, &parsed.fps);
            break;
        case FIELD_CODE:
            valid = parse_uint32(value, value_length, true, &parsed.code);
            break;
        case FIELD_ENABLE:
            valid = parse_uint32(value, value_length, false, &parsed.enable);
            break;
        case FIELD_ERRNO:
            valid = parse_int(value, value_length, &parsed.last_errno);
            break;
        case FIELD_WORKER_ERRNO:
            valid = parse_int(value, value_length, &parsed.worker_errno);
            break;
        case FIELD_LEASE:
            valid = parse_uint32(value, value_length, false, &parsed.lease) &&
                    parsed.lease <= 1;
            break;
        case FIELD_MATCH:
            valid = parse_uint32(value, value_length, false, &parsed.match) &&
                    parsed.match <= 1;
            break;
        default:
            return -EINVAL;
        }
        if (!valid)
            return -EINVAL;
    }

    for (unsigned field = 0; field < FIELD_COUNT; ++field)
        if (!seen[field])
            return -EINVAL;
    *status = parsed;
    return 0;
}

int max9296_prepare_acquire_owner_lock(const char *path)
{
    const int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
        return -errno;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int result = -errno;
        close(fd);
        return result;
    }
    return fd;
}

void max9296_prepare_release_owner_lock(int fd)
{
    if (fd >= 0)
        close(fd);
}

int max9296_prepare_classify(const Max9296PrepareTarget *target,
                             const Max9296PrepareStatus *status,
                             Max9296PrepareDisposition *disposition)
{
    if (!target || !status || !disposition)
        return -EINVAL;

    *disposition = MAX9296_DISPOSITION_FAIL;
    if (status->state == MAX9296_STATE_PREPARING)
        return -EBUSY;
    if (status->lease != 0) {
        if (status->state != MAX9296_STATE_READY)
            return -EPROTO;
        if (status->match != 0 && status->worker_errno == 0 &&
            status->generation != 0 && status->epoch != 0 &&
            has_current_fingerprint(target, status))
            *disposition = MAX9296_DISPOSITION_REFRESH_READY;
        return 0;
    }

    if (status->state == MAX9296_STATE_CONSUMED) {
        if (status->worker_errno != 0)
            return 0;
        if (status->match != 0 && status->generation != 0 &&
            status->epoch != 0 && has_current_fingerprint(target, status))
            *disposition = MAX9296_DISPOSITION_WARM;
        else
            *disposition = MAX9296_DISPOSITION_NEW_PREPARE;
        return 0;
    }

    if (status->state == MAX9296_STATE_READY)
        return 0;
    if (status->worker_errno == 0 &&
        (status->state == MAX9296_STATE_IDLE ||
         status->state == MAX9296_STATE_FAILED ||
         status->state == MAX9296_STATE_EXPIRED ||
         status->state == MAX9296_STATE_STALE))
        *disposition = MAX9296_DISPOSITION_NEW_PREPARE;
    return 0;
}

uint64_t max9296_prepare_generate_generation(void)
{
    static uint64_t last_generation = 0;
    struct timespec value = {};
    uint64_t candidate = 1;
    if (clock_gettime(CLOCK_MONOTONIC, &value) == 0) {
        candidate = static_cast<uint64_t>(value.tv_sec) * 1000000000ULL +
                    static_cast<uint64_t>(value.tv_nsec);
        candidate ^= static_cast<uint64_t>(getpid()) << 32;
        if (candidate == 0)
            candidate = 1;
    }

    uint64_t observed = __atomic_load_n(&last_generation, __ATOMIC_RELAXED);
    for (;;) {
        const uint64_t next = candidate > observed ? candidate : observed + 1;
        if (__atomic_compare_exchange_n(&last_generation, &observed, next,
                                        false, __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED))
            return next == 0 ? 1 : next;
    }
}

int max9296_prepare_all(const Max9296PrepareInput *input,
                        Max9296PrepareReport *report,
                        const Max9296PrepareIo *provided_io)
{
    if (!input || !report)
        return -EINVAL;
    const Max9296PrepareIo *const io = provided_io ? provided_io : &kDefaultIo;
    if (!valid_io(io))
        return -EINVAL;

    memset(report, 0, sizeof(*report));
    report->generation = input->generation;
    for (unsigned csi = 0; csi < 2; ++csi)
        report->domain[csi].action = MAX9296_ACTION_SKIPPED;

    Max9296PrepareTarget target[2] = {};
    int first_error = max9296_prepare_build_targets(input, target);
    if (first_error < 0) {
        report->error = first_error;
        return first_error;
    }

    unsigned active_count = 0;
    unsigned missing_count = 0;
    for (unsigned csi = 0; csi < 2; ++csi) {
        Max9296PrepareDomainReport &domain = report->domain[csi];
        domain.active = target[csi].active;
        if (!target[csi].active)
            continue;
        ++active_count;
        const int result = read_status(io, target[csi].path, &domain.before);
        if (result == -ENOENT)
            ++missing_count;
        if (result < 0) {
            domain.error = result;
            domain.action = MAX9296_ACTION_FAILED;
            remember_error(result, &first_error);
        }
    }

    if (active_count != 0 && missing_count == active_count) {
        report->legacy_fallback = true;
        report->error = 0;
        for (unsigned csi = 0; csi < 2; ++csi) {
            if (target[csi].active) {
                report->domain[csi].action = MAX9296_ACTION_LEGACY;
                report->domain[csi].error = 0;
            }
        }
        return MAX9296_PREPARE_LEGACY;
    }
    if (first_error < 0) {
        report->error = first_error;
        return first_error;
    }

    Max9296PrepareDisposition disposition[2] = {
        MAX9296_DISPOSITION_FAIL,
        MAX9296_DISPOSITION_FAIL,
    };
    for (unsigned csi = 0; csi < 2; ++csi) {
        Max9296PrepareDomainReport &domain = report->domain[csi];
        if (!target[csi].active)
            continue;
        const int result = max9296_prepare_classify(
            &target[csi], &domain.before, &disposition[csi]);
        if (result < 0 || disposition[csi] == MAX9296_DISPOSITION_FAIL) {
            domain.error = result < 0 ? result
                                      : failed_disposition_error(&domain.before);
            domain.action = MAX9296_ACTION_FAILED;
            remember_error(domain.error, &first_error);
        } else if (disposition[csi] == MAX9296_DISPOSITION_WARM) {
            domain.action = MAX9296_ACTION_WARM_REUSED;
        }
    }
    if (first_error < 0) {
        report->error = first_error;
        return first_error;
    }

    pthread_t thread[2] = {};
    bool created[2] = {};
    PrepareWorker worker[2] = {};
    for (unsigned csi = 0; csi < 2; ++csi) {
        if (!target[csi].active ||
            disposition[csi] == MAX9296_DISPOSITION_WARM)
            continue;
        const bool preexisting_lease =
            disposition[csi] == MAX9296_DISPOSITION_REFRESH_READY;
        const uint64_t request_generation = preexisting_lease
                                                ? report->domain[csi]
                                                      .before.generation
                                                : report->generation;
        worker[csi].io = io;
        worker[csi].target = &target[csi];
        worker[csi].report = &report->domain[csi];
        worker[csi].generation = request_generation;
        worker[csi].preexisting_lease = preexisting_lease;
        const int result = io->thread_create(io->context, &thread[csi],
                                             prepare_worker_entry,
                                             &worker[csi]);
        if (result != 0) {
            const int error = pthread_error(result);
            report->domain[csi].error = error;
            report->domain[csi].action = MAX9296_ACTION_FAILED;
            remember_error(error, &first_error);
            break;
        }
        created[csi] = true;
    }

    for (unsigned csi = 0; csi < 2; ++csi) {
        if (!created[csi])
            continue;
        int join_error = 0;
        for (;;) {
            const int result = io->thread_join(io->context, thread[csi], NULL);
            if (result == 0)
                break;
            const int error = pthread_error(result);
            if (join_error == 0)
                join_error = error;
            /* A failed join proves nothing about worker liveness.  Retrying
             * until success is an intentional fail-stop: the coordinator
             * must not inspect worker-owned state, rollback, or return while
             * quiescence is uncertain. */
            io->sleep_ms(io->context, 100);
        }
        if (join_error != 0) {
            if (report->domain[csi].error == 0)
                report->domain[csi].error = join_error;
            remember_error(join_error, &first_error);
        }
    }
    for (unsigned csi = 0; csi < 2; ++csi)
        if (target[csi].active)
            remember_error(report->domain[csi].error, &first_error);

    for (unsigned csi = 0; csi < 2; ++csi) {
        Max9296PrepareDomainReport &domain = report->domain[csi];
        if (!target[csi].active)
            continue;
        const int read_result = read_status(io, target[csi].path, &domain.after);
        int result = read_result;
        if (result == 0)
            result = validate_final_status(&target[csi], &domain.before,
                                           &domain.after, disposition[csi],
                                           report->generation);
        if (result < 0) {
            if (domain.error == 0)
                domain.error = result;
            remember_error(result, &first_error);
        }
    }

    uint64_t epoch = 0;
    for (unsigned csi = 0; csi < 2; ++csi) {
        Max9296PrepareDomainReport &domain = report->domain[csi];
        if (!target[csi].active || domain.after.epoch == 0)
            continue;
        if (epoch == 0) {
            epoch = domain.after.epoch;
        } else if (domain.after.epoch != epoch) {
            if (domain.error == 0)
                domain.error = -ESTALE;
            remember_error(-ESTALE, &first_error);
        }
    }

    if (first_error < 0) {
        for (unsigned csi = 0; csi < 2; ++csi) {
            Max9296PrepareDomainReport &domain = report->domain[csi];
            if (!domain.rollback_owned)
                continue;
            static const char cancel[] = "0\n";
            const WriteResult result = retry_write(
                io, target[csi].path, cancel, sizeof(cancel) - 1);
            if (result.result !=
                static_cast<ssize_t>(sizeof(cancel) - 1))
                domain.rollback_error = result.result < 0
                                            ? static_cast<int>(result.result)
                                            : -EIO;
        }
        report->error = first_error;
        return first_error;
    }

    report->error = 0;
    return MAX9296_PREPARE_OK;
}
