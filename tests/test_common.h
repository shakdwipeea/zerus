#ifndef ZERUS_TEST_COMMON_H
#define ZERUS_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "engine/prelude.h"

// ---------------------------------------------------------------------------
// Shared test harness macros
// ---------------------------------------------------------------------------

#define TEST_ASSERT(cond, ...)                                                 \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            test_current_failed = true;                                        \
            fprintf(stderr, "  FAIL %s:%d: ", __FILE__, __LINE__);             \
            fprintf(stderr, __VA_ARGS__);                                      \
            fprintf(stderr, "\n");                                             \
            return;                                                            \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do                                                                         \
    {                                                                          \
        tests_run++;                                                           \
        test_current_failed = false;                                           \
        printf("  %-50s", #fn);                                                \
        fn();                                                                  \
        if (test_current_failed)                                               \
        {                                                                      \
            printf("FAIL\n");                                                  \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            tests_passed++;                                                    \
            printf("PASS\n");                                                  \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// Shared allocator
// ---------------------------------------------------------------------------

static void* test_malloc_fn(ptrdiff_t size, void* ctx)
{
    (void) ctx;
    return malloc((size_t) size);
}

static void test_free_fn(void* ptr, void* ctx)
{
    (void) ctx;
    free(ptr);
}

static allocator test_alloc = {
    .malloc = test_malloc_fn,
    .free   = test_free_fn,
    .ctx    = NULL,
};

// ---------------------------------------------------------------------------
// Shared validation tracking callback
// ---------------------------------------------------------------------------

static int test_validation_error_count = 0;

static void test_validation_reset(void) { test_validation_error_count = 0; }

static int test_validation_errors(void) { return test_validation_error_count; }

static VKAPI_ATTR VkBool32 VKAPI_CALL
test_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
                    VkDebugUtilsMessageTypeFlagsEXT             message_type,
                    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
                    void*                                       user_data)
{
    (void) message_type;
    (void) user_data;

    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        test_validation_error_count++;
        fprintf(stderr, "VALIDATION ERROR: %s\n", callback_data->pMessage);
    }
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        fprintf(stderr, "VALIDATION WARNING: %s\n", callback_data->pMessage);
    }

    return VK_FALSE;
}

#endif  // ZERUS_TEST_COMMON_H
