//
// Zerus Game Engine - GPU Image Utilities
//
// Helpers for offscreen images and CPU readback paths.
// These are used by headless tests today and are designed to be reused for
// screenshots, captures, and GPU diagnostics.
//

#ifndef GPU_IMAGE_H
#define GPU_IMAGE_H

#include <stdbool.h>
#include <stdio.h>

#include <vulkan/vulkan_core.h>

#include "device.h"

typedef enum
{
    GPU_IMAGE_OK,
    GPU_IMAGE_MEMORY_TYPE_NOT_FOUND,
    GPU_IMAGE_CREATION_FAILED,
    GPU_IMAGE_MEMORY_ALLOC_FAILED,
    GPU_IMAGE_MEMORY_BIND_FAILED,
    GPU_IMAGE_BUFFER_CREATION_FAILED,
    GPU_IMAGE_BUFFER_MEMORY_ALLOC_FAILED,
    GPU_IMAGE_BUFFER_MEMORY_BIND_FAILED,
    GPU_IMAGE_MAP_FAILED,
    GPU_IMAGE_INVALID_ARGUMENT,
    GPU_IMAGE_UNSUPPORTED_LAYOUT_TRANSITION,
} gpu_image_status_t;

typedef struct
{
    gpu_image_status_t status;
    VkImage            image;
    VkDeviceMemory     memory;
    VkFormat           format;
    VkExtent2D         extent;
} gpu_image_t;

typedef struct
{
    gpu_image_status_t status;
    VkBuffer           buffer;
    VkDeviceMemory     memory;
    VkDeviceSize       size;
    bool               mapped;
    void*              mapped_ptr;
} gpu_staging_buffer_t;

uint32_t find_memory_type(VkPhysicalDevice      physical_device,
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

gpu_image_t create_gpu_image(device_info_t     device_info,
                             VkFormat          format,
                             uint32_t          width,
                             uint32_t          height,
                             VkImageUsageFlags usage)
{
    gpu_image_t result = {
        .status = GPU_IMAGE_OK,
        .format = format,
        .extent = { width, height },
    };

    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = format,
        .extent        = { width, height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult vk_result
        = vkCreateImage(device_info.device, &image_info, NULL, &result.image);
    if (vk_result != VK_SUCCESS)
    {
        printf("failed to create gpu image: %d\n", vk_result);
        result.status = GPU_IMAGE_CREATION_FAILED;
        return result;
    }

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device_info.device, result.image, &mem_req);

    uint32_t memory_type_index
        = find_memory_type(device_info.physical_device,
                           mem_req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type_index == UINT32_MAX)
    {
        printf("no suitable memory type found for gpu image\n");
        vkDestroyImage(device_info.device, result.image, NULL);
        result.image  = VK_NULL_HANDLE;
        result.status = GPU_IMAGE_MEMORY_TYPE_NOT_FOUND;
        return result;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_req.size,
        .memoryTypeIndex = memory_type_index,
    };

    vk_result = vkAllocateMemory(
        device_info.device, &alloc_info, NULL, &result.memory);
    if (vk_result != VK_SUCCESS)
    {
        printf("failed to allocate gpu image memory: %d\n", vk_result);
        vkDestroyImage(device_info.device, result.image, NULL);
        result.image  = VK_NULL_HANDLE;
        result.status = GPU_IMAGE_MEMORY_ALLOC_FAILED;
        return result;
    }

    vk_result
        = vkBindImageMemory(device_info.device, result.image, result.memory, 0);
    if (vk_result != VK_SUCCESS)
    {
        printf("failed to bind gpu image memory: %d\n", vk_result);
        vkFreeMemory(device_info.device, result.memory, NULL);
        vkDestroyImage(device_info.device, result.image, NULL);
        result.memory = VK_NULL_HANDLE;
        result.image  = VK_NULL_HANDLE;
        result.status = GPU_IMAGE_MEMORY_BIND_FAILED;
        return result;
    }

    return result;
}

void destroy_gpu_image(device_info_t device_info, gpu_image_t* image)
{
    if (!image)
    {
        return;
    }

    if (image->image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device_info.device, image->image, NULL);
    }

    if (image->memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_info.device, image->memory, NULL);
    }

    image->status = GPU_IMAGE_OK;
    image->image  = VK_NULL_HANDLE;
    image->memory = VK_NULL_HANDLE;
}

