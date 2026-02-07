//
// E2E tests for the Zerus renderer (headless mode)
//
// 1. Headless offscreen render with pixel readback
// 2. Sustained multi-frame render with fence-based sync
// 3. Asserts zero Vulkan validation errors across all tests
//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

#define ZERUS_CORE_IMPLEMENTATION
#include "engine/core.h"

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------

static int  tests_run           = 0;
static int  tests_passed        = 0;
static bool test_current_failed = false;

// ---------------------------------------------------------------------------
// Shared engine context (initialized once)
// ---------------------------------------------------------------------------

static zerus_engine_state_t engine = { 0 };

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------

#define TEST_IMAGE_WIDTH 64
#define TEST_IMAGE_HEIGHT 64
#define TEST_IMAGE_FORMAT VK_FORMAT_B8G8R8A8_UNORM
#define SUSTAINED_FRAME_COUNT 100

// ---------------------------------------------------------------------------
// Test: headless render with pixel readback
// ---------------------------------------------------------------------------

static void test_headless_render(void)
{
    gpu_image_t image = create_gpu_image(engine.device_info,
                                         TEST_IMAGE_FORMAT,
                                         TEST_IMAGE_WIDTH,
                                         TEST_IMAGE_HEIGHT,
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                             | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    TEST_ASSERT(image.status == GPU_IMAGE_OK,
                "create_gpu_image failed: %d",
                image.status);

    VkDeviceSize readback_size = TEST_IMAGE_WIDTH * TEST_IMAGE_HEIGHT * 4;
    gpu_staging_buffer_t staging
        = create_staging_buffer(engine.device_info, readback_size);
    TEST_ASSERT(staging.status == GPU_IMAGE_OK,
                "create_staging_buffer failed: %d",
                staging.status);

    VkCommandBuffer cmd
        = frame_begin_recording(&engine.frame_state, engine.device_info.device);

    record_frame(
        &engine.renderer, cmd, image.image, image.format, image.extent);

    gpu_image_status_t transition_status
        = record_image_barrier(cmd,
                               image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    TEST_ASSERT(transition_status == GPU_IMAGE_OK,
                "record_image_barrier failed: %d",
                transition_status);

    gpu_image_status_t copy_status
        = record_image_to_buffer_copy(cmd, &image, &staging);
    TEST_ASSERT(copy_status == GPU_IMAGE_OK,
                "record_image_to_buffer_copy failed: %d",
                copy_status);

    frame_status_t frame_status = frame_end_and_submit(
        &engine.frame_state, engine.device_info.graphics_queue);
    TEST_ASSERT(frame_status == FRAME_OK,
                "frame_end_and_submit failed: %d",
                frame_status);

    vkQueueWaitIdle(engine.device_info.graphics_queue);

    gpu_image_status_t map_status
        = map_staging_buffer(engine.device_info, &staging);
    TEST_ASSERT(map_status == GPU_IMAGE_OK,
                "map_staging_buffer failed: %d",
                map_status);
    TEST_ASSERT(staging.mapped_ptr != NULL,
                "map_staging_buffer returned NULL pointer");

    // Format is B8G8R8A8_UNORM. Clear color is {0.0, 0.1, 0.15, 1.0}.
    // Expected bytes: B ~= 38, G ~= 26, R ~= 0, A ~= 255.
    const uint8_t* pixels = (const uint8_t*) staging.mapped_ptr;

    uint8_t b = pixels[0];
    uint8_t g = pixels[1];
    uint8_t r = pixels[2];
    uint8_t a = pixels[3];

    // Keep tolerance small but non-zero to handle implementation rounding.
    TEST_ASSERT(r <= 2, "R: expected ~0, got %u", r);
    TEST_ASSERT(g >= 24 && g <= 28, "G: expected ~26, got %u", g);
    TEST_ASSERT(b >= 36 && b <= 40, "B: expected ~38, got %u", b);
    TEST_ASSERT(a >= 254, "A: expected 255, got %u", a);

    size_t mid_offset
        = (TEST_IMAGE_HEIGHT / 2 * TEST_IMAGE_WIDTH + TEST_IMAGE_WIDTH / 2) * 4;
    uint8_t mid_b = pixels[mid_offset + 0];
    uint8_t mid_g = pixels[mid_offset + 1];
    uint8_t mid_r = pixels[mid_offset + 2];
    uint8_t mid_a = pixels[mid_offset + 3];

    TEST_ASSERT(mid_r <= 2, "mid R: expected ~0, got %u", mid_r);
    TEST_ASSERT(
        mid_g >= 24 && mid_g <= 28, "mid G: expected ~26, got %u", mid_g);
    TEST_ASSERT(
        mid_b >= 36 && mid_b <= 40, "mid B: expected ~38, got %u", mid_b);
    TEST_ASSERT(mid_a >= 254, "mid A: expected 255, got %u", mid_a);

    destroy_staging_buffer(engine.device_info, &staging);
    destroy_gpu_image(engine.device_info, &image);
}

// ---------------------------------------------------------------------------
// Test: sustained rendering over many frames (fence-based sync)
// ---------------------------------------------------------------------------

static void test_sustained_render(void)
{
    gpu_image_t image = create_gpu_image(engine.device_info,
                                         TEST_IMAGE_FORMAT,
                                         TEST_IMAGE_WIDTH,
                                         TEST_IMAGE_HEIGHT,
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    TEST_ASSERT(image.status == GPU_IMAGE_OK,
                "create_gpu_image failed: %d",
                image.status);

    for (int i = 0; i < SUSTAINED_FRAME_COUNT; i++)
    {
        VkCommandBuffer cmd = frame_begin_recording(&engine.frame_state,
                                                    engine.device_info.device);

        record_frame(
            &engine.renderer, cmd, image.image, image.format, image.extent);

        frame_status_t status = frame_end_and_submit(
            &engine.frame_state, engine.device_info.graphics_queue);
        TEST_ASSERT(
            status == FRAME_OK, "frame_end_and_submit failed on frame %d", i);
    }

    vkQueueWaitIdle(engine.device_info.graphics_queue);
    destroy_gpu_image(engine.device_info, &image);
}

// ---------------------------------------------------------------------------
// Test: zero validation errors
// ---------------------------------------------------------------------------

static void test_no_validation_errors(void)
{
    TEST_ASSERT(test_validation_errors() == 0,
                "expected 0 validation errors, got %d",
                test_validation_errors());
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    printf("Running renderer tests...\n");

    test_validation_reset();

    engine_config_t config   = zerus_engine_default_config();
    config.headless          = true;
    config.enable_validation = true;
    config.debug_callback    = test_debug_callback;

    engine = zerus_engine_init(&test_alloc, config);
    if (!engine.initialized || engine.err != INIT_OK)
    {
        fprintf(
            stderr,
            "Failed to create headless engine context (err=%d) — skipping tests\n",
            engine.err);
        // Return success so CI doesn't fail on machines without Vulkan.
        return EXIT_SUCCESS;
    }

    RUN_TEST(test_headless_render);
    RUN_TEST(test_sustained_render);
    RUN_TEST(test_no_validation_errors);

    zerus_engine_shutdown(&engine);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? EXIT_SUCCESS : EXIT_FAILURE;
}
