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
  void AddData(std::span<const u8> data);
  // void AddData(const void* data, std::size_t len);
  // void AddData(const char* data);
  void AddData(u8 n);
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

  Reset = 0xf0,
  SetAddress = 0xf1,
  CommMethodChange = 0xf2,
};

enum class ClientFeature : u8
{
  SwitchInput = 0x01,          // players, buttons, 0
  CoinInput = 0x02,            // slots, 0, 0
  AnalogInput = 0x03,          // channels, bits, 0
  RotaryInput = 0x04,          // channels, 0, 0
  KeycodeInput = 0x05,         // 0, 0, 0
  ScreenPositionInput = 0x06,  // X-bits, Y-bits, channels
  MiscSwitchInput = 0x07,      // SW-MSB, SW-LSB, 0

  CardSystem = 0x10,            // slots, 0, 0
  MedalHopper = 0x11,           // channels, 0, 0
  GeneralPurposeOutput = 0x12,  // slots, 0, 0
  AnalogOutput = 0x13,          // channels, 0, 0
  CharacterOutput = 0x14,       // width, height, type
  Backup = 0x15,                // 0, 0, 0
};

// CharacterOutput type:
// 00 Unknown
// 01 ASCII (numeric)
// 02 ASCII (alphanumeric)
// 03 ASCII (alphanumeric, half-width katakana)
// 04 ASCII (kanji support, SHIFT-JIS)

namespace TriforcePeripheral
{

class JVSIOBoard
{
public:
  // Returns the number of read bytes.
  u32 ProcessJVSIO(std::span<const u8> input);

protected:
  struct ClientFeatureSpec
  {
    ClientFeature feature;
    u8 param_a;
    u8 param_b;
    u8 param_c;
  };

  struct FrameReader
  {
    bool HasCount(u32 count) const { return m_data_end - m_data >= count; }

    u8 ReadByte() { return *(m_data++); }
    void Skip(u32 count) { m_data += count; }

    // TODO: private
    u8* m_data;
    u8* m_data_end;
  };

  struct JVSIOFrameContext
  {
    FrameReader request;
    JVSIOMessage message;
  };

  virtual JVSIOReportCode HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx) = 0;

  // using JVSIOMessageHandler = std::function<JVSIOReportCode(JVSIOFrameContext&)>;

private:
  std::span<u8> UnescapeData(std::span<const u8> input, std::span<u8> output);
  void ProcessFrame(std::span<const u8> data);

  // 0 == address not yet assigned.
  u8 m_client_address = 0;

  std::vector<u8> m_last_acknowledge;
};

}  // namespace TriforcePeripheral
