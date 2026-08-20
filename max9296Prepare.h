#ifndef MAX9296_PREPARE_H_
#define MAX9296_PREPARE_H_

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>

#define MAX9296_PREPARE_OWNER_LOCK "/run/lock/gstapp-camera.lock"

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

enum Max9296PrepareDisposition {
    MAX9296_DISPOSITION_WARM,
    MAX9296_DISPOSITION_REFRESH_READY,
    MAX9296_DISPOSITION_NEW_PREPARE,
    MAX9296_DISPOSITION_FAIL
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

struct Max9296PrepareIo {
    ssize_t (*read_file)(void *context, const char *path,
                         char *buffer, size_t capacity);
    ssize_t (*write_file)(void *context, const char *path,
                          const char *buffer, size_t length);
    uint64_t (*monotonic_ns)(void *context);
    void (*sleep_ms)(void *context, unsigned milliseconds);
    int (*thread_create)(void *context, pthread_t *thread,
                         void *(*entry)(void *), void *argument);
    /* Zero is the only quiescence proof. A nonzero result means the worker's
     * liveness is unknown and the coordinator will retry rather than return. */
    int (*thread_join)(void *context, pthread_t thread, void **result);
    void *context;
    /* Optional extended write contract. `committed` is true once the complete
     * sysfs store occurred, even if later descriptor cleanup fails. */
    ssize_t (*write_file_with_commit)(void *context, const char *path,
                                      const char *buffer, size_t length,
                                      bool *committed);
};

struct Max9296PrepareDomainReport {
    bool active;
    bool rollback_owned;
    Max9296PrepareAction action;
    int error;
    int rollback_error;
    uint64_t elapsed_ns;
    Max9296PrepareStatus before;
    Max9296PrepareStatus after;
};

struct Max9296PrepareReport {
    bool legacy_fallback;
    uint64_t generation;
    int error;
    Max9296PrepareDomainReport domain[2];
};

enum {
    MAX9296_PREPARE_OK = 0,
    MAX9296_PREPARE_LEGACY = 1
};

int max9296_prepare_build_targets(const Max9296PrepareInput *input,
                                  Max9296PrepareTarget targets[2]);
int max9296_prepare_parse_status(const char *line, size_t length,
                                 Max9296PrepareStatus *status);
const char *max9296_prepare_path(unsigned csi);
int max9296_prepare_acquire_owner_lock(const char *path);
void max9296_prepare_release_owner_lock(int fd);
int max9296_prepare_classify(const Max9296PrepareTarget *target,
                             const Max9296PrepareStatus *status,
                             Max9296PrepareDisposition *disposition);
uint64_t max9296_prepare_generate_generation(void);
int max9296_prepare_all(const Max9296PrepareInput *input,
                        Max9296PrepareReport *report,
                        const Max9296PrepareIo *io);

#endif
