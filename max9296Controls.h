#ifndef MAX9296_CONTROLS_H_
#define MAX9296_CONTROLS_H_

#include <stddef.h>
#include <stdint.h>

enum {
    MAX9296_DZ_MIN = 100,
    MAX9296_DZ_MAX = 300,
    MAX9296_DZ_DEFAULT = 100,
    MAX9296_DZ_CENTER_MAX = 65535,
    MAX9296_DZ_X_DEFAULT = 32768,
    MAX9296_DZ_Y_DEFAULT = 32768,
};

enum Max9296ZoomInvalidField {
    MAX9296_ZOOM_INVALID_DZ = 1U << 0,
    MAX9296_ZOOM_INVALID_X = 1U << 1,
    MAX9296_ZOOM_INVALID_Y = 1U << 2,
};

struct Max9296ZoomCenter {
    uint32_t x;
    uint32_t y;
};

struct Max9296ControlWrite {
    uint32_t id;
    int32_t value;
};

enum Max9296ExposurePlan {
    MAX9296_WRITE_EXPOSURE_SEED,
    MAX9296_WARN_AND_WRITE_EXPOSURE_SEED,
    MAX9296_SKIP_EXPOSURE_SEED,
};

struct Max9296CropControlBatch {
    Max9296ControlWrite enable;
    Max9296ControlWrite tuple[5];
    size_t tuple_count;
};

uint32_t max9296_dz_sanitize(uint32_t *dz);
uint32_t max9296_zoom_center_sanitize(Max9296ZoomCenter *center);
size_t max9296_zoom_build_controls(uint8_t enabled_slots,
                                   uint32_t common_dz,
                                   const Max9296ZoomCenter centers[2],
                                   Max9296ControlWrite controls[5]);
uint32_t max9296_mode_total_fps_limit(uint32_t width, uint32_t height);
uint8_t max9296_crop_enable_normalize(uint8_t present,
                                      uint8_t boolean_type,
                                      int value,
                                      uint8_t *invalid);
int max9296_crop_build_control_batch(
    uint8_t crop_enable, uint8_t enabled_slots, uint32_t common_dz,
    const Max9296ZoomCenter centers[2], Max9296CropControlBatch *batch);
int max9296_single_active_slot(uint8_t enabled_slots);
Max9296ExposurePlan max9296_exposure_plan(uint32_t fps,
                                          uint8_t enabled_slots,
                                          uint8_t auto_ae_slots);

#endif
