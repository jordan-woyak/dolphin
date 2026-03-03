// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#include "Common/CommonTypes.h"

#include "Core/HW/Triforce/SerialDevice.h"

namespace Triforce
{

// IC card reader.
// Used by: GekitouProYakyuu, KeyOfAvalon, VirtuaStriker4[_2006]
class ICCardReader final : public SerialDevice
{
public:
  explicit ICCardReader(u8 card_slot);

  void Process() override;

  void DoState(PointerWrap& p) override;

private:
  void CreateCards(u8 card_count);

  void SendReply(u8 command, u16 status_code, std::span<const u8> payload);

  static constexpr std::size_t PAGE_SIZE = 8;
  static constexpr u32 PAGE_COUNT = 256;
  static constexpr u32 PAGE_INDEX_MASK = 0xff;

  class ICCard
  {
  public:
    using UID = std::array<u8, PAGE_SIZE>;

    ICCard(std::string filename, const UID& uid);

    // Load from file if it exists, else create fresh card data.
    void Initialize();

    const auto& GetUID() const { return m_uid; }
    bool IsHalted() const { return m_current_state == State::Halted; }

    // Returns an empty span on error.
    std::span<const u8> ReadData(u16 page, u16 page_count);

    bool WriteData(u16 page, std::span<const u8> write_span);

    // Returns the new value.
    u16 DecreaseUseCount(u16 page, u16 amount);

    void SetHalted() { m_current_state = State::Halted; }
    void SetIdle() { m_current_state = State::Idle; }

    void DoState(PointerWrap& p);

  private:
    void FlushData(u32 byte_offset, u32 byte_count);

    std::array<u8, PAGE_SIZE * PAGE_COUNT> m_data{};

    const std::string m_filename;

    const UID m_uid;

    enum class State : u8
    {
      Idle = 0,
      Halted = 1,
    };

    // I believe "Halted" cards don't respond in "AntiCollision".
    State m_current_state = State::Idle;
  };

  // FYI: These are more like multiple RFID tags on a single card.
  // Avalon makes use of this for not yet fully understood reasons.
  std::vector<std::unique_ptr<ICCard>> m_ic_cards;

  bool m_is_field_on = false;

  // VirtuaStriker4 has 2 IC card slots.
  const u8 m_card_slot;
};

}  // namespace Triforce
