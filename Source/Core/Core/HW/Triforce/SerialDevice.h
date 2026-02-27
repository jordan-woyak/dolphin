// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <deque>
#include <span>
#include <vector>

#include "Common/CommonTypes.h"

class PointerWrap;

namespace Triforce
{

class SerialDevice
{
public:
  SerialDevice() = default;
  virtual ~SerialDevice() = default;

  SerialDevice(const SerialDevice&) = delete;
  SerialDevice& operator=(const SerialDevice&) = delete;
  SerialDevice(SerialDevice&&) = delete;
  SerialDevice& operator=(SerialDevice&&) = delete;

  virtual void DoState(PointerWrap& p);

  void WriteBytes(std::span<const u8> bytes);

  std::size_t GetOutputCount() const { return m_tx_buffer.size(); }

  // Caller should ensure buffer contains byte.size() bytes.
  void TakeOutput(std::span<u8> bytes);

  // TODO: Better name ?
  virtual void Process() = 0;

protected:
  std::span<const u8> GetInputSpan() const { return m_rx_buffer; }

  void ChewBytes(std::size_t count);

  void OutputByte(u8 byte) { m_tx_buffer.emplace_back(byte); }

  void OutputBytes(std::span<const u8> bytes);

private:
  // The stream of bytes from the baseboard to the device.
  // FYI: Current device implementations tend to empty the entire buffer in one go,
  //  so std::vector's O(n) erase-at-front should be a non-issue.
  // The contiguous data of std::vector is convenient for packet parsing.
  std::vector<u8> m_rx_buffer;

  // The stream of bytes from the device to the baseboard.
  // It may be read in chunks so std::vector would be less appropriate here.
  std::deque<u8> m_tx_buffer;
};

}  // namespace Triforce
