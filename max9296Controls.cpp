#include "max9296Controls.h"

#include <linux/videodev2.h>

#define V4L2_CID_DZ (V4L2_CID_USER_BASE + 0x1022)
#define V4L2_CID_DZ_X_CH0 (V4L2_CID_USER_BASE + 0x1027)
#define V4L2_CID_DZ_X_CH1 (V4L2_CID_USER_BASE + 0x1028)
#define V4L2_CID_DZ_Y_CH0 (V4L2_CID_USER_BASE + 0x1029)
#define V4L2_CID_DZ_Y_CH1 (V4L2_CID_USER_BASE + 0x102a)
#define V4L2_CID_CROP_ENABLE (V4L2_CID_USER_BASE + 0x102b)

uint32_t max9296_dz_sanitize(uint32_t *dz)
{
    if (!dz)
        return MAX9296_ZOOM_INVALID_DZ;

    if (*dz < MAX9296_DZ_MIN || *dz > MAX9296_DZ_MAX) {
        *dz = MAX9296_DZ_DEFAULT;
        return MAX9296_ZOOM_INVALID_DZ;
    }

    return 0;
}

uint32_t max9296_zoom_center_sanitize(Max9296ZoomCenter *center)
{
    if (!center)
        return MAX9296_ZOOM_INVALID_X | MAX9296_ZOOM_INVALID_Y;

    uint32_t invalid = 0;
    if (center->x > MAX9296_DZ_CENTER_MAX) {
        center->x = MAX9296_DZ_X_DEFAULT;
        invalid |= MAX9296_ZOOM_INVALID_X;
    }
    if (center->y > MAX9296_DZ_CENTER_MAX) {
        center->y = MAX9296_DZ_Y_DEFAULT;
        invalid |= MAX9296_ZOOM_INVALID_Y;
    }
    return invalid;
}

static size_t append_slot_controls(unsigned slot,
                                   const Max9296ZoomCenter &center,
                                   Max9296ControlWrite *controls)
{
    const uint32_t x_id = slot == 0 ? V4L2_CID_DZ_X_CH0
                                    : V4L2_CID_DZ_X_CH1;
    const uint32_t y_id = slot == 0 ? V4L2_CID_DZ_Y_CH0
                                    : V4L2_CID_DZ_Y_CH1;

    controls[0] = {x_id, static_cast<int32_t>(center.x)};
    controls[1] = {y_id, static_cast<int32_t>(center.y)};
    return 2;
}

size_t max9296_zoom_build_controls(uint8_t enabled_slots,
                                   uint32_t common_dz,
                                   const Max9296ZoomCenter centers[2],
                                   Max9296ControlWrite controls[5])
{
    if (!centers || !controls || enabled_slots == 0 || enabled_slots > 3)
        return 0;

    controls[0] = {V4L2_CID_DZ, static_cast<int32_t>(common_dz)};
    size_t count = 1;
    if (enabled_slots & 0x01)
        count += append_slot_controls(0, centers[0], controls + count);
    if (enabled_slots & 0x02)
        count += append_slot_controls(1, centers[1], controls + count);
    return count;
}

uint32_t max9296_mode_total_fps_limit(uint32_t width, uint32_t height)
{
    if ((width == 640 && height == 360) ||
        (width == 1280 && height == 720))
        return 240;
    if (width == 1920 && height == 1080)
        return 180;
    return 0;
}

uint8_t max9296_crop_enable_normalize(uint8_t present,
                                      uint8_t boolean_type,
                                      int value,
                                      uint8_t *invalid)
{
    if (invalid)
        *invalid = 0;
    if (!present)
        return 0;
    if (!boolean_type || (value != 0 && value != 1)) {
        if (invalid)
            *invalid = 1;
        return 0;
    }
    return static_cast<uint8_t>(value);
}

int max9296_crop_build_control_batch(
    uint8_t crop_enable, uint8_t enabled_slots, uint32_t common_dz,
    const Max9296ZoomCenter centers[2], Max9296CropControlBatch *batch)
{
    if (!centers || !batch || enabled_slots == 0 || enabled_slots > 3)
        return -1;

    Max9296ZoomCenter sanitized_centers[2] = {centers[0], centers[1]};
    uint32_t sanitized_dz = common_dz;
    max9296_dz_sanitize(&sanitized_dz);
    max9296_zoom_center_sanitize(&sanitized_centers[0]);
    max9296_zoom_center_sanitize(&sanitized_centers[1]);

    batch->enable = {V4L2_CID_CROP_ENABLE, crop_enable ? 1 : 0};
    /* The driver clusters dz and both channel centers.  Always submit the
     * complete five-control tuple so a single S_EXT_CTRLS transaction cannot
     * observe a mixture of old and new center values, even in single mode. */
    batch->tuple_count = max9296_zoom_build_controls(
        0x03, sanitized_dz, sanitized_centers, batch->tuple);
    return batch->tuple_count ? 0 : -1;
}

int max9296_single_active_slot(uint8_t enabled_slots)
{
    if (enabled_slots == 0x01)
        return 0;
    if (enabled_slots == 0x02)
        return 1;
    return -1;
}

Max9296ExposurePlan max9296_exposure_plan(uint32_t fps,
                                          uint8_t enabled_slots,
                                          uint8_t auto_ae_slots)
{
    if (fps <= 30)
        return MAX9296_WRITE_EXPOSURE_SEED;

    enabled_slots &= 0x03;
    auto_ae_slots &= 0x03;
    if (enabled_slots & static_cast<uint8_t>(~auto_ae_slots))
        return MAX9296_WARN_AND_WRITE_EXPOSURE_SEED;
    return MAX9296_SKIP_EXPOSURE_SEED;
}
