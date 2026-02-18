// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <functional>
#include <span>
#include <vector>

#include "Common/CommonTypes.h"

// "JAMMA Video Standard" I/O

static constexpr u8 JVSIO_SYNC = 0xe0;
static constexpr u8 JVSIO_MARK = 0xd0;
static constexpr u8 JVSIO_BROADCAST_ADDRESS = 0xff;

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
    const u8* m_data;
    const u8* m_data_end;
  };

  struct ResponseWriter
  {
    friend JVSIOBoard;

    void AddData(u8 value)
    {
      m_checksum += value;

      if (m_data != m_data_end)
        *(m_data++) = value;
    }

    void AddData(std::span<const u8> data)
    {
      for (u8 value : data)
        AddData(value);
    }

  private:
    void StartFrame(u8 node)
    {
      m_checksum = 0;
      *(m_data++) = JVSIO_SYNC;
      AddData(node);
      AddData(0);  // Later becomes byte count.
      AddData(u8(JVSIOStatusCode::Normal));
    }

    u32 EndFrame()
    {
      if (m_data == m_data_end)
      {
        // TODO: Set overflow error.
        return 0;
      }

      // TODO: Fill in count.
      *(m_data++) = m_checksum;

      // TODO: Return size..
      return 5;
    }

    void StartReport() { AddData(0); }

    void SetLastReportCode(JVSIOReportCode code)
    {
      const u8 value = u8(code);
      *m_last_report_code_index = value;
      m_checksum += value;
    }

    u8* m_data;
    u8* m_data_end;

    u8 m_checksum = 0;

    u8* m_last_report_code_index;
  };

  struct JVSIOFrameContext
  {
    FrameReader request;
    ResponseWriter message;
  };

  virtual JVSIOReportCode HandleJVSIORequest(JVSIOCommand cmd, JVSIOFrameContext* ctx) = 0;

private:
  // Returns read count.
  u32 UnescapeData(std::span<const u8> input, std::span<u8> output);

  bool ProcessBroadcastFrame(std::span<const u8> frame);
  bool ProcessFrame(std::span<const u8> frame);

  // 0 == address not yet assigned.
  u8 m_client_address = 0;

  std::vector<u8> m_last_response;
};

}  // namespace TriforcePeripheral
