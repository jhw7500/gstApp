#include "../max9296Controls.h"

#include <stdint.h>
#include <stdio.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition)                                                \
    do {                                                                \
        ++checks;                                                       \
        if (!(condition)) {                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                    #condition);                                        \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static void check_control(const Max9296ControlWrite &control,
                          uint32_t id, int32_t value)
{
    CHECK(control.id == id);
    CHECK(control.value == value);
}

static void test_sanitizes_common_factor_and_channel_center_independently(void)
{
    uint32_t dz = 99;
    Max9296ZoomCenter center = {65536, 70000};

    const uint32_t changed = max9296_dz_sanitize(&dz) |
                             max9296_zoom_center_sanitize(&center);

    CHECK(changed == (MAX9296_ZOOM_INVALID_DZ |
                      MAX9296_ZOOM_INVALID_X |
                      MAX9296_ZOOM_INVALID_Y));
    CHECK(dz == 100);
    CHECK(center.x == 32768);
    CHECK(center.y == 32768);

    uint32_t boundary_dz = 300;
    Max9296ZoomCenter boundary_center = {0, 65535};
    CHECK(max9296_dz_sanitize(&boundary_dz) == 0);
    CHECK(max9296_zoom_center_sanitize(&boundary_center) == 0);
    CHECK(boundary_dz == 300);
    CHECK(boundary_center.x == 0);
    CHECK(boundary_center.y == 65535);

    CHECK(max9296_dz_sanitize(NULL) == MAX9296_ZOOM_INVALID_DZ);
    CHECK(max9296_zoom_center_sanitize(NULL) ==
          (MAX9296_ZOOM_INVALID_X | MAX9296_ZOOM_INVALID_Y));
}

static void test_builds_one_common_factor_and_dual_slot_centers(void)
{
    const Max9296ZoomCenter centers[2] = {
        {1000, 2000},
        {3000, 4000},
    };
    Max9296ControlWrite controls[5] = {};

    const size_t count =
        max9296_zoom_build_controls(3, 200, centers, controls);

    CHECK(count == 5);
    check_control(controls[0], 0x00981922, 200);
    check_control(controls[1], 0x00981927, 1000);
    check_control(controls[2], 0x00981929, 2000);
    check_control(controls[3], 0x00981928, 3000);
    check_control(controls[4], 0x0098192a, 4000);
}

static void test_single_mode_uses_common_factor_and_active_local_center(void)
{
    const Max9296ZoomCenter centers[2] = {
        {111, 222},
        {333, 444},
    };
    Max9296ControlWrite controls[5] = {};

    CHECK(max9296_zoom_build_controls(1, 150, centers, controls) == 3);
    check_control(controls[0], 0x00981922, 150);
    check_control(controls[1], 0x00981927, 111);
    check_control(controls[2], 0x00981929, 222);

    CHECK(max9296_zoom_build_controls(2, 250, centers, controls) == 3);
    check_control(controls[0], 0x00981922, 250);
    check_control(controls[1], 0x00981928, 333);
    check_control(controls[2], 0x0098192a, 444);
}

static void test_rejects_nonexistent_slot_masks(void)
{
    const Max9296ZoomCenter centers[2] = {{32768, 32768},
                                          {32768, 32768}};
    Max9296ControlWrite controls[5] = {};

    CHECK(max9296_zoom_build_controls(0, 100, centers, controls) == 0);
    CHECK(max9296_zoom_build_controls(4, 100, centers, controls) == 0);
    CHECK(max9296_zoom_build_controls(3, 100, NULL, controls) == 0);
    CHECK(max9296_zoom_build_controls(3, 100, centers, NULL) == 0);
}

static void test_reports_supported_resolution_fps_limits(void)
{
    CHECK(max9296_mode_total_fps_limit(640, 360) == 240);
    CHECK(max9296_mode_total_fps_limit(1280, 720) == 240);
    CHECK(max9296_mode_total_fps_limit(1920, 1080) == 180);
    CHECK(max9296_mode_total_fps_limit(640, 720) == 0);
    CHECK(max9296_mode_total_fps_limit(1280, 360) == 0);
}

static void test_normalizes_strict_crop_enable_boolean(void)
{
    uint8_t invalid = 9;

    CHECK(max9296_crop_enable_normalize(0, 0, 0, &invalid) == 0);
    CHECK(invalid == 0);
    CHECK(max9296_crop_enable_normalize(1, 1, 0, &invalid) == 0);
    CHECK(invalid == 0);
    CHECK(max9296_crop_enable_normalize(1, 1, 1, &invalid) == 1);
    CHECK(invalid == 0);

    CHECK(max9296_crop_enable_normalize(1, 0, 1, &invalid) == 0);
    CHECK(invalid == 1);
    CHECK(max9296_crop_enable_normalize(1, 1, 2, &invalid) == 0);
    CHECK(invalid == 1);
    CHECK(max9296_crop_enable_normalize(1, 1, 1, NULL) == 1);
}

static void test_builds_enable_first_and_sanitized_crop_tuple(void)
{
    const Max9296ZoomCenter centers[2] = {
        {1000, 2000},
        {3000, 4000},
    };
    Max9296CropControlBatch batch = {};

    CHECK(max9296_crop_build_control_batch(1, 3, 200, centers, &batch) == 0);
    check_control(batch.enable, 0x0098192b, 1);
    CHECK(batch.tuple_count == 5);
    check_control(batch.tuple[0], 0x00981922, 200);
    check_control(batch.tuple[1], 0x00981927, 1000);
    check_control(batch.tuple[2], 0x00981929, 2000);
    check_control(batch.tuple[3], 0x00981928, 3000);
    check_control(batch.tuple[4], 0x0098192a, 4000);

    CHECK(max9296_crop_build_control_batch(0, 2, 99, centers, &batch) == 0);
    check_control(batch.enable, 0x0098192b, 0);
    CHECK(batch.tuple_count == 3);
    check_control(batch.tuple[0], 0x00981922, 100);
    check_control(batch.tuple[1], 0x00981928, 3000);
    check_control(batch.tuple[2], 0x0098192a, 4000);

    const Max9296ZoomCenter invalid_centers[2] = {
        {70000, 70001},
        {80000, 80001},
    };
    CHECK(max9296_crop_build_control_batch(
              1, 1, 400, invalid_centers, &batch) == 0);
    check_control(batch.tuple[0], 0x00981922, 100);
    check_control(batch.tuple[1], 0x00981927, 32768);
    check_control(batch.tuple[2], 0x00981929, 32768);

    CHECK(max9296_crop_build_control_batch(1, 0, 100, centers, &batch) == -1);
    CHECK(max9296_crop_build_control_batch(1, 4, 100, centers, &batch) == -1);
    CHECK(max9296_crop_build_control_batch(1, 1, 100, NULL, &batch) == -1);
    CHECK(max9296_crop_build_control_batch(1, 1, 100, centers, NULL) == -1);
}

static void test_selects_high_fps_exposure_policy(void)
{
    CHECK(max9296_exposure_plan(30, 3, 0) ==
          MAX9296_WRITE_EXPOSURE_SEED);
    CHECK(max9296_exposure_plan(31, 3, 3) ==
          MAX9296_SKIP_EXPOSURE_SEED);
    CHECK(max9296_exposure_plan(60, 1, 1) ==
          MAX9296_SKIP_EXPOSURE_SEED);
    CHECK(max9296_exposure_plan(120, 2, 2) ==
          MAX9296_SKIP_EXPOSURE_SEED);
    CHECK(max9296_exposure_plan(31, 3, 1) ==
          MAX9296_REJECT_MANUAL_EXPOSURE);
    CHECK(max9296_exposure_plan(60, 1, 0) ==
          MAX9296_REJECT_MANUAL_EXPOSURE);
    CHECK(max9296_exposure_plan(120, 2, 0) ==
          MAX9296_REJECT_MANUAL_EXPOSURE);
}

int main(void)
{
    test_sanitizes_common_factor_and_channel_center_independently();
    test_builds_one_common_factor_and_dual_slot_centers();
    test_single_mode_uses_common_factor_and_active_local_center();
    test_rejects_nonexistent_slot_masks();
    test_reports_supported_resolution_fps_limits();
    test_normalizes_strict_crop_enable_boolean();
    test_builds_enable_first_and_sanitized_crop_tuple();
    test_selects_high_fps_exposure_policy();
    printf("max9296 controls test: %d checks, %d failures -> %s\n", checks,
           failures, failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
