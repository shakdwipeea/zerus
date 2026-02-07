#ifndef DEVICE_H
#define DEVICE_H

#include <stdio.h>

#include <vulkan/vulkan_core.h>

#include "prelude.h"

typedef enum
{
    DEVICE_OK,
    QUEUE_FAMILY_NOT_FOUND,
    DEVICE_NOT_FOUND,
    GRAPHICS_QUEUE_NOT_FOUND,
    DEVICE_CREATION_FAILED,
} device_error_t;

typedef struct
{
    device_error_t   error;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          graphics_queue;
    VkQueue          compute_queue;
    uint32_t         queue_index;
} device_info_t;

// Extension policy is internal to the engine rather than a public API surface.
// Headless mode intentionally skips swapchain because it should remain usable
// on systems where presentation support is unavailable (CI, capture tools,
// etc.).
static const char* const DEVICE_EXTENSIONS_WINDOWED[]
    = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME };

static const char* const DEVICE_EXTENSIONS_HEADLESS[]
    = { VK_KHR_SPIRV_1_4_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME };

list_t* enumerate_devices(allocator* alloc, VkInstance instance)
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);
    if (device_count == 0)
    {
        return NULL;
    }

    list_t* physical_devices = make_list(alloc, device_count);
    if (!physical_devices)
    {
        return NULL;
    }

    physical_devices->len = device_count;
    vkEnumeratePhysicalDevices(
        instance, &device_count, (VkPhysicalDevice*) &physical_devices->data);
    return physical_devices;
}

void _print_queue_flags(VkQueueFlagBits flags)
{
    printf("Queue flags: 0x%08x (", flags);

    bool first = true;
    if (flags & VK_QUEUE_GRAPHICS_BIT)
    {
        printf("%sGRAPHICS", first ? "" : " | ");
        first = false;
    }
    if (flags & VK_QUEUE_COMPUTE_BIT)
    {
        printf("%sCOMPUTE", first ? "" : " | ");
        first = false;
    }
    if (flags & VK_QUEUE_TRANSFER_BIT)
    {
        printf("%sTRANSFER", first ? "" : " | ");
        first = false;
    }
    if (flags & VK_QUEUE_SPARSE_BINDING_BIT)
    {
        printf("%sSPARSE_BINDING", first ? "" : " | ");
        first = false;
    }
    if (flags & VK_QUEUE_PROTECTED_BIT)
    {
        printf("%sPROTECTED", first ? "" : " | ");
        first = false;
    }

    printf(")\n");
}

device_info_t pick_device(allocator* alloc, VkInstance instance, bool headless)
{
    device_info_t            device_info      = { 0 };
    list_t*                  physical_devices = NULL;
    VkQueueFamilyProperties* families         = NULL;
    VkPhysicalDevice         chosen_device    = VK_NULL_HANDLE;

    physical_devices = enumerate_devices(alloc, instance);
    if (!physical_devices)
    {
        device_info.error = DEVICE_NOT_FOUND;
        return device_info;
    }

    for (uint32_t i = 0; i < physical_devices->len; i++)
    {
        VkPhysicalDevice device = physical_devices->data[i];

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        // Prefer discrete when available to match app behavior, but keep a
        // fallback for CI/software ICD environments.
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            chosen_device = device;
            printf("found discrete GPU device\n");
            break;
        }
    }

    if (chosen_device == VK_NULL_HANDLE && physical_devices->len > 0)
    {
        chosen_device = physical_devices->data[0];
        printf("no discrete GPU found, using first available device\n");
    }

    if (chosen_device == VK_NULL_HANDLE)
    {
        device_info.error = DEVICE_NOT_FOUND;
        goto cleanup;
    }

    device_info.physical_device = chosen_device;

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(
        chosen_device, &queue_family_count, NULL);
    if (queue_family_count == 0)
    {
        device_info.error = QUEUE_FAMILY_NOT_FOUND;
        goto cleanup;
    }

    families = (VkQueueFamilyProperties*) alloc->malloc(
        queue_family_count * sizeof(VkQueueFamilyProperties), alloc->ctx);
    if (!families)
    {
        device_info.error = DEVICE_CREATION_FAILED;
        goto cleanup;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(
        chosen_device, &queue_family_count, families);

    printf("queue family count: %u\n", queue_family_count);

    uint32_t graphics_queue_index          = UINT32_MAX;
    uint32_t dedicated_compute_queue_index = UINT32_MAX;
    for (uint32_t i = 0; i < queue_family_count; i++)
    {
        VkQueueFamilyProperties queue_family = families[i];

        if (graphics_queue_index == UINT32_MAX
            && (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            graphics_queue_index = i;
        }

        if (dedicated_compute_queue_index == UINT32_MAX
            && (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT)
            && !(queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT))
        {
            dedicated_compute_queue_index = i;
        }

        printf("index %u ", i);
        _print_queue_flags(queue_family.queueFlags);
    }

    if (graphics_queue_index == UINT32_MAX)
    {
        device_info.error = GRAPHICS_QUEUE_NOT_FOUND;
        goto cleanup;
    }

    float                   default_queue_priority = 1.0f;
    uint32_t                queue_count            = 1;
    VkDeviceQueueCreateInfo queue_create_info[2]   = {
        {
              .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
              .queueCount       = 1,
              .queueFamilyIndex = graphics_queue_index,
              .pQueuePriorities = &default_queue_priority,
        },
    };

    if (dedicated_compute_queue_index != UINT32_MAX)
    {
        queue_create_info[1] = (VkDeviceQueueCreateInfo) {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueCount       = 1,
            .queueFamilyIndex = dedicated_compute_queue_index,
            .pQueuePriorities = &default_queue_priority,
        };
        queue_count++;
    }

    const char* const* required_extensions
        = headless ? DEVICE_EXTENSIONS_HEADLESS : DEVICE_EXTENSIONS_WINDOWED;
    uint32_t required_extension_count
        = headless ? (uint32_t) (sizeof(DEVICE_EXTENSIONS_HEADLESS)
                                 / sizeof(DEVICE_EXTENSIONS_HEADLESS[0]))
                   : (uint32_t) (sizeof(DEVICE_EXTENSIONS_WINDOWED)
                                 / sizeof(DEVICE_EXTENSIONS_WINDOWED[0]));

    VkDeviceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = NULL,
        .queueCreateInfoCount    = queue_count,
        .pQueueCreateInfos       = queue_create_info,
        .enabledExtensionCount   = required_extension_count,
        .ppEnabledExtensionNames = required_extensions,
    };

    VkResult res = vkCreateDevice(
        chosen_device, &create_info, NULL, &device_info.device);
    if (res != VK_SUCCESS)
    {
        device_info.error = DEVICE_CREATION_FAILED;
        goto cleanup;
    }

    device_info.queue_index = graphics_queue_index;
    vkGetDeviceQueue(device_info.device,
                     graphics_queue_index,
                     0,
                     &device_info.graphics_queue);

    if (dedicated_compute_queue_index != UINT32_MAX)
    {
        vkGetDeviceQueue(device_info.device,
                         dedicated_compute_queue_index,
                         0,
                         &device_info.compute_queue);
    }
    else
    {
        // Shared queue keeps compute paths working even on devices with only a
        // single universal queue family.
        device_info.compute_queue = device_info.graphics_queue;
    }

    device_info.error = DEVICE_OK;

cleanup:
    if (families)
    {
        alloc->free(families, alloc->ctx);
    }
    if (physical_devices)
    {
        alloc->free(physical_devices, alloc->ctx);
    }

    return device_info;
}

#endif  // DEVICE_H
