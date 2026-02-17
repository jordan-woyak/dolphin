// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <span>

#include "Common/CommonTypes.h"

// "JAMMA Video Standard" I/O
class JVSIOMessage
{
public:
  void Start(int node);
  void AddData(const u8* dst, std::size_t len, int sync);
  void AddData(const void* data, std::size_t len);
  void AddData(const char* data);
  void AddData(u32 n);
  void End();

  u32 m_pointer = 0;
  std::array<u8, 0x80> m_message;

private:
  u32 m_last_start = 0;
  u32 m_checksum = 0;
};

// TODO: enum class
enum JVSIOStatusCode
{
  StatusOkay = 1,
  UnsupportedCommand = 2,
  ChecksumError = 3,
  AcknowledgeOverflow = 4,
};

enum class JVSIOCommand : u8
{
  IOID = 0x10,
  CommandRevision = 0x11,
  JVRevision = 0x12,
  CommunicationVersion = 0x13,
  CheckFunctionality = 0x14,
  MainID = 0x15,

  SwitchesInput = 0x20,
  CoinInput = 0x21,
  AnalogInput = 0x22,
  RotaryInput = 0x23,
  KeyCodeInput = 0x24,
  PositionInput = 0x25,
  GeneralSwitchInput = 0x26,

  PayoutRemain = 0x2E,
  Retrans = 0x2F,
  CoinSubOutput = 0x30,
  PayoutAddOutput = 0x31,
  GeneralDriverOutput = 0x32,
  AnalogOutput = 0x33,
  CharacterOutput = 0x34,
  CoinAddOutput = 0x35,
  PayoutSubOutput = 0x36,
  GeneralDriverOutput2 = 0x37,
  GeneralDriverOutput3 = 0x38,

  NAMCOCommand = 0x70,

  Reset = 0xF0,
  SetAddress = 0xF1,
  ChangeComm = 0xF2,
};

namespace TriforcePeripheral
{

class JVSIOHandler
{
public:
  void ProcessJVSIOFrame(std::span<const u8> frame);

  struct JVSIOMessageContext
  {
    JVSIOMessage message;
  };

  using JVSIOMessageHandler = std::function<void(JVSIOMessageContext)>;

  void SetJVSIOHandler(JVSIOCommand, JVSIOMessageHandler);

private:
  std::array<JVSIOMessageHandler, 0x100> m_handlers;
};

}  // namespace TriforcePeripheral
