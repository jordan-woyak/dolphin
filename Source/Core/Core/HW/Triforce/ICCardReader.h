// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>

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
  void SendReply(u8 command, u16 status_code, std::span<const u8> payload);

  static constexpr std::size_t PAGE_SIZE = 8;
  static constexpr u32 PAGE_COUNT = 256;

  // TODO: Write this to the filesystem.
  // std::array<u8, PAGE_SIZE * PAGE_COUNT> m_ic_card_data{};

  class ICCard
  {
  public:
    using UID = std::array<u8, PAGE_SIZE>;

    // TODO: Bad interface with use_count only for new cards..
    ICCard(std::string filename, const UID& uid, u32 use_count);

    std::span<const u8> ReadData(u32 byte_offset, u32 byte_count);
    bool WriteData(u32 byte_offset, std::span<const u8> write_span);

    void Halt() { m_is_halted = true; }
    void FieldOff() { m_is_halted = false; }

    const auto& GetUID() const { return m_uid; }
    bool IsHalted() const { return m_is_halted; }

  private:
    void FlushData(u32 byte_offset, u32 byte_count);

    std::array<u8, PAGE_SIZE * PAGE_COUNT> m_data{};

    const std::string m_filename;

    const UID m_uid;

    bool m_is_halted = false;
  };

  std::list<ICCard> m_ic_cards;

  ICCard* m_selected_card = nullptr;

  // TODO: state:
  bool m_is_field_on{};
};

}  // namespace Triforce
