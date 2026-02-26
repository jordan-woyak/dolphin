// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <numeric>
#include <span>

#include "Common/CommonTypes.h"

class PointerWrap;

namespace Triforce
{

// TODO: Better name ?
struct ICCardReplyHeader
{
  u8 fixed;  // Games seem to usually expect 0x10.
  u8 command;
  u16 length;  // Big-endian, includes status and payload bytes.
  u16 status;  // Big-endian.
};

// TODO: move elsewhere
constexpr u8 CheckSumXOR(std::span<const u8> data)
{
  return std::accumulate(data.data(), data.data() + data.size(), u8{}, std::bit_xor());
}

// Serial deck reader used by The Key of Avalon.
class DeckReader
{
public:
  // TODO: This is an ugly interface..
  void Process(u8 cd_reader_command, const std::function<void(std::span<const u8>)>& callback);

  void DoState(PointerWrap& p);

private:
};

}  // namespace Triforce
