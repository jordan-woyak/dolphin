// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/FZeroAX.h"

#include "Common/BitUtils.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/SI/SI.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/System.h"

#include "Common/Swap.h"

#include "InputCommon/GCPadStatus.h"

namespace TriforcePeripheral
{

FZeroAXCommon::FZeroAXCommon()
{
  // TODO: Document this better
  // Client features:
  //
  // Inputs:
  // 0x01: Switch input:  players,  buttons
  // 0x02: Coin input:    slots
  // 0x03: Analog input:  channels, bits
  // 0x04: Rotary input: channels
  // 0x05: Keycode input: 0,0,0 ?
  // 0x06: Screen position input: X bits, Y bits, channels
  //
  // Outputs:
  // 0x10: Card system: slots
  // 0x11: Medal hopper: channels
  // 0x12: GPO-out: slots
  // 0x13: Analog output: channels
  // 0x14: Character output: width, height, type
  // 0x15: Backup
  //
  SetJVSIOHandler(JVSIOCommand::FeatureCheck, [](JVSIOFrameContext ctx) {
    // 2 Player (12bit) (p2=paddles), 1 Coin slot, 6 Analog-in
    // message.AddData((void *)"\x01\x02\x0C\x00", 4);
    // message.AddData((void *)"\x02\x01\x00\x00", 4);
    // message.AddData((void *)"\x03\x06\x00\x00", 4);
    // message.AddData((void *)"\x00\x00\x00\x00", 4);
    //
    // DX Version: 2 Player (22bit) (p2=paddles), 2 Coin slot, 8 Analog-in,
    // 22 Driver-out
    // ctx.message.AddData("\x01\x02\x12\x00", 4);
    // ctx.message.AddData("\x02\x02\x00\x00", 4);
    // ctx.message.AddData("\x03\x08\x0A\x00", 4);
    // ctx.message.AddData("\x12\x16\x00\x00", 4);
    // ctx.message.AddData("\x00\x00\x00\x00", 4);

    return JVSIOReportCode::Normal;
  });

  SetJVSIOHandler(JVSIOCommand::GenericOutput1, [this](JVSIOFrameContext& ctx) {
    if (!ctx.request.HasCount(1))
      return JVSIOReportCode::ParameterSizeError;

    const u8 byte_count = ctx.request.ReadByte();

    if (!ctx.request.HasCount(byte_count))
      return JVSIOReportCode::ParameterSizeError;

    ctx.request.Skip(byte_count);

    // Handling of the motion seat used in F-Zero AXs DX version
    // const u16 seat_state = Common::swap16(jvs_io + 1) >> 2;
    // jvs_io += bytes;

    // switch (seat_state)
    // {
    // case 0x70:
    //   m_delay++;
    //   if ((m_delay % 10) == 0)
    //   {
    //     m_rx_reply = 0xFB;
    //   }
    //   break;
    // case 0xF0:
    //   m_rx_reply = 0xF0;
    //   break;
    // default:
    // case 0xA0:
    // case 0x60:
    //   break;
    // }

    return JVSIOReportCode::Normal;
  });
}

FZeroAX::FZeroAX()
{
  // TODO: consolodate the common functionality of this..
  SetJVSIOHandler(JVSIOCommand::IOIdentify, [](JVSIOFrameContext& ctx) {
    ctx.message.AddData(
        Common::AsU8Span(std::span{"SEGA ENTERPRISES,LTD.;837-13844-01 I/O CNTL BD2 ;"}));
    NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0x10, BoardID");
    return JVSIOReportCode::Normal;
  });
}

FZeroAXMonster::FZeroAXMonster()
{
}

u32 FZeroAXCommon::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
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
      break;
    }

    if (m_motor_init)
    {
      std::array<u8, 3> motor_reply{};

      // Status
      motor_reply[0] = 0;
      motor_reply[1] = 0;

      // Error
      motor_reply[2] = 0;

      std::ranges::copy(motor_reply, data_out.data() + data_offset);
      data_offset += motor_reply.size();
    }
  }

  return data_offset;
}

}  // namespace TriforcePeripheral
