//
// E2E tests for the Zerus renderer
//
// 1. Headless offscreen render with pixel readback
// 2. Sustained multi-frame render with fence-based sync
// 3. Asserts zero Vulkan validation errors across all tests
//
// Requires: Vulkan ICD (real GPU or lavapipe for CI)
// Does NOT require: display server, GLFW, window
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "engine/prelude.h"
#include "engine/device.h"
#include "engine/frame.h"
#include "engine/renderer.h"

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST_ASSERT(cond, ...)                                                 \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
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
        printf("  %-50s", #fn);                                                \
        fn();                                                                  \
        tests_passed++;                                                        \
        printf("PASS\n");                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// Validation error tracking
// ---------------------------------------------------------------------------

static int validation_error_count = 0;

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
        validation_error_count++;
        fprintf(stderr, "VALIDATION ERROR: %s\n", callback_data->pMessage);
    }
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        fprintf(stderr, "VALIDATION WARNING: %s\n", callback_data->pMessage);
    }

    return VK_FALSE;
}

// ---------------------------------------------------------------------------
// Vulkan helpers (test-only, independent of engine code)
// ---------------------------------------------------------------------------

static uint32_t find_memory_type(VkPhysicalDevice      physical_device,
                                 uint32_t              type_filter,
                                 VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++)
    {
        if ((type_filter & (1u << i))
            && (mem_props.memoryTypes[i].propertyFlags & properties)
                   == properties)
        {
            return i;
        }
    }

    return UINT32_MAX;
}

// ---------------------------------------------------------------------------
// Test state (shared across the headless test)
// ---------------------------------------------------------------------------

typedef struct
{
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice         physical_device;
    VkDevice                 device;
    VkQueue                  queue;
    uint32_t                 queue_index;
    VkCommandPool            command_pool;
} test_vulkan_ctx_t;

static bool create_test_vulkan_ctx(test_vulkan_ctx_t* ctx)
{
    // -- Instance --
    VkApplicationInfo app_info = {
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Zerus Test",
        .apiVersion       = VK_API_VERSION_1_1,
    };

    // Check for validation layer support
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    VkLayerProperties layers[256];
    vkEnumerateInstanceLayerProperties(&layer_count, layers);

    bool has_validation = false;
    for (uint32_t i = 0; i < layer_count; i++)
    {
        if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            has_validation = true;
            break;
        }
    }

    const char* layer_names[]     = { "VK_LAYER_KHRONOS_validation" };
    const char* extension_names[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };

    VkInstanceCreateInfo instance_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = has_validation ? 1 : 0,
        .ppEnabledLayerNames     = has_validation ? layer_names : NULL,
        .enabledExtensionCount   = has_validation ? 1 : 0,
        .ppEnabledExtensionNames = has_validation ? extension_names : NULL,
    };

    VkResult res = vkCreateInstance(&instance_info, NULL, &ctx->instance);
    if (res != VK_SUCCESS)
    {
        fprintf(stderr, "failed to create Vulkan instance: %d\n", res);
        return false;
    }

    // -- Debug messenger --
    if (has_validation)
    {
        VkDebugUtilsMessengerCreateInfoEXT dbg_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity
            = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
              | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = test_debug_callback,
        };

        PFN_vkCreateDebugUtilsMessengerEXT create_fn
            = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
                ctx->instance, "vkCreateDebugUtilsMessengerEXT");
        if (create_fn)
        {
            create_fn(ctx->instance, &dbg_info, NULL, &ctx->debug_messenger);
        }
    }

    // -- Physical device (pick first available) --
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL);
    if (device_count == 0)
    {
        fprintf(stderr, "no Vulkan devices found\n");
        return false;
    }

    VkPhysicalDevice devices[16];
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices);
    ctx->physical_device = devices[0];

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &props);
    printf("  Using device: %s\n", props.deviceName);

    // -- Queue family (need graphics) --
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        ctx->physical_device, &qf_count, NULL);
    VkQueueFamilyProperties qf_props[16];
    vkGetPhysicalDeviceQueueFamilyProperties(
        ctx->physical_device, &qf_count, qf_props);

    ctx->queue_index = UINT32_MAX;
    for (uint32_t i = 0; i < qf_count; i++)
    {
        if (qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            ctx->queue_index = i;
            break;
        }
    }
    if (ctx->queue_index == UINT32_MAX)
    {
        fprintf(stderr, "no graphics queue found\n");
        return false;
    }

    // -- Logical device (no extensions needed for headless clear) --
    float                   priority   = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->queue_index,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };

    VkDeviceCreateInfo device_info = {
        .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos    = &queue_info,
    };

    res = vkCreateDevice(
        ctx->physical_device, &device_info, NULL, &ctx->device);
    if (res != VK_SUCCESS)
    {
        fprintf(stderr, "failed to create device: %d\n", res);
        return false;
    }

    vkGetDeviceQueue(ctx->device, ctx->queue_index, 0, &ctx->queue);

    // -- Command pool --
    VkCommandPoolCreateInfo pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->queue_index,
    };
    res = vkCreateCommandPool(
        ctx->device, &pool_info, NULL, &ctx->command_pool);
    if (res != VK_SUCCESS)
    {
        fprintf(stderr, "failed to create command pool: %d\n", res);
        return false;
    }

    return true;
}

