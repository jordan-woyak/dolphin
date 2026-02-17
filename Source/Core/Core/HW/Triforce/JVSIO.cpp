// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/Triforce/JVSIO.h"

#include "Common/MsgHandler.h"

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
    PanicAlertFmt("JVSIOMessage overrun!");
    return;
  }

  while (len--)
  {
    const u8 c = *dst++;
    if (!sync && ((c == 0xE0) || (c == 0xD0)))
    {
      if (m_pointer + 2 > sizeof(m_message))
      {
        PanicAlertFmt("JVSIOMessage overrun!");
        break;
      }
      m_message[m_pointer++] = 0xD0;
      m_message[m_pointer++] = c - 1;
    }
    else
    {
      if (m_pointer >= sizeof(m_message))
      {
        PanicAlertFmt("JVSIOMessage overrun!");
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
    PanicAlertFmt("JVSIOMessage: Not enough space for checksum!");
  }
}

namespace TriforcePeripheral
{

}  // namespace TriforcePeripheral
