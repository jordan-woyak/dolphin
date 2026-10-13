// Copyright 2017 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef USE_UPNP

#include "Common/UPnP.h"

#include "Common/Logging/Log.h"
#include "Common/SmallVector.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <miniupnpc.h>
#include <miniwget.h>
#include <string>
#include <thread>
#include <upnpcommands.h>
#include <upnperrors.h>

namespace
{
struct PortMapping
{
  u16 port;
  const char* protocol;
};

Common::SmallVector<PortMapping, 2> g_mapped_ports;
}  // namespace

static UPNPUrls s_urls;
static IGDdatas s_data;
static std::array<char, 20> s_our_ip;
static std::thread s_thread;

// called from ---UPnP--- thread
// discovers the IGD
static bool InitUPnP()
{
  static bool s_inited = false;
  static bool s_error = false;

  // Don't init if already inited
  if (s_inited)
    return true;

  // Don't init if it failed before
  if (s_error)
    return false;

  s_urls = {};
  s_data = {};

  // Find all UPnP devices
  int upnperror = 0;
  std::unique_ptr<UPNPDev, decltype(&freeUPNPDevlist)> devlist(nullptr, freeUPNPDevlist);
#if MINIUPNPC_API_VERSION >= 14
  devlist.reset(upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &upnperror));
#else
  devlist.reset(upnpDiscover(2000, nullptr, nullptr, 0, 0, &upnperror));
#endif
  if (!devlist)
  {
    if (upnperror == UPNPDISCOVER_SUCCESS)
    {
      WARN_LOG_FMT(NETPLAY, "No UPnP devices could be found.");
    }
    else
    {
      WARN_LOG_FMT(NETPLAY, "An error occurred trying to discover UPnP devices: {}",
                   strupnperror(upnperror));
    }

    s_error = true;

    return false;
  }

  // Look for the IGD
  bool found_valid_igd = false;
  for (UPNPDev* dev = devlist.get(); dev; dev = dev->pNext)
  {
    if (!std::strstr(dev->st, "InternetGatewayDevice"))
      continue;

    int desc_xml_size = 0;
    std::unique_ptr<char, decltype(&std::free)> desc_xml(nullptr, std::free);
    int statusCode = 200;
#if MINIUPNPC_API_VERSION >= 16
    desc_xml.reset(
        static_cast<char*>(miniwget_getaddr(dev->descURL, &desc_xml_size, s_our_ip.data(),
                                            static_cast<int>(s_our_ip.size()), 0, &statusCode)));
#else
    desc_xml.reset(static_cast<char*>(miniwget_getaddr(
        dev->descURL, &desc_xml_size, s_our_ip.data(), static_cast<int>(s_our_ip.size()), 0)));
#endif
    if (desc_xml && statusCode == 200)
    {
      parserootdesc(desc_xml.get(), desc_xml_size, &s_data);
      GetUPNPUrls(&s_urls, &s_data, dev->descURL, 0);

      found_valid_igd = true;
      NOTICE_LOG_FMT(NETPLAY, "Got info from IGD at {}.", dev->descURL);
      break;
    }
    else
    {
      WARN_LOG_FMT(NETPLAY, "Error getting info from IGD at {}.", dev->descURL);
    }
  }

  if (!found_valid_igd)
    WARN_LOG_FMT(NETPLAY, "Could not find a valid IGD in the discovered UPnP devices.");

  s_inited = true;

  return true;
}

// called from ---UPnP--- thread
// Attempt to stop portforwarding.
// --
// NOTE: It is important that this happens! A few very crappy routers
// apparently do not delete UPnP mappings on their own, so if you leave them
// hanging, the NVRAM will fill with portmappings, and eventually all UPnP
// requests will fail silently, with the only recourse being a factory reset.
// --
static bool UnmapPort(const u16 port, const char* protocol)
{
  std::string port_str = std::to_string(port);
  UPNP_DeletePortMapping(s_urls.controlURL, s_data.first.servicetype, port_str.c_str(), protocol,
                         nullptr);

  return true;
}

// called from ---UPnP--- thread
// Attempt to portforward!
static bool MapPort(const char* addr, const u16 port, const char* protocol)
{
  std::string port_str = std::to_string(port);
  int result = UPNP_AddPortMapping(
      s_urls.controlURL, s_data.first.servicetype, port_str.c_str(), port_str.c_str(), addr,
      fmt::format("dolphin-emu {} on {}", protocol, addr).c_str(), protocol, nullptr, nullptr);

  return result == 0;
}

static void UnmapAllMappedPorts()
{
  for (auto&& mapping : g_mapped_ports)
    UnmapPort(mapping.port, mapping.protocol);
  g_mapped_ports.clear();
}

// UPnP thread: try to map a port
static void MapPortThread(const u16 port)
{
  UnmapAllMappedPorts();

  if (!InitUPnP())
    return;

  for (const char* protocol : {"UDP", "TCP"})
  {
    if (MapPort(s_our_ip.data(), port, protocol))
    {
      NOTICE_LOG_FMT(NETPLAY, "Successfully mapped {} port {} to {}.", protocol, port,
                     s_our_ip.data());
      g_mapped_ports.emplace_back(port, protocol);
    }
    else
    {
      WARN_LOG_FMT(NETPLAY, "Failed to map {} port {} to {}.", protocol, port, s_our_ip.data());
    }
  }
}

void Common::UPnP::TryPortmapping(u16 port)
{
  if (s_thread.joinable())
    s_thread.join();
  s_thread = std::thread(&MapPortThread, port);
}

void Common::UPnP::StopPortmapping()
{
  if (s_thread.joinable())
    s_thread.join();
  s_thread = std::thread(&UnmapAllMappedPorts);
  s_thread.join();
}

#endif
