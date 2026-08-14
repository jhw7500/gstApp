#ifndef MAX9296_PREPARE_H_
#define MAX9296_PREPARE_H_

#include <stddef.h>
#include <stdint.h>

enum Max9296PrepareState {
    MAX9296_STATE_IDLE,
    MAX9296_STATE_PREPARING,
    MAX9296_STATE_READY,
    MAX9296_STATE_STALE,
    MAX9296_STATE_CONSUMED,
    MAX9296_STATE_FAILED,
    MAX9296_STATE_EXPIRED
};

enum Max9296PrepareAction {
    MAX9296_ACTION_SKIPPED,
    MAX9296_ACTION_LEGACY,
    MAX9296_ACTION_WARM_REUSED,
    MAX9296_ACTION_READY_REFRESHED,
    MAX9296_ACTION_COLD_PREPARED,
    MAX9296_ACTION_FAILED
};

struct Max9296PrepareInput {
    uint32_t width;
    uint32_t height;
    uint32_t fps[2];
    uint8_t channel_enabled[4];
    uint64_t generation;
};

struct Max9296PrepareStatus {
    Max9296PrepareState state;
    uint64_t generation;
    uint64_t epoch;
    char mode[16];
    char table[16];
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t code;
    uint32_t enable;
    int last_errno;
    int worker_errno;
    uint32_t lease;
    uint32_t match;
};

struct Max9296PrepareTarget {
    bool active;
    unsigned csi;
    const char *path;
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t enable;
    const char *mode;
    const char *table;
};

int max9296_prepare_build_targets(const Max9296PrepareInput *input,
                                  Max9296PrepareTarget targets[2]);
int max9296_prepare_parse_status(const char *line, size_t length,
                                 Max9296PrepareStatus *status);
const char *max9296_prepare_path(unsigned csi);

#endif
