// Copyright 2016 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#if defined(__linux__) && HAVE_BLUEZ
#include <bluetooth/bluetooth.h>

#include "Core/HW/WiimoteReal/WiimoteReal.h"

namespace WiimoteReal
{
class WiimoteLinux final : public Wiimote
{
public:
  WiimoteLinux(bdaddr_t bdaddr);
  ~WiimoteLinux() override;
  std::string GetId() const override
  {
    char bdaddr_str[18] = {};
    ba2str(&m_bluetooth_address, bdaddr_str);
    return bdaddr_str;
  }

  bool Is200HzModeEstablished() const override;
  void Set200HzModeEstablished(bool);

  bool ConnectInternal() override;

protected:
  void DisconnectInternal() override;
  bool IsConnected() const override;
  void IOWakeup() override;
  int IORead(u8* buf) override;
  int IOWrite(u8 const* buf, size_t len) override;

private:
  const bdaddr_t m_bluetooth_address;
  int m_cmd_sock = -1;
  int m_int_sock = -1;
  int m_wakeup_pipe_w = -1;
  int m_wakeup_pipe_r = -1;
  bool m_is_200hz_established = false;
};

class WiimoteScannerLinux final : public WiimoteScannerBackend
{
public:
  WiimoteScannerLinux();
  ~WiimoteScannerLinux() override;
  bool IsReady() const override;
  void FindWiimotes(std::vector<Wiimote*>&, Wiimote*&) override;
  void Update() override {}                // not needed on Linux
  void RequestStopSearching() override {}  // not needed on Linux
private:
  int m_device_id;
  int m_device_sock;

  void AddAutoConnectAddresses(std::vector<Wiimote*>&);
};
}  // namespace WiimoteReal

#else
#include "Core/HW/WiimoteReal/IODummy.h"
namespace WiimoteReal
{
using WiimoteScannerLinux = WiimoteScannerDummy;
}
#endif
