//
// Swapchain frame-loop test via GLFW + X11 surface (Xvfb-friendly)
//
// Exercises the real engine frame loop (begin_frame -> record_frame ->
// end_frame) with a swapchain, semaphores, and presentation.
//
// This test is intended to run under Xvfb in CI/local headless runs so it can
// validate acquire/present synchronization behavior without a visible window.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/prelude.h"
#include "engine/device.h"
#include "engine/surface.h"
#include "engine/frame.h"
#include "engine/renderer.h"

// ---------------------------------------------------------------------------
// Allocator
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
// Validation error tracking
// ---------------------------------------------------------------------------

static int validation_error_count = 0;

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
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
// Helpers
// ---------------------------------------------------------------------------

#define FRAME_COUNT 10

static bool check_validation_layer(void)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, NULL);

    VkLayerProperties* layers = malloc(count * sizeof(VkLayerProperties));
    if (!layers)
    {
        return false;
    }

    vkEnumerateInstanceLayerProperties(&count, layers);

    bool found = false;
    for (uint32_t i = 0; i < count; i++)
    {
        if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            found = true;
            break;
        }
    }

    free(layers);
    return found;
}

static VkDebugUtilsMessengerEXT create_debug_messenger(VkInstance instance)
{
    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;

    VkDebugUtilsMessengerCreateInfoEXT dbg_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    PFN_vkCreateDebugUtilsMessengerEXT create_fn
        = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
    if (create_fn)
    {
        create_fn(instance, &dbg_info, NULL, &debug_messenger);
    }

    return debug_messenger;
}

