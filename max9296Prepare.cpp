#include "max9296Prepare.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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

    const char *const mode = input->width == 1280 ? "hd" : "fhd";
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
        target.mode = mode;
        target.table = enable == 3 ? "dual" : "single";
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
