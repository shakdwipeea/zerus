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

#include "gpu_image.h"

typedef enum
{
    FRAME_OK,
    FRAME_SWAPCHAIN_OUT_OF_DATE,
    FRAME_ERROR
} frame_status_t;

#define FRAME_MAX_SWAPCHAIN_IMAGES 8

typedef struct
{
    frame_status_t  status;
    VkCommandPool   command_pool;
    VkCommandBuffer command_buffer;
    VkSemaphore     image_available;
    VkSemaphore     render_finished[FRAME_MAX_SWAPCHAIN_IMAGES];
    uint32_t        render_finished_count;
    VkFence         in_flight;
} frame_state_t;

static frame_status_t ensure_render_finished_semaphore(frame_state_t* frame,
                                                       VkDevice       device,
                                                       uint32_t image_index)
{
    if (image_index >= FRAME_MAX_SWAPCHAIN_IMAGES)
    {
        printf("swapchain image index %u exceeds frame semaphore budget\n",
               image_index);
        return FRAME_ERROR;
    }

    if (frame->render_finished[image_index] != VK_NULL_HANDLE)
    {
        return FRAME_OK;
    }

    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    VkResult res = vkCreateSemaphore(
        device, &sem_info, NULL, &frame->render_finished[image_index]);
    if (res != VK_SUCCESS)
    {
        printf("failed to create render-finished semaphore for image %u\n",
               image_index);
        return FRAME_ERROR;
    }

    if (image_index + 1 > frame->render_finished_count)
    {
        frame->render_finished_count = image_index + 1;
    }

    return FRAME_OK;
}

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
    vkCreateFence(device_info.device, &fence_info, nullptr, &state.in_flight);

    return state;
}

// Wait for the in-flight fence, reset it, then reset and begin the command
// buffer for recording.  Usable by both windowed (swapchain) and headless
// (offscreen / test) paths.
VkCommandBuffer frame_begin_recording(frame_state_t* frame, VkDevice device)
{
    vkWaitForFences(device, 1, &frame->in_flight, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &frame->in_flight);

    VkCommandBuffer cmd = frame->command_buffer;
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    return cmd;
}

// End recording and submit the command buffer with the in-flight fence.
// No semaphore wait/signal — suitable for headless / offscreen rendering.
frame_status_t frame_end_and_submit(frame_state_t* frame, VkQueue queue)
{
    vkEndCommandBuffer(frame->command_buffer);

    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &frame->command_buffer,
    };
    VkResult res = vkQueueSubmit(queue, 1, &submit, frame->in_flight);
    if (res != VK_SUCCESS)
    {
        printf("failed to submit command buffer: %d\n", res);
        return FRAME_ERROR;
    }
    return FRAME_OK;
}

frame_begin_result_t begin_frame(frame_state_t* frame,
                                 device_info_t* device,
                                 VkSwapchainKHR swapchain)
{
    frame_begin_result_t result = { 0 };

    VkCommandBuffer cmd = frame_begin_recording(frame, device->device);

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

    gpu_image_status_t transition_status
        = record_image_barrier(cmd,
                               image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    if (transition_status != GPU_IMAGE_OK)
    {
        return FRAME_ERROR;
    }

    frame_status_t semaphore_status
        = ensure_render_finished_semaphore(frame, device->device, image_index);
    if (semaphore_status != FRAME_OK)
    {
        return FRAME_ERROR;
    }

    VkSemaphore render_finished = frame->render_finished[image_index];

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
                .pSignalSemaphores    = &render_finished,
    };
    vkQueueSubmit(device->graphics_queue, 1, &submit_info, frame->in_flight);

    // Present
    VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &render_finished,
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

    for (uint32_t i = 0; i < FRAME_MAX_SWAPCHAIN_IMAGES; i++)
    {
        if (frame->render_finished[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(
                device_info.device, frame->render_finished[i], nullptr);
        }
    }

    vkDestroyFence(device_info.device, frame->in_flight, nullptr);
    vkDestroyCommandPool(device_info.device, frame->command_pool, nullptr);
}

#endif  // FRAME_H