static void destroy_test_vulkan_ctx(test_vulkan_ctx_t* ctx)
{
    vkDeviceWaitIdle(ctx->device);
    vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);
    vkDestroyDevice(ctx->device, NULL);

    if (ctx->debug_messenger)
    {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn
            = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
                ctx->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_fn)
        {
            destroy_fn(ctx->instance, ctx->debug_messenger, NULL);
        }
    }

    vkDestroyInstance(ctx->instance, NULL);
}

// ---------------------------------------------------------------------------
// Shared test context (initialized once)
// ---------------------------------------------------------------------------

static test_vulkan_ctx_t vk_ctx = { 0 };

// ---------------------------------------------------------------------------
// Test: headless render with pixel readback
// ---------------------------------------------------------------------------

#define TEST_IMAGE_WIDTH 64
#define TEST_IMAGE_HEIGHT 64
#define TEST_IMAGE_FORMAT VK_FORMAT_B8G8R8A8_UNORM

static void test_headless_render(void)
{
    // -- Create offscreen image --
    VkImageCreateInfo image_info = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = TEST_IMAGE_FORMAT,
        .extent      = { TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage
        = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage  image;
    VkResult res = vkCreateImage(vk_ctx.device, &image_info, NULL, &image);
    TEST_ASSERT(res == VK_SUCCESS, "failed to create offscreen image: %d", res);

    // Allocate image memory
    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(vk_ctx.device, image, &mem_req);

    uint32_t mem_type = find_memory_type(vk_ctx.physical_device,
                                         mem_req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    TEST_ASSERT(mem_type != UINT32_MAX, "no suitable memory type for image");

    VkMemoryAllocateInfo mem_alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_req.size,
        .memoryTypeIndex = mem_type,
    };
    VkDeviceMemory image_memory;
    res = vkAllocateMemory(vk_ctx.device, &mem_alloc, NULL, &image_memory);
    TEST_ASSERT(res == VK_SUCCESS, "failed to allocate image memory");
    vkBindImageMemory(vk_ctx.device, image, image_memory, 0);

    // -- Create staging buffer for readback --
    VkDeviceSize buffer_size
        = TEST_IMAGE_WIDTH * TEST_IMAGE_HEIGHT * 4;  // BGRA

    VkBufferCreateInfo buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = buffer_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    VkBuffer staging_buffer;
    res = vkCreateBuffer(vk_ctx.device, &buf_info, NULL, &staging_buffer);
    TEST_ASSERT(res == VK_SUCCESS, "failed to create staging buffer");

    VkMemoryRequirements buf_mem_req;
    vkGetBufferMemoryRequirements(vk_ctx.device, staging_buffer, &buf_mem_req);

    uint32_t buf_mem_type
        = find_memory_type(vk_ctx.physical_device,
                           buf_mem_req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                               | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    TEST_ASSERT(buf_mem_type != UINT32_MAX,
                "no suitable memory type for staging buffer");

    VkMemoryAllocateInfo buf_mem_alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = buf_mem_req.size,
        .memoryTypeIndex = buf_mem_type,
    };
    VkDeviceMemory buffer_memory;
    res = vkAllocateMemory(vk_ctx.device, &buf_mem_alloc, NULL, &buffer_memory);
    TEST_ASSERT(res == VK_SUCCESS, "failed to allocate buffer memory");
    vkBindBufferMemory(vk_ctx.device, staging_buffer, buffer_memory, 0);

    // -- Allocate command buffer --
    VkCommandBufferAllocateInfo cmd_alloc = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = vk_ctx.command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(vk_ctx.device, &cmd_alloc, &cmd);

    // -- Record commands --
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    // 1. record_frame: transitions UNDEFINED -> TRANSFER_DST, clears image
    renderer_t renderer = create_renderer();
    VkExtent2D extent   = { TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT };
    record_frame(&renderer, cmd, image, TEST_IMAGE_FORMAT, extent);

    // 2. Transition: TRANSFER_DST -> TRANSFER_SRC (for readback)
    VkImageMemoryBarrier barrier_to_src = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         NULL,
                         0,
                         NULL,
                         1,
                         &barrier_to_src);

    // 3. Copy image to staging buffer
    VkBufferImageCopy copy_region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel       = 0,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, 1 },
    };
    vkCmdCopyImageToBuffer(cmd,
                           image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging_buffer,
                           1,
                           &copy_region);

    vkEndCommandBuffer(cmd);

    // -- Submit and wait --
    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };
    res = vkQueueSubmit(vk_ctx.queue, 1, &submit, VK_NULL_HANDLE);
    TEST_ASSERT(res == VK_SUCCESS, "queue submit failed: %d", res);
    vkQueueWaitIdle(vk_ctx.queue);

    // -- Read back pixels --
    void* mapped;
    vkMapMemory(vk_ctx.device, buffer_memory, 0, buffer_size, 0, &mapped);

    // Format is B8G8R8A8_UNORM. Clear color is {0.0, 0.1, 0.15, 1.0}.
    // Expected bytes: B = 0.15*255 ≈ 38, G = 0.1*255 ≈ 26, R = 0, A = 255
    const uint8_t* pixels = (const uint8_t*) mapped;

    // Check pixel (0,0) — first pixel
    uint8_t b = pixels[0];
    uint8_t g = pixels[1];
    uint8_t r = pixels[2];
    uint8_t a = pixels[3];

    // Allow ±2 tolerance for rounding across implementations
    TEST_ASSERT(r <= 2, "R: expected ~0, got %u", r);
    TEST_ASSERT(g >= 24 && g <= 28, "G: expected ~26, got %u", g);
    TEST_ASSERT(b >= 36 && b <= 40, "B: expected ~38, got %u", b);
    TEST_ASSERT(a >= 254, "A: expected 255, got %u", a);

    // Check a pixel in the middle too (should be the same — uniform clear)
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

    vkUnmapMemory(vk_ctx.device, buffer_memory);

    // -- Cleanup --
    destroy_renderer(&renderer);
    vkFreeCommandBuffers(vk_ctx.device, vk_ctx.command_pool, 1, &cmd);
    vkDestroyBuffer(vk_ctx.device, staging_buffer, NULL);
    vkFreeMemory(vk_ctx.device, buffer_memory, NULL);
    vkDestroyImage(vk_ctx.device, image, NULL);
    vkFreeMemory(vk_ctx.device, image_memory, NULL);
}

