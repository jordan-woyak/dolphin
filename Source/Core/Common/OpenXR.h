// Copyright 2019 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>

#include "Common/MathUtil.h"
#include "Common/Matrix.h"

namespace Common::OpenXR
{
bool Init(const std::vector<std::string>& required_extensions);

bool CreateSession(const void* graphics_binding);

// TODO: timeout
bool WaitForReady();

bool BeginSession();

bool WaitFrame();
bool BeginFrame();
bool EndFrame();

bool CreateSwapchain();

bool EnumerateSwapchainImages(uint32_t count, uint32_t* capacity, void* data);

template <typename T>
std::vector<T> GetSwapchainImages(const T& image_type)
{
#if 0
  uint32_t chain_length;
  XrResult result = xrEnumerateSwapchainImages(s_swapchain, 0, &chain_length, nullptr);

  std::vector<T> images(chain_length, image_type);
  result = xrEnumerateSwapchainImages(s_swapchain, chain_length, &chain_length,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
#else
  uint32_t chain_length;
  EnumerateSwapchainImages(0, &chain_length, nullptr);

  std::vector<T> images(chain_length, image_type);
  EnumerateSwapchainImages(chain_length, &chain_length, images.data());
#endif

  return images;
}

uint32_t AquireAndWaitForSwapchainImage();
bool ReleaseSwapchainImage();
int64_t GetSwapchainFormat();
std::pair<int32_t, int32_t> GetSwapchainSize();

Matrix44 GetHeadMatrix();

}  // namespace Common::OpenXR
