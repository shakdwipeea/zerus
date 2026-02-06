//
// Zerus Game Engine - Frame Module
//
// Per-frame synchronization and presentation cadence.
// Owns: command pool, command buffer, semaphores, fence.
//
// Usage:
//   frame_begin_result_t frame = begin_frame(...)
//   record_frame(renderer, frame.cmd, image, ...)   // from renderer.h
//   end_frame(...)                                   // final transition +
//   submit + present
//

#ifndef FRAME_H
#define FRAME_H

#include <stdio.h>
#include <vulkan/vulkan_core.h>

typedef enum
{
    FRAME_OK,
    FRAME_SWAPCHAIN_OUT_OF_DATE,
    FRAME_ERROR
} frame_status_t;

typedef struct
{
    frame_status_t  status;
    VkCommandPool   command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore     image_available;
    VkSemaphore     render_finished;
    VkFence         in_flight;
} frame_state_t;

typedef struct
{
    frame_status_t  status;
    VkCommandBuffer cmd;
    uint32_t        image_index;
} frame_begin_result_t;

frame_state_t create_frame_state(device_info_t device_info)
{
    frame_state_t state = { 0 };

    // Create command pool
    VkCommandPoolCreateInfo pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device_info.queue_index,
    };
    VkResult res = vkCreateCommandPool(
        device_info.device, &pool_info, nullptr, &state.command_pool);
    if (res != VK_SUCCESS)
    {
        printf("failed to create command pool\n");
        state.status = FRAME_ERROR;
        return state;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo cmd_alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = state.command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    res = vkAllocateCommandBuffers(
        device_info.device, &cmd_alloc_info, &state.command_buffer);
    if (res != VK_SUCCESS)
    {
        printf("failed to allocate command buffer\n");
        state.status = FRAME_ERROR;
        return state;
    }

    // Create sync objects
    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    vkCreateSemaphore(
        device_info.device, &sem_info, nullptr, &state.image_available);
    vkCreateSemaphore(
        device_info.device, &sem_info, nullptr, &state.render_finished);
    vkCreateFence(device_info.device, &fence_info, nullptr, &state.in_flight);

    return state;
}

frame_begin_result_t begin_frame(frame_state_t* frame,
                                 device_info_t* device,
                                 VkSwapchainKHR swapchain)
{
    frame_begin_result_t result = { 0 };

    // Wait for previous frame to finish
    vkWaitForFences(device->device, 1, &frame->in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(device->device, 1, &frame->in_flight);

    // Acquire next swapchain image
    VkResult res = vkAcquireNextImageKHR(device->device,
                                         swapchain,
                                         UINT64_MAX,
                                         frame->image_available,
                                         VK_NULL_HANDLE,
                                         &result.image_index);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        result.status = FRAME_SWAPCHAIN_OUT_OF_DATE;
        return result;
    }

    // Reset and begin command buffer
    VkCommandBuffer cmd = frame->command_buffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    result.cmd    = cmd;
    result.status = FRAME_OK;
    return result;
}

frame_status_t end_frame(frame_state_t* frame,
                         device_info_t* device,
                         VkSwapchainKHR swapchain,
                         uint32_t       image_index,
                         VkImage        image)
{
    VkCommandBuffer cmd = frame->command_buffer;

    // Transition: TRANSFER_DST_OPTIMAL -> PRESENT_SRC_KHR
    VkImageMemoryBarrier barrier_to_present = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = 0,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier_to_present);

    vkEndCommandBuffer(cmd);

    // Submit
    VkPipelineStageFlags wait_stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo         submit_info = {
                .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .waitSemaphoreCount   = 1,
                .pWaitSemaphores      = &frame->image_available,
                .pWaitDstStageMask    = &wait_stage,
                .commandBufferCount   = 1,
                .pCommandBuffers      = &cmd,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores    = &frame->render_finished,
    };
    vkQueueSubmit(device->graphics_queue, 1, &submit_info, frame->in_flight);

    // Present
    VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &frame->render_finished,
        .swapchainCount     = 1,
        .pSwapchains        = &swapchain,
        .pImageIndices      = &image_index,
    };
    VkResult res = vkQueuePresentKHR(device->graphics_queue, &present_info);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        return FRAME_SWAPCHAIN_OUT_OF_DATE;
    }

    return FRAME_OK;
}

void destroy_frame_state(device_info_t device_info, frame_state_t* frame)
{
    vkDestroySemaphore(device_info.device, frame->image_available, nullptr);
    vkDestroySemaphore(device_info.device, frame->render_finished, nullptr);
    vkDestroyFence(device_info.device, frame->in_flight, nullptr);
    vkDestroyCommandPool(device_info.device, frame->command_pool, nullptr);
}

#endif  // FRAME_H
