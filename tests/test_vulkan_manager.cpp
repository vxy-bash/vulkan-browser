#include <gtest/gtest.h>
#include "vulkan/VulkanManager.h"

// These tests validate the VulkanManager singleton lifecycle without
// an actual display surface.  They are expected to be run on a machine
// that has a Vulkan-capable GPU and driver installed.

TEST(VulkanManagerTest, SingletonIsAlwaysSameInstance)
{
    auto& a = vkb::VulkanManager::instance();
    auto& b = vkb::VulkanManager::instance();
    EXPECT_EQ(&a, &b);
}

TEST(VulkanManagerTest, NotInitializedBeforeInit)
{
    EXPECT_FALSE(vkb::VulkanManager::instance().isInitialized());
}

// More integration tests (requiring a real GPU) would be in a separate
// target that is only run in CI environments with GPU passthrough.