gpu_staging_buffer_t create_staging_buffer(device_info_t device_info,
                                           VkDeviceSize  size)
{
    gpu_staging_buffer_t result = {
        .status     = GPU_IMAGE_OK,
        .size       = size,
        .mapped     = false,
        .mapped_ptr = NULL,
    };

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };

    VkResult vk_result = vkCreateBuffer(
        device_info.device, &buffer_info, NULL, &result.buffer);
    if (vk_result != VK_SUCCESS)
    {
        printf("failed to create staging buffer: %d\n", vk_result);
        result.status = GPU_IMAGE_BUFFER_CREATION_FAILED;
        return result;
    }

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device_info.device, result.buffer, &mem_req);

    uint32_t memory_type_index
        = find_memory_type(device_info.physical_device,
                           mem_req.memoryTypeBits,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                               | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memory_type_index == UINT32_MAX)
    {
        printf("no suitable memory type found for staging buffer\n");
        vkDestroyBuffer(device_info.device, result.buffer, NULL);
        result.buffer = VK_NULL_HANDLE;
        result.status = GPU_IMAGE_MEMORY_TYPE_NOT_FOUND;
        return result;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_req.size,
        .memoryTypeIndex = memory_type_index,
    };

    vk_result = vkAllocateMemory(
        device_info.device, &alloc_info, NULL, &result.memory);
    if (vk_result != VK_SUCCESS)
    {
        printf("failed to allocate staging buffer memory: %d\n", vk_result);
        vkDestroyBuffer(device_info.device, result.buffer, NULL);
        result.buffer = VK_NULL_HANDLE;
        result.status = GPU_IMAGE_BUFFER_MEMORY_ALLOC_FAILED;
        return result;
    }

    vk_result = vkBindBufferMemory(
        device_info.device, result.buffer, result.memory, 0);
    if (vk_result != VK_SUCCESS)
    {
        printf("failed to bind staging buffer memory: %d\n", vk_result);
        vkFreeMemory(device_info.device, result.memory, NULL);
        vkDestroyBuffer(device_info.device, result.buffer, NULL);
        result.memory = VK_NULL_HANDLE;
        result.buffer = VK_NULL_HANDLE;
        result.status = GPU_IMAGE_BUFFER_MEMORY_BIND_FAILED;
        return result;
    }

    return result;
}

gpu_image_status_t map_staging_buffer(device_info_t         device_info,
                                      gpu_staging_buffer_t* buffer)
{
    if (!buffer || buffer->memory == VK_NULL_HANDLE)
    {
        return GPU_IMAGE_INVALID_ARGUMENT;
    }

    if (buffer->mapped)
    {
        // Mapping the same memory block repeatedly is legal but creates extra
        // state transitions for no gain. Treat map() as idempotent so callers
        // can safely call it in layered tooling paths.
        buffer->status = GPU_IMAGE_OK;
        return GPU_IMAGE_OK;
    }

    VkResult vk_result = vkMapMemory(device_info.device,
                                     buffer->memory,
                                     0,
                                     buffer->size,
                                     0,
                                     &buffer->mapped_ptr);
    if (vk_result != VK_SUCCESS)
    {
        buffer->mapped_ptr = NULL;
        buffer->status     = GPU_IMAGE_MAP_FAILED;
        return buffer->status;
    }

    buffer->mapped = true;
    buffer->status = GPU_IMAGE_OK;
    return GPU_IMAGE_OK;
}

