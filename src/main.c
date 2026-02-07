#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ZERUS_CORE_IMPLEMENTATION
#include "engine/core.h"


#include <cglm/cglm.h>


// Standard library allocator wrappers
static void* std_malloc(ptrdiff_t size, void* ctx)
{
    (void) ctx;
    return malloc(size);
}

static void std_free(void* ptr, void* ctx)
{
    (void) ctx;
    free(ptr);
}

int main(int argc, char* argv[])
{
    (void) argc;  // Suppress unused parameter warning
    (void) argv;

    printf("Zerus Game Engine v1.0.0\n");
    printf("Initializing engine...\n");
    fflush(stdout);

    allocator std_alloc = { std_malloc, std_free, NULL };

    engine_config_t config = zerus_engine_default_config();
    config.headless        = false;

    zerus_engine_state_t engine = zerus_engine_init(&std_alloc, config);

    if (!engine.initialized || engine.err != INIT_OK)
    {
        fprintf(stderr, "Failed to initialize engine (err=%d)\n", engine.err);
        return EXIT_FAILURE;
    }

    printf("Engine initialized successfully\n");
    fflush(stdout);

    // Update engine systems
    zerus_engine_start(&engine);

    printf("Engine shutdown complete\n");
    return EXIT_SUCCESS;
}
