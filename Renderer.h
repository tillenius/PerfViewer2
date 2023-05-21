#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define VULKAN_HPP_NO_EXCEPTIONS
#define VULKAN_HPP_TYPESAFE_CONVERSION
#include <vulkan/vulkan.h>
#include <vector>

#include "VertexData.h"

class Render {
public:

    ~Render();

    PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
    PFN_vkCmdEndRenderingKHR vkCmdEndRenderingKHR;

    bool init(HINSTANCE hinstance, HWND hwnd, const std::vector<vertex_t> & vertices, const std::vector<uint32_t> & line_indices, const std::vector<uint32_t> & triangle_indices);
    void draw();
    bool resize();

    float m_x =  0.0f;
    float m_y =  0.0f;
    float m_sx = 1.0f;
    float m_sy = 1.0f;
    uint32_t                            m_selected_index = 999;

private:
    VkDeviceMemory alloc(VkImage image, VkMemoryPropertyFlags properties);
    VkDeviceMemory alloc(VkBuffer image, VkMemoryPropertyFlags properties);
    VkDeviceMemory alloc(VkMemoryRequirements requirements, VkMemoryPropertyFlags properties);

    static VkInstance create_instance();
    static VkPhysicalDevice select_physical_device(VkInstance instance);
    static VkDevice create_device(VkPhysicalDevice physical_device, uint32_t queue_family_index);

    VkSurfaceKHR create_surface(HINSTANCE hinstance, HWND hwnd) const;
    bool create_swapchain(VkSwapchainKHR old_swapchain, VkSurfaceCapabilitiesKHR & surface_capabilities);
    bool create_command_buffers();
    bool setup_descriptors();
    bool create_pipeline();
    bool update_uniform_buffer();
    bool setup_vertex_buffer(const std::vector<vertex_t> & vertices, const std::vector<uint32_t> & line_indices, const std::vector<uint32_t> & triangle_indices);
    bool render(uint32_t swapchain_index);

    VkResult acquire_next_image(uint32_t frame, uint32_t & image_index);

    static constexpr uint32_t IMAGE_COUNT = 3;
    static constexpr VkSampleCountFlagBits SAMPLES = VK_SAMPLE_COUNT_1_BIT;
    static constexpr VkFormat COLOR_FORMAT = VK_FORMAT_B8G8R8A8_UNORM;
    static constexpr const char * DEVICE_EXTENSIONS[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
        VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME
    };

    uint32_t                            m_index_count_line;
    uint32_t                            m_index_count_tri;
    VkDeviceSize                        m_vertex_buffer_index_offset_line;
    VkDeviceSize                        m_vertex_buffer_index_offset_tri;
    VkInstance                          m_instance;
    VkPhysicalDevice                    m_physical_device;
    VkPhysicalDeviceMemoryProperties    m_memory_properties;
    VkSurfaceKHR                        m_surface;
    VkSurfaceCapabilitiesKHR            m_surface_capabilities;
    uint32_t                            m_queue_family_index;
    VkDevice                            m_device;
    VkQueue                             m_queue;
    VkCommandPool                       m_command_pool;
    VkExtent2D                          m_extent;
    VkSwapchainKHR                      m_swapchain;
    VkPipeline                          m_pipeline[2];
    VkPipelineLayout                    m_pipeline_layout;
    VkBuffer                            m_vertex_buffer;
    VkDeviceMemory                      m_vertex_buffer_memory;
    VkDescriptorPool                    m_descriptor_pool;
    VkDescriptorSetLayout               m_descriptor_set_layout;
    VkDescriptorSet                     m_descriptor_set;
    VkDescriptorBufferInfo              m_uniform_buffer_descriptor;
    VkBuffer                            m_uniform_buffer;
    VkDeviceMemory                      m_uniform_buffer_memory;
    void *                              m_uniform_memory_data;
    std::vector<VkImage>                m_images;
    std::vector<VkImageView>            m_image_views;
    std::vector<VkCommandBuffer>        m_command_buffers;
    std::vector<VkSemaphore>            m_swapchain_acquire_semaphore;
    std::vector<VkSemaphore>            m_swapchain_release_semaphore;
    std::vector<VkFence>                m_queue_submit_fence;
    uint32_t                            m_frame = 0;
    bool                                m_init = false;
};
