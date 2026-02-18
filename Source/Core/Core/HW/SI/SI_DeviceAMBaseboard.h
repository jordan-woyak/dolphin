// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Core/HW/GCPad.h"
#include "Core/HW/MagCard/MagneticCardReader.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Triforce/TriforcePeripheral.h"

#include "InputCommon/GCPadStatus.h"

namespace Movie
{
class MovieManager;
}

namespace SerialInterface
{

// Triforce (GC-AM) baseboard
class CSIDevice_AMBaseboard : public ISIDevice
{
  enum EButtonCombo
  {
    COMBO_NONE = 0,
    COMBO_ORIGIN,
    COMBO_RESET
  };

public:
  // constructor
  CSIDevice_AMBaseboard(Core::System& system, SIDevices device, int device_number);

  // run the SI Buffer
  int RunBuffer(u8* buffer, int request_length) override;

  u32 RunGCAMBuffer(std::span<const u8> input, std::span<u8> output);

  // return true on new data
  DataResponse GetData(u32& hi, u32& low) override;

  // send a command directly
  void SendCommand(u32 command, u8 poll) override;

  void DoState(PointerWrap&) override;

private:
  enum BaseBoardCommand
  {
    GCAM_Reset = 0x00,
    GCAM_Command = 0x70,
  };

  enum GCAMCommand
  {
    StatusSwitches = 0x10,
    SerialNumber = 0x11,
    Unknown_12 = 0x12,
    Unknown_14 = 0x14,
    FirmVersion = 0x15,
    FPGAVersion = 0x16,
    RegionSettings = 0x1F,

    Unknown_21 = 0x21,
    Unknown_22 = 0x22,
    Unknown_23 = 0x23,
    Unknown_24 = 0x24,

    SerialA = 0x31,
    SerialB = 0x32,

    JVSIOA = 0x40,
    JVSIOB = 0x41,

    Unknown_60 = 0x60,
  };

  static constexpr u32 STANDARD_RESPONSE_SIZE = 0x80;

  // Reply has to be delayed due a bug in the parser.
  std::array<std::array<u8, STANDARD_RESPONSE_SIZE>, 2> m_response_buffers{};
  u8 m_current_response_buffer_index = 0;

  // Magnetic Card Reader
  MagCard::MagneticCardReader::Settings m_mag_card_settings;

  std::vector<u8> m_mag_card_in_buffer;
  std::vector<u8> m_mag_card_out_buffer;

  std::unique_ptr<MagCard::MagneticCardReader> m_mag_card_reader;

  // Game specific hardware
  std::unique_ptr<TriforcePeripheral::Peripheral> m_peripheral;
};

}  // namespace SerialInterface