void unmap_staging_buffer(device_info_t         device_info,
                          gpu_staging_buffer_t* buffer)
{
    if (!buffer || buffer->memory == VK_NULL_HANDLE)
    {
        return;
    }

    if (!buffer->mapped)
    {
        // Unmap is intentionally a no-op for already-unmapped buffers so
        // cleanup paths can call it unconditionally without tracking extra
        // flags.
        return;
    }

    vkUnmapMemory(device_info.device, buffer->memory);
    buffer->mapped     = false;
    buffer->mapped_ptr = NULL;
    buffer->status     = GPU_IMAGE_OK;
}

void destroy_staging_buffer(device_info_t         device_info,
                            gpu_staging_buffer_t* buffer)
{
    if (!buffer)
    {
        return;
    }

    unmap_staging_buffer(device_info, buffer);

    if (buffer->buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device_info.device, buffer->buffer, NULL);
    }

    if (buffer->memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_info.device, buffer->memory, NULL);
    }

    buffer->status     = GPU_IMAGE_OK;
    buffer->buffer     = VK_NULL_HANDLE;
    buffer->memory     = VK_NULL_HANDLE;
    buffer->size       = 0;
    buffer->mapped     = false;
    buffer->mapped_ptr = NULL;
}

gpu_image_status_t record_image_barrier(VkCommandBuffer cmd,
                                        VkImage         image,
                                        VkImageLayout   old_layout,
                                        VkImageLayout   new_layout)
{
    typedef struct
    {
        VkImageLayout        old_layout;
        VkImageLayout        new_layout;
        VkAccessFlags        src_access;
        VkAccessFlags        dst_access;
        VkPipelineStageFlags src_stage;
        VkPipelineStageFlags dst_stage;
    } layout_transition_rule_t;

    // Centralizing transition policy here keeps synchronization behavior
    // consistent across runtime features (presentation), test tools
    // (readback), and future capture/debug workflows.
    static const layout_transition_rule_t transition_rules[] = {
        { VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          0,
          VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT },
        { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_TRANSFER_READ_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT },
        { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          VK_ACCESS_TRANSFER_WRITE_BIT,
          0,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT },
        { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          VK_ACCESS_TRANSFER_WRITE_BIT,
          VK_ACCESS_SHADER_READ_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT },
        { VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          VK_ACCESS_TRANSFER_READ_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT },
    };

    const layout_transition_rule_t* selected_rule = NULL;
    for (size_t i = 0;
         i < sizeof(transition_rules) / sizeof(transition_rules[0]);
         i++)
    {
        if (transition_rules[i].old_layout == old_layout
            && transition_rules[i].new_layout == new_layout)
        {
            selected_rule = &transition_rules[i];
            break;
        }
    }

    if (!selected_rule)
    {
        printf("unsupported image layout transition: %d -> %d\n",
               old_layout,
               new_layout);
        return GPU_IMAGE_UNSUPPORTED_LAYOUT_TRANSITION;
    }

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = selected_rule->src_access,
        .dstAccessMask       = selected_rule->dst_access,
        .oldLayout           = selected_rule->old_layout,
        .newLayout           = selected_rule->new_layout,
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
                         selected_rule->src_stage,
                         selected_rule->dst_stage,
                         0,
                         0,
                         NULL,
                         0,
                         NULL,
                         1,
                         &barrier);

    return GPU_IMAGE_OK;
}

gpu_image_status_t record_image_to_buffer_copy(
    VkCommandBuffer             cmd,
    const gpu_image_t*          image,
    const gpu_staging_buffer_t* buffer)
{
    if (!image || !buffer || image->image == VK_NULL_HANDLE
        || buffer->buffer == VK_NULL_HANDLE)
    {
        return GPU_IMAGE_INVALID_ARGUMENT;
    }

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
        .imageExtent
        = { image->extent.width, image->extent.height, 1 },
    };

    vkCmdCopyImageToBuffer(cmd,
                           image->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer->buffer,
                           1,
                           &copy_region);
    return GPU_IMAGE_OK;
}

#endif  // GPU_IMAGE_H
