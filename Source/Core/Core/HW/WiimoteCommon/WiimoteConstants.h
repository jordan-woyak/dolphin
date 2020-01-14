// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/CommonTypes.h"

namespace WiimoteCommon
{
constexpr u8 MAX_PAYLOAD = 23;

// Based on testing, old WiiLi.org docs, and WiiUse library:
// Max battery level seems to be 0xc8 (decimal 200)
constexpr u8 MAX_BATTERY_LEVEL = 0xc8;

// TODO: eliminate redundant definitions.
enum : u16
{
  PAD_LEFT = 0x01,
  PAD_RIGHT = 0x02,
  PAD_DOWN = 0x04,
  PAD_UP = 0x08,
  BUTTON_PLUS = 0x10,

  BUTTON_TWO = 0x0100,
  BUTTON_ONE = 0x0200,
  BUTTON_B = 0x0400,
  BUTTON_A = 0x0800,
  BUTTON_MINUS = 0x1000,
  BUTTON_HOME = 0x8000,
};

enum class InputReportID : u8
{
  Status = 0x20,
  ReadDataReply = 0x21,
  Ack = 0x22,

  // Not a real value on the wiimote, just a state to disable reports:
  ReportDisabled = 0x00,

  ReportCore = 0x30,
  ReportCoreAccel = 0x31,
  ReportCoreExt8 = 0x32,
  ReportCoreAccelIR12 = 0x33,
  ReportCoreExt19 = 0x34,
  ReportCoreAccelExt16 = 0x35,
  ReportCoreIR10Ext9 = 0x36,
  ReportCoreAccelIR10Ext6 = 0x37,

  ReportExt21 = 0x3d,
  ReportInterleave1 = 0x3e,
  ReportInterleave2 = 0x3f,
};

enum class OutputReportID : u8
{
  Rumble = 0x10,
  LED = 0x11,
  ReportMode = 0x12,
  IRLogicEnable = 0x13,
  SpeakerEnable = 0x14,
  RequestStatus = 0x15,
  WriteData = 0x16,
  ReadData = 0x17,
  SpeakerData = 0x18,
  SpeakerMute = 0x19,
  IRLogicEnable2 = 0x1a,
};

enum class LED : u8
{
  None = 0x00,
  LED_1 = 0x10,
  LED_2 = 0x20,
  LED_3 = 0x40,
  LED_4 = 0x80
};

enum class AddressSpace : u8
{
  // FYI: The EEPROM address space is offset 0x0070 on i2c slave 0x50.
  // However attempting to access this device directly results in an error.
  EEPROM = 0x00,
  // 0x02 Doesn't always seem to work on a real remote. More testing is needed.
  I2CBus = 0x01,
  I2CBusAlt = 0x02,
};

enum class ErrorCode : u8
{
  // Normal result.
  Success = 0,
  // Produced by read/write attempts during an active read.
  Busy = 4,
  // Produced by using something other than the above AddressSpace values.
  InvalidSpace = 6,
  // Produced by an i2c read/write with a non-responding slave address.
  Nack = 7,
  // Produced by accessing invalid regions of EEPROM or the EEPROM directly over i2c.
  InvalidAddress = 8,
};

}  // namespace WiimoteCommon
