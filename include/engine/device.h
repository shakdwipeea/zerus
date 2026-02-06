//
// Created by akash on 5/7/25.
//

#ifndef DEVICE_H
#define DEVICE_H
#include "prelude.h"


#include <vulkan/vulkan_core.h>

typedef enum
{
    DEVICE_OK,
    QUEUE_FAMILY_NOT_FOUND,
    DEVICE_NOT_FOUND,
    GRAPHICS_QUEUE_NOT_FOUND,
    DEVICE_CREATION_FAILED,

} device_error_t;

list_t* enumerate_devices(allocator* alloc, VkInstance instance)
{
    // create device
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, nullptr);

    list_t* physical_devices = make_list(alloc, device_count);
    if (!physical_devices)
    {
        return nullptr;
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
    // Add other flags as needed...

    printf(")\n");
}

typedef struct
{
    device_error_t   error;
    VkPhysicalDevice physical_device;
    VkDevice         device;
    VkQueue          graphics_queue;
    VkQueue          compute_queue;
    uint32_t         queue_index;
} device_info_t;

device_info_t pick_device(allocator* alloc, VkInstance instance)
{
    device_info_t device_info = { 0 };

    list_t* physical_devices = enumerate_devices(alloc, instance);

    VkPhysicalDevice choosen_device = nullptr;

    for (uint32_t i = 0; i < physical_devices->len; i++)
    {
        VkPhysicalDevice device = physical_devices->data[i];

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        // picking the first discrete gpu for simplicity
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            choosen_device = device;
            printf("found discrete GPU device\n");
            break;
        }
    }

    // Fall back to any available device (e.g., integrated GPU, lavapipe)
    if (choosen_device == nullptr && physical_devices->len > 0)
    {
        choosen_device = physical_devices->data[0];
        printf("no discrete GPU found, using first available device\n");
    }

    if (choosen_device == nullptr)
    {
        device_info.error = DEVICE_NOT_FOUND;
        return device_info;
    }

    // we found a device
    device_info.physical_device = choosen_device;

    // now lets look for a queue where we will submit the commands
    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(
        choosen_device, &queue_family_count, nullptr);
    if (queue_family_count == 0)
    {
        device_info.error = QUEUE_FAMILY_NOT_FOUND;
        return device_info;
    }

    VkQueueFamilyProperties* families
        = (VkQueueFamilyProperties*) alloc->malloc(
            queue_family_count * sizeof(VkQueueFamilyProperties), alloc->ctx);

    vkGetPhysicalDeviceQueueFamilyProperties(
        choosen_device, &queue_family_count, families);


    printf("queue family count: %d \n", queue_family_count);

    uint32_t graphics_queue_index = -1, compute_queue_index = -1;
    for (uint32_t i = 0; i < queue_family_count; i++)
    {
        VkQueueFamilyProperties queue_family = families[i];

        if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            graphics_queue_index = i;
        }
        else if (queue_family.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            // check if there is a dedicated compute queue
            compute_queue_index = i;
        }

        printf("index %d", i);
        _print_queue_flags(queue_family.queueFlags);
    }

    if (graphics_queue_index == -1u)
    {
        device_info.error = GRAPHICS_QUEUE_NOT_FOUND;
        return device_info;
    }

    uint32_t                queue_count            = 1;
    float                   default_queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_create_info[2] = { (VkDeviceQueueCreateInfo) {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueCount       = 1,
        .queueFamilyIndex = graphics_queue_index,
        .pQueuePriorities = &default_queue_priority } };

    // if we have a dedicated compute queue, we create a logical one for that
    if (compute_queue_index != -1u)
    {
        queue_create_info[1] = (VkDeviceQueueCreateInfo) {
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueCount       = 1,
            .queueFamilyIndex = compute_queue_index,
            .pQueuePriorities = &default_queue_priority
        };

        queue_count++;
    }

    const char* device_extensions[4]
        = { VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_SPIRV_1_4_EXTENSION_NAME,
            VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
            VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME };

    VkDeviceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = nullptr,
        .queueCreateInfoCount    = queue_count,
        .pQueueCreateInfos       = queue_create_info,
        .enabledExtensionCount   = 4,
        .ppEnabledExtensionNames = device_extensions,
    };

    VkResult res = vkCreateDevice(
        choosen_device, &create_info, nullptr, &device_info.device);
    if (res != VK_SUCCESS)
    {
        device_info.error = DEVICE_CREATION_FAILED;
        return device_info;
    }


    device_info.queue_index = graphics_queue_index;

    vkGetDeviceQueue(device_info.device,
                     graphics_queue_index,
                     0,
                     &device_info.graphics_queue);

    if (compute_queue_index == -1u)
    {
        vkGetDeviceQueue(device_info.device,
                         compute_queue_index,
                         0,
                         &device_info.compute_queue);
    }
    else
    {
        // if not compute queue, we share the queue
        device_info.compute_queue = device_info.graphics_queue;
    }

    alloc->free(families, alloc->ctx);
    alloc->free(physical_devices, alloc->ctx);

    return device_info;
}
#endif  // DEVICE_H
