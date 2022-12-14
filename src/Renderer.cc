#include "Renderer.h"

// std
#include <array>
#include <cstdio>
#include <iostream>
#include <stdexcept>

namespace VE
{
    struct SimplePushConstantData
    {
        glm::mat2 transform{1.0F};
        glm::vec2 offset;
        alignas(16) glm::vec3 color;
    };

    // Constructor
    Renderer::Renderer(Window& window, Device& device)
        : m_window{window}, m_device{device}, m_currentImageIndex{}, m_isFrameStarted{}, m_currentFrameIndex{}
    {
        recreateSwapChain();
        createCommandBuffers();
    }

    // Destructor
    Renderer::~Renderer(void) { freeCommandBuffers(); }

    void Renderer::recreateSwapChain(void)
    {
        auto extent{m_window.getExtent()};

        while(extent.width == 0 || extent.height == 0)
        {
            extent = m_window.getExtent();
            glfwWaitEvents();
        }
        vkDeviceWaitIdle(m_device.device());

        if(m_swapChain == nullptr)
        {
            m_swapChain = std::make_unique<SwapChain>(m_device, m_window.getExtent());
        }
        else
        {
            std::shared_ptr<SwapChain> oldSwapChain{std::move(m_swapChain)};

            m_swapChain = std::make_unique<SwapChain>(m_device, m_window.getExtent(), oldSwapChain);

            if(!oldSwapChain->compareSwapChainFormats(*m_swapChain))
            {
                // TODO: Notify the app about the new incompatible render pass has been created
                throw std::runtime_error{"Swap chain image/depth format has changed!"};
            }
        }
    }

    void Renderer::createCommandBuffers(void)
    {
        m_commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

        // Allocate command buffers
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_device.getCommandPool();
        allocInfo.commandBufferCount = static_cast<std::uint32_t>(m_commandBuffers.size());

        if(vkAllocateCommandBuffers(m_device.device(), &allocInfo, m_commandBuffers.data()) != VK_SUCCESS)
        {
            throw std::runtime_error{"Failed to allocate command buffers!"};
        }
    }

    void Renderer::freeCommandBuffers(void)
    {
        vkFreeCommandBuffers(m_device.device(), m_device.getCommandPool(),
                             static_cast<std::uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());
        m_commandBuffers.clear();
    }

    VkCommandBuffer Renderer::beginFrame(void)
    {
        if(m_isFrameStarted)
        {
            throw std::runtime_error{"Can't call beginFrame(void) while already in progress"};
        }

        auto result{m_swapChain->acquireNextImage(&m_currentImageIndex)};
        if(result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain();
            return nullptr;
        }

        if(result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        m_isFrameStarted = true;

        auto commandBuffer{getCurrentCommandBuffer()};
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if(vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to begin recording command buffer!");
        }
        return commandBuffer;
    }

    void Renderer::endFrame(void)
    {
        if(!m_isFrameStarted)
        {
            throw std::runtime_error{"Can't call endFrame(void) while frame isn't in progress!"};
        }

        auto commandBuffer{getCurrentCommandBuffer()};

        if(vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error{"Failed to finish recording command buffer!"};
        }

        auto result{m_swapChain->submitCommandBuffers(&commandBuffer, &m_currentImageIndex)};

        if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window.wasWindowResized())
        {
            m_window.resetWindowResizedFlag();
            recreateSwapChain();
        }
        else if(result != VK_SUCCESS)
        {
            throw std::runtime_error{"Failed to present swap chain image!"};
        }

        m_isFrameStarted = false;
        m_currentFrameIndex = (m_currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
    }

    void Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer)
    {
        if(!m_isFrameStarted)
        {
            throw std::runtime_error{
                  "Can't call beginSwapChainRenderPass(VkCommandBuffer commandBuffer) while frame isn't in progress!"};
        }

        if(commandBuffer != getCurrentCommandBuffer())
        {
            throw std::runtime_error{"Can't begin render pass on command buffer from a different frame!"};
        }

        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = m_swapChain->getRenderPass();
        renderPassInfo.framebuffer = m_swapChain->getFrameBuffer(m_currentImageIndex);

        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = m_swapChain->getSwapChainExtent();

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = {{0.0F, 0.0F, 0.0F, 1.0F}};
        clearValues[1].depthStencil = {1.0F, 0};

        renderPassInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0F;
        viewport.y = 0.0F;
        viewport.width = static_cast<float>(m_swapChain->getSwapChainExtent().width);
        viewport.height = static_cast<float>(m_swapChain->getSwapChainExtent().height);
        viewport.minDepth = 0.0F;
        viewport.maxDepth = 1.0F;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{{0, 0}, m_swapChain->getSwapChainExtent()};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer)
    {
        if(!m_isFrameStarted)
        {
            throw std::runtime_error{
                  "Can't call endSwapChainRenderPass(VkCommandBuffer commandBuffer) while frame isn't in progress!"};
        }

        if(commandBuffer != getCurrentCommandBuffer())
        {
            throw std::runtime_error{"Can't end render pass on command buffer from a different frame!"};
        }

        vkCmdEndRenderPass(commandBuffer);
    }

    [[nodiscard]] VkCommandBuffer Renderer::getCurrentCommandBuffer(void) const
    {
        if(!m_isFrameStarted)
        {
            throw std::runtime_error{"Can't get command buffer while frame isn't in progress!"};
        }
        return m_commandBuffers[m_currentFrameIndex];
    }

    [[nodiscard]] std::uint32_t Renderer::getFrameIndex(void) const
    {
        if(!m_isFrameStarted)
        {
            throw std::runtime_error{"Can't get frame index while frame isn't in progress!"};
        }
        return m_currentFrameIndex;
    }
}
