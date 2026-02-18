// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/JVSIO.h"

#include <numeric>

#include "Common/Logging/Log.h"

namespace TriforcePeripheral
{

u32 JVSIOBoard::ProcessJVSIO(std::span<const u8> input)
{
  std::array<u8, 3 + 256> buffer;
  const u32 header_escaped_size = UnescapeData(input, std::span{buffer}.first(3));

  if (header_escaped_size == 0)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Bad Size");
    return 0;
  }

  if (input[0] != JVSIO_SYNC)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Expected JVSIO_SYNC");
  }

  const u8 destination_node = buffer[1];
  const u8 payload_size = buffer[2];

  const u32 payload_escaped_size =
      UnescapeData(input.subspan(header_escaped_size), std::span{buffer}.subspan(3, payload_size));

  if (payload_escaped_size == 0)
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Bad Size");
    return 0;
  }

  const auto range_to_checksum = std::span{buffer}.subspan(1, payload_size - 1);
  const u8 proper_checksum =
      std::accumulate(range_to_checksum.begin(), range_to_checksum.end(), u8(0));

  if (proper_checksum != buffer[payload_size + 3])
  {
    ERROR_LOG_FMT(SERIALINTERFACE_JVSIO, "Bad checksum");
    return 0;
  }

  if (destination_node == JVSIO_BROADCAST_ADDRESS)
  {
    if (!ProcessBroadcastFrame(range_to_checksum.subspan(2)))
      return 0;
  }

  if (destination_node == m_client_address)
  {
    if (!ProcessFrame(range_to_checksum.subspan(2)))
      return 0;
  }

  return header_escaped_size + payload_escaped_size;
}

u32 JVSIOBoard::UnescapeData(std::span<const u8> input, std::span<u8> output)
{
  auto out = output.begin();
  u8 mark_state = 0x00;
  u32 read_count = 0;
  for (const u8 byte_value : input)
  {
    ++read_count;

    if (byte_value == JVSIO_MARK)
    {
      mark_state = 0x01;
      continue;
    }

    *out = byte_value + mark_state;
    mark_state = 0x00;

    if (++out == output.end())
      return read_count;
  }

  return 0;
}

bool JVSIOBoard::ProcessBroadcastFrame(std::span<const u8> frame)
{
  FrameReader request{frame.data(), frame.data() + frame.size()};

  if (!request.HasCount(1))
    return true;

  const auto cmd = JVSIOCommand(request.ReadByte());
  switch (cmd)
  {
  case JVSIOCommand::Reset:
  {
    if (!request.HasCount(1))
      return false;

    if (request.ReadByte() == 0xd9)
    {
      NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "Command 0xF0, Reset");
    }

    m_last_response.clear();
    break;
  }
  case JVSIOCommand::SetAddress:
  {
    if (!request.HasCount(1))
      return false;

    const u8 node = request.ReadByte();

    if (m_client_address == 0)
    {
      m_client_address = node;
      NOTICE_LOG_FMT(SERIALINTERFACE_JVSIO, "JVS-IO: Command 0xF1, SetAddress: node={}", node);
    }

    m_last_response.resize(3 + 256);
    break;
  }
  default:
    return false;
  }

  // TODO: Check for excess bytes?
  return true;
}

bool JVSIOBoard::ProcessFrame(std::span<const u8> frame)
{
  m_last_response.resize(3 + 256);

  JVSIOFrameContext ctx{
      .request{frame.data(), frame.data() + frame.size()},
  };

  ctx.message.m_data = m_last_response.data();
  ctx.message.m_data_end = m_last_response.data() + m_last_response.size();

  ctx.message.StartFrame(m_client_address);

  while (ctx.request.HasCount(1))
  {
    ctx.message.StartReport();

    const auto cmd = JVSIOCommand(ctx.request.ReadByte());
    const auto report_code = HandleJVSIORequest(cmd, &ctx);

    ctx.message.SetLastReportCode(report_code);

    if (report_code != JVSIOReportCode::Normal)
      break;
  }

  m_last_response.resize(ctx.message.EndFrame());
  return true;
}

}  // namespace TriforcePeripheral
