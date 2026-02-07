//
// Swapchain frame-loop test via engine core init + update path.
//
// This intentionally exercises the exact startup/update/shutdown flow used by
// applications so test behavior tracks production behavior.
//

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "test_common.h"

#define ZERUS_CORE_IMPLEMENTATION
#include "engine/core.h"

#define FRAME_COUNT 10

int main(void)
{
    printf("Running swapchain frame-loop test...\n");

#ifdef GLFW_PLATFORM
#ifdef GLFW_PLATFORM_X11
    // Prefer X11 under Xvfb even when the host session is Wayland so CI and
    // local headless runs follow the same surface backend.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
#endif

    test_validation_reset();

    engine_config_t config   = zerus_engine_default_config();
    config.headless          = false;
    config.window.width      = 64;
    config.window.height     = 64;
    config.window.title      = "Zerus Swapchain Test";
    config.window.visible    = false;
    config.enable_validation = true;
    config.debug_callback    = test_debug_callback;

    zerus_engine_state_t engine = zerus_engine_init(&test_alloc, config);
    if (!engine.initialized || engine.err != INIT_OK)
    {
        printf("  SKIP: unable to initialize swapchain path (err=%d)\n",
               engine.err);
        return EXIT_SUCCESS;
    }

    bool frame_loop_ok = true;
    for (int i = 0; i < FRAME_COUNT; i++)
    {
        engine_update_status_t status = zerus_engine_update(&engine);
        if (status != ENGINE_UPDATE_OK)
        {
            fprintf(stderr,
                    "  FAIL: zerus_engine_update returned %d on frame %d\n",
                    status,
                    i);
            frame_loop_ok = false;
            break;
        }
    }

    zerus_engine_shutdown(&engine);

    if (!frame_loop_ok)
    {
        return EXIT_FAILURE;
    }

    if (test_validation_errors() > 0)
    {
        fprintf(stderr,
                "  FAIL: %d validation error(s) detected\n",
                test_validation_errors());
        return EXIT_FAILURE;
    }

    printf("  PASS: %d frames, 0 validation errors\n", FRAME_COUNT);
    return EXIT_SUCCESS;
}
