// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/FZeroAX.h"

#include "Core/HW/GCPad.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/System.h"

#include "Common/Swap.h"

#include "InputCommon/GCPadStatus.h"

namespace TriforcePeripheral
{

u32 FZeroAX::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  const auto& serial_interface = Core::System::GetInstance().GetSerialInterface();

  const auto length = u32(data_in.size());

  u32 data_offset = 0;

  u32 command_offset = 0;
  while (command_offset < length)
  {
    // All commands are OR'd with 0x80
    // Last byte is checksum which we don't care about
    // if (!validate_data_in_out(command_offset + 4, 0, "SerialA"))
    //   break;
    const u32 serial_command = Common::swap32(data_in.data() + command_offset) ^ 0x80000000;

    command_offset += sizeof(u32);

    // if (command_offset + 5 >= std::size(m_motor_reply))
    // {
    //   ERROR_LOG_FMT(SERIALINTERFACE_AMBB,
    //                 "GC-AM: Command 0x31 (MOTOR) overflow: offset={} >= motor_reply_size={}",
    //                 command_offset + 5, std::size(m_motor_reply));
    //   data_in = data_in_end;
    //   break;
    // }

    // Status
    m_motor_reply[command_offset + 2] = 0;
    m_motor_reply[command_offset + 3] = 0;

    // Error
    m_motor_reply[command_offset + 4] = 0;

    switch (serial_command >> 24)
    {
    case 0:
    case 1:  // Set Maximum?
    case 2:
      break;

    // 0x00-0x40: left
    // 0x40-0x80: right
    case 4:  // Move Steering Wheel
      // Left
      if (serial_command & 0x010000)
      {
        m_motor_force_y = -((s16)serial_command & 0xFF00);
      }
      else  // Right
      {
        m_motor_force_y = (serial_command - 0x4000) & 0xFF00;
      }

      m_motor_force_y *= 2;

      // FFB
      if (m_motor_init == 2)
      {
        if (serial_interface.GetDeviceType(1) == SerialInterface::SIDEVICE_GC_STEERING)
        {
          const GCPadStatus pad_status = Pad::GetStatus(1);
          if (pad_status.isConnected)
          {
            const ControlState mapped_strength = (double)(m_motor_force_y >> 8) / 127.f;

            Pad::Rumble(1, mapped_strength);
            INFO_LOG_FMT(SERIALINTERFACE_AMBB, "GC-AM: Command 0x31 (MOTOR) mapped_strength:{}",
                         mapped_strength);
          }
        }
      }
      break;
    case 6:  // nice
    case 9:
    default:
      break;
    // Switch back to normal controls
    case 7:
      m_motor_init = 2;
      break;
    // Reset
    case 0x7F:
      m_motor_init = 1;
      memset(m_motor_reply, 0, sizeof(m_motor_reply));
      break;
    }

    // Checksum
    m_motor_reply[command_offset + 5] = m_motor_reply[command_offset + 2] ^
                                        m_motor_reply[command_offset + 3] ^
                                        m_motor_reply[command_offset + 4];

    if (m_motor_init)
    {
      const u32 reply_size = m_motor_reply[1] + 2;
      //   if (!validate_data_in_out(length, reply_size, "SerialA"))
      //     break;

      //   if (reply_size > sizeof(m_motor_reply))
      //   {
      //     ERROR_LOG_FMT(SERIALINTERFACE_AMBB,
      //                   "GC-AM: Command SerialA, reply_size={} too big for m_motor_reply",
      //                   reply_size);
      //     data_in = data_in_end;
      //     break;
      //   }
      memcpy(data_out.data() + data_offset, m_motor_reply, reply_size);
      data_offset += reply_size;
    }
  }

  return data_offset;
}

}  // namespace TriforcePeripheral
