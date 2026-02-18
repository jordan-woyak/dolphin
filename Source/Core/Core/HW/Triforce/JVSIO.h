// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <span>
#include <vector>

#include "Common/CommonTypes.h"

static constexpr u8 JVSIO_SYNC = 0xe0;
static constexpr u8 JVSIO_MARK = 0xd0;
static constexpr u8 JVSIO_BROADCAST_ADDRESS = 0xff;

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

enum class JVSIOStatusCode : u8
{
  Normal = 1,
  UnknownCommand = 2,
  ChecksumError = 3,
  AcknowledgeOverflow = 4,
};

enum class JVSIOReportCode : u8
{
  Normal = 1,
  ParameterSizeError = 2,
  ParameterDataError = 3,
  Busy = 4,
};

// TODO: Clean up naming.
enum class JVSIOCommand : u8
{
  IOIdentify = 0x10,
  CommandRevision = 0x11,
  JVSRevision = 0x12,
  CommVersion = 0x13,
  FeatureCheck = 0x14,
  MainID = 0x15,

  SwitchInput = 0x20,
  CoinInput = 0x21,
  AnalogInput = 0x22,
  RotaryInput = 0x23,
  KeycodeInput = 0x24,
  ScreenPositionInput = 0x25,
  MiscSwitchInput = 0x26,

  RemainingPayout = 0x2e,
  DataRetransmit = 0x2f,
  CoinCounterDec = 0x30,
  PayoutCounterInc = 0x31,
  GenericOutput1 = 0x32,
  AnalogOutput = 0x33,
  CharacterOutput = 0x34,
  CoinCounterInc = 0x35,
  PayoutCounterDec = 0x36,
  GenericOutput2 = 0x37,
  GenericOutput3 = 0x38,

  // Vendor-specific commands:
  NAMCOCommand = 0x70,

  Reset = 0xf0,
  SetAddress = 0xf1,
  CommMethodChange = 0xf2,
};

namespace TriforcePeripheral
{

class JVSClient
{
public:
  // Returns the number of read bytes.
  u32 ProcessJVSIO(std::span<const u8> input);

  // struct FrameReader
  // {
  //   bool HasByte() const;
  //   u8 ReadNextByte();

  //   // TODO: Private:
  //   std::span<const u8> data;
  // };

  struct JVSIOFrameContext
  {
    std::vector<u8> request;
    JVSIOMessage message;
  };

  using JVSIOMessageHandler = std::function<JVSIOReportCode(JVSIOFrameContext)>;

  void SetJVSIOHandler(JVSIOCommand, JVSIOMessageHandler);

private:
  std::span<u8> UnescapeData(std::span<const u8> input, std::span<u8> output);
  void ProcessFrame(std::span<const u8> data);

  std::array<JVSIOMessageHandler, 0x100> m_handlers;

  u8 m_client_address = 0;
  std::vector<u8> m_last_acknowledge;
};

}  // namespace TriforcePeripheral
