// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/ICCardReader.h"

#include <numeric>

#include "Common/BitUtils.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/Swap.h"

namespace
{

static constexpr char s_cdr_program_version[] = {"           Version 1.22,2003/09/19,171-8213B"};
static constexpr char s_cdr_boot_version[] = {"           Version 1.04,2003/06/17,171-8213B"};
static constexpr u8 s_cdr_card_data[] = {
    0x00, 0x6E, 0x00, 0x00, 0x01, 0x00, 0x00, 0x06, 0x00, 0x00, 0x07, 0x00, 0x00, 0x0B,
    0x00, 0x00, 0x0E, 0x00, 0x00, 0x10, 0x00, 0x00, 0x17, 0x00, 0x00, 0x19, 0x00, 0x00,
    0x1A, 0x00, 0x00, 0x1B, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x20, 0x00,
    0x00, 0x22, 0x00, 0x00, 0x23, 0x00, 0x00, 0x24, 0x00, 0x00, 0x27, 0x00, 0x00, 0x28,
    0x00, 0x00, 0x2C, 0x00, 0x00, 0x2F, 0x00, 0x00, 0x34, 0x00, 0x00, 0x35, 0x00, 0x00,
    0x37, 0x00, 0x00, 0x38, 0x00, 0x00, 0x39, 0x00, 0x00, 0x3D, 0x00};

static constexpr u8 CheckSumXOR(const u8* data, u32 length)
{
  return std::accumulate(data, data + length, u8{}, std::bit_xor());
}

}  // namespace

namespace TriforcePeripheral
{

enum CDReaderCommand
{
  ShutterAuto = 0x61,
  BootVersion = 0x62,
  SensLock = 0x63,
  SensCard = 0x65,
  FirmwareUpdate = 0x66,
  ShutterGet = 0x67,
  CameraCheck = 0x68,
  ShutterCard = 0x69,
  ProgramChecksum = 0x6B,
  BootChecksum = 0x6D,
  ShutterLoad = 0x6F,
  ReadCard = 0x72,
  ShutterSave = 0x73,
  SelfTest = 0x74,
  ProgramVersion = 0x76,
};

enum ICCARDCommand
{
  GetStatus = 0x10,
  SetBaudrate = 0x11,
  FieldOn = 0x14,
  FieldOff = 0x15,
  InsertCheck = 0x20,
  AntiCollision = 0x21,
  SelectCard = 0x22,
  ReadPage = 0x24,
  WritePage = 0x25,
  DecreaseUseCount = 0x26,
  ReadUseCount = 0x33,
  ReadPages = 0x34,
  WritePages = 0x35,
};

ICCardReader::ICCardReader()
{
  // Card ID
  m_ic_card_data[0x20] = 0x95;
  m_ic_card_data[0x21] = 0x71;

  // Use count
  m_ic_card_data[0x28] = 0xFF;
  m_ic_card_data[0x29] = 0xFF;
}

u32 ICCardReader::SerialA(std::span<const u8> data_in, std::span<u8> data_out)
{
  // Serial IC-CARD / Serial Deck Reader

  //   if (!validate_data_in_out(2, 0, "SerialA (IC-CARD)"))
  //     break;
  u32 serial_command = data_in[1];

  ICCommand icco;

  // Set default reply
  //   icco.pktcmd = gcam_command;
  //   icco.pktlen = 7;
  icco.fixed = 0x10;
  icco.command = serial_command;
  icco.flag = 0;
  icco.length = 2;
  icco.status = 0;
  icco.extlen = 0;

  // TODO:
  u32 data_offset = 0;
  u32 length = 0;

  // Check for rest of data from the write pages command
  if (m_ic_write_size && m_ic_write_offset)
  {
    const u32 size = data_in[1];

    // if (!validate_data_in_out(size + 2, 0, "SerialA (IC-CARD)"))
    //   break;
    DEBUG_LOG_FMT(SERIALINTERFACE_CARD, "Command: {}", HexDump(data_in.data(), size + 2));

    INFO_LOG_FMT(SERIALINTERFACE_CARD,
                 "GC-AM: Command 25 (IC-CARD) Write Pages: Off:{:x} Size:{:x} PSize:{:x}",
                 m_ic_write_offset, m_ic_write_size, size);

    // if (u64{m_ic_write_offset} + size > sizeof(m_ic_write_buffer))
    // {
    //   ERROR_LOG_FMT(SERIALINTERFACE_CARD,
    //                 "GC-AM: Command 25 (IC-CARD) m_ic_write_buffer overflow:\n"
    //                 " - m_ic_write_buffer(offset={}, size={})\n"
    //                 " - size={}\n",
    //                 m_ic_write_offset, sizeof(m_ic_write_buffer), size);
    //   data_in = data_in_end;
    //   break;
    // }
    memcpy(m_ic_write_buffer + m_ic_write_offset, data_in.data() + 2, size);

    m_ic_write_offset += size;

    if (m_ic_write_offset > m_ic_write_size)
    {
      m_ic_write_offset = 0;

      const u16 page = m_ic_write_buffer[5];
      const u16 count = m_ic_write_buffer[7];
      const u32 write_size = u32(count) * 8;
      const u32 write_offset = u32(page) * 8;

      //   if ((write_size + write_offset) > sizeof(m_ic_card_data) ||
      //       (10 + write_size) > sizeof(m_ic_write_buffer))
      //   {
      //     ERROR_LOG_FMT(SERIALINTERFACE_CARD,
      //                   "GC-AM: Command 25 (IC-CARD) Write Pages overflow:\n"
      //                   " - m_ic_card_data(offset={}, size={})\n"
      //                   " - m_ic_write_buffer(offset={}, size={})\n"
      //                   " - size={}, page={}, count={}\n",
      //                   write_offset, sizeof(m_ic_card_data), 10, sizeof(m_ic_write_buffer),
      //                   write_size, page, count);
      //     data_in = data_in_end;
      //     break;
      //   }
      memcpy(m_ic_card_data + write_offset, m_ic_write_buffer + 10, write_size);

      INFO_LOG_FMT(SERIALINTERFACE_CARD,
                   "GC-AM: Command 25 (IC-CARD) Write Pages:{} Count:{}({:x})", page, count, size);

      icco.command = WritePages;

      //   if (!validate_data_in_out(0, icco.pktlen + 2, "SerialA (IC-CARD)"))
      //     break;
      ICCardSendReply(&icco, data_out.data(), &data_offset);
    }
    // if (!validate_data_in_out(length, 0, "SerialA (IC-CARD)"))
    //   break;
    return length;
  }

  switch (ICCARDCommand(serial_command))
  {
  case ICCARDCommand::GetStatus:
    icco.status = m_ic_card_state;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Get Status:{:02x}",
                 m_ic_card_state);
    break;
  case ICCARDCommand::SetBaudrate:
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Set Baudrate");
    break;
  case ICCARDCommand::FieldOn:
    m_ic_card_state |= 0x10;
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Field On");
    break;
  case ICCARDCommand::InsertCheck:
    icco.status = m_ic_card_status;
    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Insert Check:{:02x}",
                 m_ic_card_status);
    break;
  case ICCARDCommand::AntiCollision:
    icco.extlen = 8;
    icco.length += icco.extlen;