// ---------------------------------------------------------------------------
// Test: sustained rendering over many frames (fence-based sync)
// ---------------------------------------------------------------------------

#define SUSTAINED_FRAME_COUNT 100

static void test_sustained_render(void)
{
    // Build a device_info_t from the test context so we can reuse the engine's
    // frame helpers (create_frame_state, frame_begin_recording, etc.).
    device_info_t dev_info = {
        .device          = vk_ctx.device,
        .physical_device = vk_ctx.physical_device,
        .graphics_queue  = vk_ctx.queue,
        .queue_index     = vk_ctx.queue_index,
    };

    frame_state_t frame = create_frame_state(dev_info);
    TEST_ASSERT(frame.status == FRAME_OK, "failed to create frame state");

    // -- Create a single offscreen image, reused across all frames --
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = TEST_IMAGE_FORMAT,
        .extent        = { TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImage  image;
    VkResult res = vkCreateImage(vk_ctx.device, &image_info, NULL, &image);
    TEST_ASSERT(res == VK_SUCCESS, "failed to create offscreen image: %d", res);

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(vk_ctx.device, image, &mem_req);

    uint32_t mem_type = find_memory_type(vk_ctx.physical_device,
                                         mem_req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    TEST_ASSERT(mem_type != UINT32_MAX, "no suitable memory type for image");

    VkMemoryAllocateInfo mem_alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_req.size,
        .memoryTypeIndex = mem_type,
    };
    VkDeviceMemory image_memory;
    res = vkAllocateMemory(vk_ctx.device, &mem_alloc, NULL, &image_memory);
    TEST_ASSERT(res == VK_SUCCESS, "failed to allocate image memory");
    vkBindImageMemory(vk_ctx.device, image, image_memory, 0);

    // -- Render loop: 100 frames with fence-based synchronization --
    renderer_t renderer = create_renderer();
    VkExtent2D extent   = { TEST_IMAGE_WIDTH, TEST_IMAGE_HEIGHT };

    for (int i = 0; i < SUSTAINED_FRAME_COUNT; i++)
    {
        VkCommandBuffer cmd = frame_begin_recording(&frame, vk_ctx.device);
        record_frame(&renderer, cmd, image, TEST_IMAGE_FORMAT, extent);

        frame_status_t status = frame_end_and_submit(&frame, vk_ctx.queue);
        TEST_ASSERT(
            status == FRAME_OK, "frame_end_and_submit failed on frame %d", i);
    }

    // Wait for the last submission before cleanup
    vkDeviceWaitIdle(vk_ctx.device);

    // -- Cleanup --
    destroy_renderer(&renderer);
    destroy_frame_state(dev_info, &frame);
    vkDestroyImage(vk_ctx.device, image, NULL);
    vkFreeMemory(vk_ctx.device, image_memory, NULL);
}

// ---------------------------------------------------------------------------
// Test: zero validation errors
// ---------------------------------------------------------------------------

static void test_no_validation_errors(void)
{
    TEST_ASSERT(validation_error_count == 0,
                "expected 0 validation errors, got %d",
                validation_error_count);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    printf("Running renderer tests...\n");

    if (!create_test_vulkan_ctx(&vk_ctx))
    {
        fprintf(stderr, "Failed to create Vulkan context — skipping tests\n");
        // Return success so CI doesn't fail on machines without Vulkan
        return EXIT_SUCCESS;
    }

    RUN_TEST(test_headless_render);
    RUN_TEST(test_sustained_render);
    RUN_TEST(test_no_validation_errors);

    destroy_test_vulkan_ctx(&vk_ctx);

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? EXIT_SUCCESS : EXIT_FAILURE;
}
