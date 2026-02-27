// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Common/CommonTypes.h"

#include "Core/HW/Triforce/SerialDevice.h"

namespace Triforce
{

// IC card reader.
// Used by: GekitouProYakyuu, KeyOfAvalon, VirtuaStriker4[_2006]
class ICCardReader final : public SerialDevice
{
public:
  ICCardReader();

  void Process() override;

  // TODO: Hacky interface to connect card insertion to user input.
  void ToggleCardState();

  void DoState(PointerWrap& p) override;

private:
  static constexpr std::size_t PAGE_SIZE = 8;
  static constexpr u32 PAGE_COUNT = 256;

  // TODO: Write this to the filesystem.
  std::array<u8, PAGE_SIZE * PAGE_COUNT> m_ic_card_data{};

  // TODO: state:
  bool m_is_field_on{};
};

}  // namespace Triforce
