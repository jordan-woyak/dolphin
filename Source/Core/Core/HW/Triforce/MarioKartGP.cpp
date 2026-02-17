// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/MarioKartGP.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/System.h"

namespace TriforcePeripheral
{

u32 MarioKartGP::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  // Serial - Wheel

  const auto& serial_interface = Core::System::GetInstance().GetSerialInterface();

  u32 data_offset = 0;

  //   if (!validate_data_in_out(10, 2, "SerialA (Wheel)"))
  //     break;

  INFO_LOG_FMT(SERIALINTERFACE_AMBB,
               "GC-AM: Command 0x31, (WHEEL) {:02x}{:02x} {:02x}{:02x} {:02x} {:02x} "
               "{:02x} {:02x} {:02x} {:02x}",
               data_in[0], data_in[1], data_in[2], data_in[3], data_in[4], data_in[5], data_in[6],
               data_in[7], data_in[8], data_in[9]);

  switch (m_wheel_init)
  {
  case 0:
    data_out[data_offset++] = 'E';  // Error
    data_out[data_offset++] = '0';
    data_out[data_offset++] = '0';
    m_wheel_init++;
    break;

  case 1:
    data_out[data_offset++] = 'C';  // Power Off
    data_out[data_offset++] = '0';
    data_out[data_offset++] = '6';
    // Only turn on when a wheel is connected
    if (serial_interface.GetDeviceType(1) == SerialInterface::SIDEVICE_GC_STEERING)
    {
      m_wheel_init++;
    }
    break;

  case 2:
    data_out[data_offset++] = 'C';  // Power On
    data_out[data_offset++] = '0';
    data_out[data_offset++] = '1';
    break;

  default:
    break;
  }

  // u16 CenteringForce= ptr(6);
  // u16 FrictionForce = ptr(8);
  // u16 Roll          = ptr(10);

  return data_offset;
}

}  // namespace TriforcePeripheral