static void destroy_debug_messenger(VkInstance               instance,
                                    VkDebugUtilsMessengerEXT debug_messenger)
{
    if (debug_messenger == VK_NULL_HANDLE)
    {
        return;
    }

    PFN_vkDestroyDebugUtilsMessengerEXT destroy_fn
        = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy_fn)
    {
        destroy_fn(instance, debug_messenger, NULL);
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    printf("Running swapchain frame-loop test (GLFW + X11 surface)...\n");

    int         exit_code   = EXIT_FAILURE;
    bool        should_skip = false;
    const char* skip_reason = NULL;

    bool                     glfw_initialized = false;
    GLFWwindow*              window           = NULL;
    VkInstance               instance         = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger  = VK_NULL_HANDLE;
    VkSurfaceKHR             surface          = VK_NULL_HANDLE;
    VkDevice                 device           = VK_NULL_HANDLE;

    surface_info_t surf_info           = { 0 };
    bool           swapchain_created   = false;
    frame_state_t  frame               = { 0 };
    bool           frame_state_created = false;

#ifdef GLFW_PLATFORM
#ifdef GLFW_PLATFORM_X11
    // When available (GLFW 3.4+), force X11 so this works under Xvfb even when
    // the host desktop session is Wayland.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
#endif

    if (!glfwInit())
    {
        should_skip = true;
        skip_reason = "glfwInit failed";
        goto cleanup;
    }
    glfw_initialized = true;

    if (!glfwVulkanSupported())
    {
        should_skip = true;
        skip_reason = "GLFW reports Vulkan unavailable";
        goto cleanup;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(64, 64, "Zerus Swapchain Test", NULL, NULL);
    if (!window)
    {
        should_skip = true;
        skip_reason = "glfwCreateWindow failed (no X11 display?)";
        goto cleanup;
    }

    uint32_t     glfw_ext_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    if (!glfw_exts || glfw_ext_count == 0)
    {
        should_skip = true;
        skip_reason = "GLFW returned no required Vulkan instance extensions";
        goto cleanup;
    }

    bool has_validation = check_validation_layer();

    const char* instance_extensions[32] = { 0 };
    if (glfw_ext_count + (has_validation ? 1u : 0u) > 32u)
    {
        fprintf(stderr, "  FAIL: too many instance extensions requested\n");
        goto cleanup;
    }

    for (uint32_t i = 0; i < glfw_ext_count; i++)
    {
        instance_extensions[i] = glfw_exts[i];
    }

    uint32_t enabled_extension_count = glfw_ext_count;
    if (has_validation)
    {
        instance_extensions[enabled_extension_count]
            = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        enabled_extension_count++;
    }

    VkApplicationInfo app_info = {
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Zerus Swapchain Test",
        .apiVersion       = VK_API_VERSION_1_1,
    };

    const char*          layer_names[] = { "VK_LAYER_KHRONOS_validation" };
    VkInstanceCreateInfo instance_ci   = {
          .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
          .pApplicationInfo        = &app_info,
          .enabledExtensionCount   = enabled_extension_count,
          .ppEnabledExtensionNames = instance_extensions,
          .enabledLayerCount       = has_validation ? 1u : 0u,
          .ppEnabledLayerNames     = has_validation ? layer_names : NULL,
    };

    VkResult res = vkCreateInstance(&instance_ci, NULL, &instance);
    if (res != VK_SUCCESS)
    {
        should_skip = true;
        skip_reason = "vkCreateInstance failed";
        goto cleanup;
    }

    if (has_validation)
    {
        debug_messenger = create_debug_messenger(instance);
    }

    res = glfwCreateWindowSurface(instance, window, NULL, &surface);
    if (res != VK_SUCCESS)
    {
        should_skip = true;
        skip_reason = "glfwCreateWindowSurface failed";
        goto cleanup;
    }

    // -- Find a physical device that supports this surface --
    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(instance, &phys_count, NULL);
    if (phys_count == 0)
    {
        should_skip = true;
        skip_reason = "no Vulkan physical devices found";
        goto cleanup;
    }

    VkPhysicalDevice phys_devices[16];
    if (phys_count > 16)
    {
        phys_count = 16;
    }
    vkEnumeratePhysicalDevices(instance, &phys_count, phys_devices);

    VkPhysicalDevice chosen_phys   = VK_NULL_HANDLE;
    uint32_t         chosen_qf_idx = UINT32_MAX;

    for (uint32_t d = 0; d < phys_count; d++)
    {
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(
            phys_devices[d], &qf_count, NULL);
        if (qf_count == 0)
        {
            continue;
        }

        VkQueueFamilyProperties* qf_props
            = malloc(qf_count * sizeof(VkQueueFamilyProperties));
        if (!qf_props)
        {
            fprintf(stderr,
                    "  FAIL: out of memory allocating queue families\n");
            goto cleanup;
        }

        vkGetPhysicalDeviceQueueFamilyProperties(
            phys_devices[d], &qf_count, qf_props);

        for (uint32_t q = 0; q < qf_count; q++)
        {
            if (!(qf_props[q].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                continue;
            }

            VkBool32 present_ok = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(
                phys_devices[d], q, surface, &present_ok);
            if (present_ok)
            {
                chosen_phys   = phys_devices[d];
                chosen_qf_idx = q;
                break;
            }
        }

        free(qf_props);

        if (chosen_phys != VK_NULL_HANDLE)
        {
            break;
        }
    }

    if (chosen_phys == VK_NULL_HANDLE)
    {
        should_skip = true;
        skip_reason = "no device supports graphics+present for this surface";
        goto cleanup;
    }

    VkPhysicalDeviceProperties chosen_props;
    vkGetPhysicalDeviceProperties(chosen_phys, &chosen_props);
    printf("  Using device: %s\n", chosen_props.deviceName);

    // -- Create logical device with VK_KHR_swapchain --
    float                   priority = 1.0f;
    VkDeviceQueueCreateInfo queue_ci = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = chosen_qf_idx,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };
    const char*        dev_exts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo device_ci  = {
         .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
         .queueCreateInfoCount    = 1,
         .pQueueCreateInfos       = &queue_ci,
         .enabledExtensionCount   = 1,
         .ppEnabledExtensionNames = dev_exts,
    };

    res = vkCreateDevice(chosen_phys, &device_ci, NULL, &device);
    if (res != VK_SUCCESS)
    {
        should_skip = true;
        skip_reason = "vkCreateDevice failed for present-capable queue";
        goto cleanup;
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, chosen_qf_idx, 0, &queue);

    // Build device_info_t for engine functions
    device_info_t dev_info = {
        .physical_device = chosen_phys,
        .device          = device,
        .graphics_queue  = queue,
        .queue_index     = chosen_qf_idx,
    };

    // -- Create swapchain using engine code --
    surf_info.surface = surface;

    surface_status_t sc_status = create_swapchain_for_surface(
        &test_alloc, dev_info, &surf_info, 64, 64);
    if (sc_status != SURFACE_OK)
    {
        should_skip = true;
        skip_reason = "create_swapchain_for_surface failed";
        goto cleanup;
    }
    swapchain_created = true;

    // -- Create frame state and renderer using engine code --
    frame = create_frame_state(dev_info);
    if (frame.status != FRAME_OK)
    {
        fprintf(stderr, "  FAIL: create_frame_state failed\n");
        goto cleanup;
    }
    frame_state_created = true;

    renderer_t renderer = create_renderer();

    // -- Run the real engine frame loop --
    printf("  Running %d frames with begin_frame/record_frame/end_frame...\n",
           FRAME_COUNT);

    bool frame_loop_ok = true;
    for (int i = 0; i < FRAME_COUNT; i++)
    {
        glfwPollEvents();

        frame_begin_result_t fb
            = begin_frame(&frame, &dev_info, surf_info.swapchain);
        if (fb.status == FRAME_SWAPCHAIN_OUT_OF_DATE)
        {
            continue;
        }
        if (fb.status != FRAME_OK)
        {
            fprintf(stderr,
                    "  FAIL: begin_frame returned %d on frame %d\n",
                    fb.status,
                    i);
            frame_loop_ok = false;
            break;
        }

        record_frame(&renderer,
                     fb.cmd,
                     surf_info.images[fb.image_index],
                     surf_info.image_format,
                     surf_info.extent);

        frame_status_t fs = end_frame(&frame,
                                      &dev_info,
                                      surf_info.swapchain,
                                      fb.image_index,
                                      surf_info.images[fb.image_index]);
        if (fs == FRAME_SWAPCHAIN_OUT_OF_DATE)
        {
            continue;
        }
        if (fs != FRAME_OK)
        {
            fprintf(
                stderr, "  FAIL: end_frame returned %d on frame %d\n", fs, i);
            frame_loop_ok = false;
            break;
        }
    }

    vkDeviceWaitIdle(device);

    destroy_renderer(&renderer);

    if (!frame_loop_ok)
    {
        fprintf(stderr, "  FAIL: frame loop did not complete\n");
        goto cleanup;
    }

    if (validation_error_count > 0)
    {
        fprintf(stderr,
                "  FAIL: %d validation error(s) detected\n",
                validation_error_count);
        goto cleanup;
    }

    printf("  PASS: %d frames, 0 validation errors\n", FRAME_COUNT);
    exit_code = EXIT_SUCCESS;

cleanup:
    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
    }

    if (frame_state_created)
    {
        device_info_t dev_info = {
            .device = device,
        };
        destroy_frame_state(dev_info, &frame);
    }

    if (swapchain_created)
    {
        device_info_t dev_info = {
            .device = device,
        };
        destroy_swapchain(&test_alloc, dev_info, &surf_info);
    }

    if (device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(device, NULL);
    }

    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, NULL);
    }

    if (instance != VK_NULL_HANDLE)
    {
        destroy_debug_messenger(instance, debug_messenger);
        vkDestroyInstance(instance, NULL);
    }

    if (window)
    {
        glfwDestroyWindow(window);
    }

    if (glfw_initialized)
    {
        glfwTerminate();
    }

    if (should_skip)
    {
        printf("  SKIP: %s\n",
               skip_reason ? skip_reason : "environment limitation");
        return EXIT_SUCCESS;
    }

    return exit_code;
}