    // Card ID
    icco.extdata[0] = 0x00;
    icco.extdata[1] = 0x00;
    icco.extdata[2] = 0x54;
    icco.extdata[3] = 0x4D;
    icco.extdata[4] = 0x50;
    icco.extdata[5] = 0x00;
    icco.extdata[6] = 0x00;
    icco.extdata[7] = 0x00;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Anti Collision");
    break;
  case ICCARDCommand::SelectCard:
    icco.extlen = 8;
    icco.length += icco.extlen;

    // Session
    icco.extdata[0] = 0x00;
    icco.extdata[1] = m_ic_card_session;
    icco.extdata[2] = 0x00;
    icco.extdata[3] = 0x00;
    icco.extdata[4] = 0x00;
    icco.extdata[5] = 0x00;
    icco.extdata[6] = 0x00;
    icco.extdata[7] = 0x00;

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Select Card:{}",
                 m_ic_card_session);
    break;
  case ICCARDCommand::ReadPage:
  case ICCARDCommand::ReadUseCount:
  {
    // if (!validate_data_in_out(8, 0, "SerialA (IC-CARD)"))
    //   break;
    const u16 page = Common::swap16(data_in.data() + 6) & 0xFF;  // 255 is max page

    icco.extlen = 8;
    icco.length += icco.extlen;

    memcpy(icco.extdata, m_ic_card_data + page * 8, 8);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Read Page:{}", page);
    break;
  }
  case ICCARDCommand::WritePage:
  {
    // if (!validate_data_in_out(10, 0, "SerialA (IC-CARD)"))
    //   break;
    const u16 page = Common::swap16(data_in.data() + 8) & 0xFF;  // 255 is max page

    // Write only one page
    if (page == 4)
    {
      icco.status = 0x80;
    }
    else
    {
      //   if (!validate_data_in_out(18, 0, "SerialA (IC-CARD)"))
      //     break;
      memcpy(m_ic_card_data + page * 8, data_in.data() + 10, 8);
    }

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (IC-CARD) Write Page:{}", page);
    break;
  }
  case ICCARDCommand::DecreaseUseCount:
  {
    // if (!validate_data_in_out(8, 0, "SerialA (IC-CARD)"))
    //   break;
    const u16 page = Common::swap16(data_in.data() + 6) & 0xFF;  // 255 is max page

    icco.extlen = 2;
    icco.length += icco.extlen;

    auto ic_card_data = Common::BitCastPtr<u16>(m_ic_card_data + 0x28);
    ic_card_data = ic_card_data - 1;

    // Counter
    icco.extdata[0] = m_ic_card_data[0x28];
    icco.extdata[1] = m_ic_card_data[0x29];

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Decrease Use Count:{}", page);
    break;
  }
  case ICCARDCommand::ReadPages:
  {
    // if (!validate_data_in_out(10, 0, "SerialA (IC-CARD)"))
    //   break;
    const u16 page = Common::swap16(data_in.data() + 6) & 0xFF;  // 255 is max page
    const u16 count = Common::swap16(data_in.data() + 8);

    const u32 offs = page * 8;
    u32 cnt = count * 8;

    // Limit read size to not overwrite the reply buffer
    const std::size_t reply_buffer_size = sizeof(icco.extdata) - 1;
    // if (data_offset > reply_buffer_size)
    // {
    //   ERROR_LOG_FMT(SERIALINTERFACE_CARD,
    //                 "GC-AM: Command 31 (IC-CARD) Read Pages overflow:"
    //                 " offset={} > buffer_size={}",
    //                 data_offset, reply_buffer_size);
    //   data_in = data_in_end;
    //   break;
    // }
    if (cnt > reply_buffer_size - data_offset)
    {
      cnt = 5 * 8;
    }

    icco.extlen = cnt;
    icco.length += icco.extlen;

    // if (offs + cnt > sizeof(icco.extdata))
    // {
    //   ERROR_LOG_FMT(SERIALINTERFACE_CARD,
    //                 "GC-AM: Command 31 (IC-CARD) Read Pages overflow:"
    //                 " offset={} + count={} > buffer_size={}",
    //                 offs, cnt, sizeof(icco.extdata));
    //   data_in = data_in_end;
    //   break;
    // }
    memcpy(icco.extdata, m_ic_card_data + offs, cnt);

    INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 31 (IC-CARD) Read Pages:{} Count:{}", page,
                 count);
    break;
  }
  case ICCARDCommand::WritePages:
  {
    // if (!validate_data_in_out(10, 0, "SerialA (IC-CARD)"))
    //   break;
    const u16 pksize = length;
    const u16 size = Common::swap16(data_in.data() + 2);
    const u16 page = Common::swap16(data_in.data() + 6) & 0xFF;  // 255 is max page
    const u16 count = Common::swap16(data_in.data() + 8);
    const u32 write_size = u32(count) * 8;
    const u32 write_offset = u32(page) * 8;

    // We got a complete packet
    if (pksize - 5 == size)
    {
      if (page == 4)  // Read Only Page, must return error
      {
        icco.status = 0x80;
      }
      else
      {
        if (write_size + write_offset > sizeof(m_ic_card_data))
        {
          ERROR_LOG_FMT(SERIALINTERFACE_CARD,
                        "GC-AM: Command 0x31 (IC-CARD) Data overflow: Pages:{} Count:{}({:x})",
                        page, count, size);
        }
        else
        {
          //   if (!validate_data_in_out(13 + write_size, 0, "SerialA (IC-CARD)"))
          //     break;
          memcpy(m_ic_card_data + write_offset, data_in.data() + 13, write_size);
        }
      }

      INFO_LOG_FMT(SERIALINTERFACE_CARD,
                   "GC-AM: Command 0x31 (IC-CARD) Write Pages:{} Count:{}({:x})", page, count,
                   size);
    }
    // VirtuaStriker 4 splits the writes over multiple packets
    else
    {
      //   if (!validate_data_in_out(2 + pksize, 0, "SerialA (IC-CARD)"))
      //     break;
      memcpy(m_ic_write_buffer, data_in.data() + 2, pksize);
      m_ic_write_offset += pksize;
      m_ic_write_size = size;
    }
    break;
  }
  default:
    // Handle Deck Reader commands
    // if (!validate_data_in_out(1, 0, "SerialA (DECK READER)"))
    //   break;
    serial_command = data_in[0];
    icco.command = serial_command;
    icco.flag = 0;
    switch (CDReaderCommand(serial_command))
    {
    case CDReaderCommand::ProgramVersion:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Program Version");

      icco.extlen = (u32)strlen(s_cdr_program_version);
      icco.length += icco.extlen;

      memcpy(icco.extdata, s_cdr_program_version, icco.extlen);
      break;
    case CDReaderCommand::BootVersion:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Boot Version");

      icco.extlen = (u32)strlen(s_cdr_boot_version);
      icco.length += icco.extlen;

      memcpy(icco.extdata, s_cdr_boot_version, icco.extlen);
      break;
    case CDReaderCommand::ShutterGet:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Shutter Get");

      icco.extlen = 4;
      icco.length += icco.extlen;

      icco.extdata[0] = 0;
      icco.extdata[1] = 0;
      icco.extdata[2] = 0;
      icco.extdata[3] = 0;
      break;
    case CDReaderCommand::CameraCheck:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Camera Check");

      icco.extlen = 6;
      icco.length += icco.extlen;

      icco.extdata[0] = 0x23;
      icco.extdata[1] = 0x28;
      icco.extdata[2] = 0x45;
      icco.extdata[3] = 0x29;
      icco.extdata[4] = 0x45;
      icco.extdata[5] = 0x29;
      break;
    case CDReaderCommand::ProgramChecksum:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Program Checksum");

      icco.extlen = 4;
      icco.length += icco.extlen;

      icco.extdata[0] = 0x23;
      icco.extdata[1] = 0x28;
      icco.extdata[2] = 0x45;
      icco.extdata[3] = 0x29;
      break;
    case CDReaderCommand::BootChecksum:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Boot Checksum");

      icco.extlen = 4;
      icco.length += icco.extlen;

      icco.extdata[0] = 0x23;
      icco.extdata[1] = 0x28;
      icco.extdata[2] = 0x45;
      icco.extdata[3] = 0x29;
      break;
    case CDReaderCommand::SelfTest:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Self Test");
      icco.flag = 0x00;
      break;
    case CDReaderCommand::SensLock:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Sens Lock");
      icco.flag = 0x01;
      break;
    case CDReaderCommand::SensCard:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Sens Card");
      break;
    case CDReaderCommand::ShutterCard:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Shutter Card");
      break;
    case CDReaderCommand::ReadCard:
      INFO_LOG_FMT(SERIALINTERFACE_CARD, "GC-AM: Command 0x31 (DECK READER) Read Card");

      icco.fixed = 0xAA;
      icco.flag = 0xAA;
      icco.extlen = sizeof(s_cdr_card_data);
      icco.length = 0x72;
      icco.status = Common::swap16(icco.extlen);

      memcpy(icco.extdata, s_cdr_card_data, sizeof(s_cdr_card_data));

      break;
    default:
      //   if (!validate_data_in_out(14, 0, "SerialA (DECK READER)"))
      //     break;
      WARN_LOG_FMT(SERIALINTERFACE_CARD,
                   "GC-AM: Command 0x31 (IC-Card) {:02x} {:02x} {:02x} {:02x} {:02x} "
                   "{:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x}",
                   data_in[2], data_in[3], data_in[4], data_in[5], data_in[6], data_in[7],
                   data_in[8], data_in[9], data_in[10], data_in[11], data_in[12], data_in[13]);
      break;
    }
    break;
  }

  //   if (!validate_data_in_out(0, icco.pktlen + 2, "SerialA (IC-CARD)"))
  //     break;
  ICCardSendReply(&icco, data_out.data(), &data_offset);

  return data_offset;
}

void ICCardReader::ICCardSendReply(ICCommand* iccommand, u8* buffer, u32* length)
{
  iccommand->status = Common::swap16(iccommand->status);

  const auto iccommand_data = reinterpret_cast<const u8*>(iccommand);
  //   const u8 crc = CheckSumXOR(iccommand_data + 2, iccommand->pktlen - 1);

  //   for (u32 i = 0; i <= iccommand->pktlen; ++i)
  //   {
  //     buffer[(*length)++] = iccommand_data[i];
  //   }

  //   buffer[(*length)++] = crc;
}

}  // namespace TriforcePeripheral
