// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/JVSIO.h"

#include <numeric>

#include "Common/Logging/Log.h"

// TODO: default handler
//   ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Unhandled: node={}, command={:02x}",
//                 node, jvsio_command);

void JVSIOMessage::Start(int node)
{
  m_last_start = m_pointer;
  const u8 header[3] = {0xE0, (u8)node, 0};
  m_checksum = 0;
  AddData(header, 3, 1);
}

void JVSIOMessage::AddData(const u8* dst, std::size_t len, int sync = 0)
{
  if (m_pointer + len >= sizeof(m_message))
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Overrun");
    return;
  }

  while (len--)
  {
    const u8 c = *dst++;
    if (!sync && ((c == 0xE0) || (c == 0xD0)))
    {
      if (m_pointer + 2 > sizeof(m_message))
      {
        ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Overrun");
        break;
      }
      m_message[m_pointer++] = 0xD0;
      m_message[m_pointer++] = c - 1;
    }
    else
    {
      if (m_pointer >= sizeof(m_message))
      {
        ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Overrun");
        break;
      }
      m_message[m_pointer++] = c;
    }

    if (!sync)
      m_checksum += c;
    sync = 0;
  }
}

void JVSIOMessage::AddData(const void* data, std::size_t len)
{
  AddData(static_cast<const u8*>(data), len);
}

void JVSIOMessage::AddData(const char* data)
{
  AddData(data, strlen(data));
}

void JVSIOMessage::AddData(u32 n)
{
  const u8 cs = n;
  AddData(&cs, 1);
}

void JVSIOMessage::End()
{
  const u32 len = m_pointer - m_last_start;
  if (m_last_start + 2 < sizeof(m_message) && len >= 3)
  {
    m_message[m_last_start + 2] = len - 2;  // assuming len <0xD0
    AddData(m_checksum + len - 2);
  }
  else
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Overrun");
  }
}

namespace TriforcePeripheral
{

void JVSClient::Process(std::span<const u8> marked_data)
{
  std::array<u8, 3 + 256> buffer;
  auto data = UnMarkData(marked_data, buffer);

  if (data.size() < 4)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Bad Size");
    return;
  }

  if (data[0] != JVSIO_SYNC)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Expected JVSIO_SYNC");
  }

  const u8 destination_node = data[1];
  const u8 byte_count = data[2];

  if (data.size() - 3 < byte_count)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Bad size");
    return;
  }

  const auto range_to_checksum = data.subspan(1, byte_count - 1);
  const u8 proper_checksum =
      std::accumulate(range_to_checksum.begin(), range_to_checksum.end(), u8(0));

  if (proper_checksum != data[byte_count + 3])
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Bad checksum");
    return;
  }

  if (destination_node == m_client_address || destination_node == JVSIO_BROADCAST_ADDRESS)
  {
    ProcessFrame(marked_data.subspan(3, byte_count));

    // TODO: also send to daisy chained clients
  }
}

std::span<u8> JVSClient::UnMarkData(std::span<const u8> input, std::span<u8> output)
{
  auto out = output.begin();
  u8 mark_state = 0x00;
  for (const u8 byte_value : input)
  {
    if (byte_value == JVSIO_MARK)
    {
      mark_state = 0x01;
      continue;
    }

    *out = byte_value + mark_state;
    mark_state = 0x00;

    if (++out == output.end())
      return {};
  }

  return {output.begin(), out};
}

void JVSClient::ProcessFrame(std::span<const u8> frame)
{
}

void JVSClient::SetJVSIOHandler(JVSIOCommand, JVSIOMessageHandler)
{
}

}  // namespace TriforcePeripheral
