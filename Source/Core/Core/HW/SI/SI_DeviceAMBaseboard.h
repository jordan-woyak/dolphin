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

  // Reply has to be delayed due a bug in the parser
  void SwapBuffers(u8* buffer, u32* buffer_length);

  // return true on new data
  DataResponse GetData(u32& hi, u32& low) override;

  // send a command directly
  void SendCommand(u32 command, u8 poll) override;

  virtual GCPadStatus GetPadStatus();
  virtual u32 MapPadStatus(const GCPadStatus& pad_status);
  virtual EButtonCombo HandleButtonCombos(const GCPadStatus& pad_status);

  static void HandleMoviePadStatus(Movie::MovieManager& movie, int device_number,
                                   GCPadStatus* pad_status);

  // Send and Receive pad input from network
  static bool NetPlay_GetInput(int pad_num, GCPadStatus* status);
  static int NetPlay_InGamePadToLocalPad(int pad_num);

  void DoState(PointerWrap&) override;

protected:
  struct SOrigin
  {
    u16 button;
    u8 origin_stick_x;
    u8 origin_stick_y;
    u8 substick_x;
    u8 substick_y;
    u8 trigger_left;
    u8 trigger_right;
    u8 unk_4;
    u8 unk_5;
  };

  // struct to compare input against
  // Set on connection to perfect neutral values
  // (standard pad only) Set on button combo to current input state
  SOrigin m_origin = {};

  // PADAnalogMode
  // Dunno if we need to do this, game/lib should set it?
  u8 m_mode = 0x3;

  // Timer to track special button combos:
  // y, X, start for 3 seconds updates origin with current status
  //   Technically, the above is only on standard pad, wavebird does not support it for example
  // b, x, start for 3 seconds triggers reset (PI reset button interrupt)
  u64 m_timer_button_combo_start = 0;
  // Type of button combo from the last/current poll
  EButtonCombo m_last_button_combo = COMBO_NONE;
  void SetOrigin(const GCPadStatus& pad_status);

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

  // TODO: Eliminate..
  enum CARDCommand
  {
    Init = 0x10,
    GetState = 0x20,
    Read = 0x33,
    IsPresent = 0x40,
    Write = 0x53,
    SetPrintParam = 0x78,
    RegisterFont = 0x7A,
    WriteInfo = 0x7C,
    Erase = 0x7D,
    Eject = 0x80,
    Clean = 0xA0,
    Load = 0xB0,
    SetShutter = 0xD0,
  };

  u8 m_last[2][0x80] = {};
  u32 m_lastptr[2] = {};

  // Magnetic Card Reader
  MagCard::MagneticCardReader::Settings m_mag_card_settings;

  std::vector<u8> m_mag_card_in_buffer;
  std::vector<u8> m_mag_card_out_buffer;

  std::unique_ptr<MagCard::MagneticCardReader> m_mag_card_reader;

  // Game specific hardware
  std::unique_ptr<TriforcePeripheral::Peripheral> m_peripheral;

  // F-Zero AX (DX)
  bool m_fzdx_seatbelt = true;
  bool m_fzdx_motion_stop = false;
  bool m_fzdx_sensor_right = false;
  bool m_fzdx_sensor_left = false;

  // F-Zero AX (CyCraft)
  bool m_fzcc_seatbelt = true;
  bool m_fzcc_sensor = false;
  bool m_fzcc_emergency = false;
  bool m_fzcc_service = false;
};

}  // namespace SerialInterface
