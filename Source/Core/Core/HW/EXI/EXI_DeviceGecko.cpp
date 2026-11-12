// Copyright 2011 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/EXI/EXI_DeviceGecko.h"

#include <functional>
#include <stop_token>
#include <thread>

#include <asio.hpp>
#include <fmt/format.h>

#include "Common/AsioUtil.h"  // IWYU pragma: keep
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/SPSCQueue.h"
#include "Common/Thread.h"
#include "Core/Core.h"

namespace
{
using asio::ip::tcp;

class GeckoSockServer
{
public:
  // Multiple CEXIGecko objects share one server.
  static std::shared_ptr<GeckoSockServer> GetInstance()
  {
    auto result = s_instance.lock();
    if (!result)
    {
      result = std::shared_ptr<GeckoSockServer>(new GeckoSockServer);
      s_instance = result;
    }
    return result;
  }

  std::shared_ptr<tcp::socket> GetAvailableSock();

private:
  static std::weak_ptr<GeckoSockServer> s_instance;

  GeckoSockServer() = default;

  void SocketThreadFunc(std::stop_token stoken);

  Common::SPSCQueue<tcp::socket> m_waiting_socks;

  std::jthread m_thread{std::bind_front(&GeckoSockServer::SocketThreadFunc, this)};
};

std::weak_ptr<GeckoSockServer> GeckoSockServer::s_instance;

void GeckoSockServer::SocketThreadFunc(std::stop_token stoken)
{
  Common::SetCurrentThreadName("USBGecko IO");

  asio::io_context io;

  tcp::acceptor server{io};

  constexpr u16 initial_port = 0xd6ec;  // "dolphin gecko"
  constexpr u16 port_retries = 10;
  constexpr u16 last_port = initial_port + port_retries;

  for (u16 server_port = initial_port; server_port <= last_port; ++server_port)
  {
    tcp::acceptor acceptor{io};

    std::error_code ec;
    if (!acceptor.open(tcp::v4(), ec) &&
        !acceptor.bind(tcp::endpoint{tcp::v4(), server_port}, ec) &&
        !acceptor.listen(tcp::socket::max_listen_connections, ec))
    {
      Core::DisplayMessage(fmt::format("USBGecko: Listening on TCP port {}", server_port), 5000);
      server = std::move(acceptor);
      break;
    }

    WARN_LOG_FMT(EXPANSIONINTERFACE, "USBGecko: Unable to bind TCP port {}: {}", server_port,
                 ec.message());
  }

  if (!server.is_open())
  {
    const std::string message =
        fmt::format("USBGecko: Failed to bind to any port in range {}-{}", initial_port, last_port);
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "{}", message);
    Core::DisplayMessage(message, 5000);
    return;
  }

  const auto enqueue_accepted_sockets =
      [](tcp::acceptor& acceptor, Common::SPSCQueue<tcp::socket>& queue) -> asio::awaitable<void> {
    while (true)
    {
      auto [ec, socket] = co_await acceptor.async_accept(asio::as_tuple);
      if (ec)
        break;
      queue.Emplace(std::move(socket));
    }
  };

  asio::co_spawn(io, enqueue_accepted_sockets(server, m_waiting_socks), asio::detached);

  std::stop_callback cb{stoken, [&] { io.stop(); }};

  // Keep the io_context running even if our acceptor tasks were to error out somehow.
  // Otherwise outside initiated async tasks would never finish.
  const auto work_guard = asio::make_work_guard(io);

  io.run();
}

std::shared_ptr<tcp::socket> GeckoSockServer::GetAvailableSock()
{
  std::shared_ptr<tcp::socket> result;
  if (!m_waiting_socks.Empty())
  {
    result = std::make_shared<tcp::socket>(std::move(m_waiting_socks.Front()));
    m_waiting_socks.Pop();
  }
  return result;
}

class CEXIGecko : public ExpansionInterface::IEXIDevice
{
public:
  using IEXIDevice::IEXIDevice;
  bool IsPresent() const override { return true; }
  void ImmReadWrite(u32& _uData, u32 _uSize) override;

  ~CEXIGecko();

private:
  static constexpr auto FIFO_SIZE = 1024uz;

  bool Connect();

  enum
  {
    CMD_LED_OFF = 0x7,
    CMD_LED_ON = 0x8,
    CMD_INIT = 0x9,
    CMD_RECV = 0xa,
    CMD_SEND = 0xb,
    CMD_CHK_TX = 0xc,
    CMD_CHK_RX = 0xd,
  };

  static constexpr u32 IDENT = 0x04700000;

  std::shared_ptr<Common::SPSCQueue<u8>> m_tx_queue{std::make_shared<Common::SPSCQueue<u8>>()};
  std::shared_ptr<Common::SPSCQueue<u8>> m_rx_queue{std::make_shared<Common::SPSCQueue<u8>>()};

  std::weak_ptr<tcp::socket> m_socket;

  std::shared_ptr<GeckoSockServer> m_server{GeckoSockServer::GetInstance()};
};

CEXIGecko::~CEXIGecko()
{
  if (auto socket = m_socket.lock())
  {
    // If we have an open socket, defer its closure.
    asio::post(socket->get_executor(), [socket] {
      std::error_code ec;
      (void)socket->close(ec);
    });
  }
}

bool CEXIGecko::Connect()
{
  if (m_socket.lock())
    return true;

  const auto socket = m_server->GetAvailableSock();
  if (!socket)
    return false;

  m_socket = socket;

  INFO_LOG_FMT(EXPANSIONINTERFACE, "USBGecko: New client connected.");

  const auto send_bytes =
      [](std::shared_ptr<tcp::socket> socket,
         std::shared_ptr<Common::SPSCQueue<u8>> tx_queue) -> asio::awaitable<void> {
    std::array<u8, FIFO_SIZE> buffer{};
    while (true)
    {
      const std::size_t send_count = std::min(buffer.size(), tx_queue->Size());
      if (send_count == 0)
      {
        // TX FIFO is empty. Wait for some data.
        co_await asio::steady_timer{socket->get_executor(), std::chrono::milliseconds{1}}
            .async_wait(asio::as_tuple);
        continue;
      }

      for (auto i = 0uz; i != send_count; ++i)
        tx_queue->Pop(buffer[i]);

      auto [ec, count] =
          co_await socket->async_send(asio::buffer(buffer.data(), send_count), asio::as_tuple);
      if (ec)
        break;
    }
  };

  const auto receive_bytes =
      [](std::shared_ptr<tcp::socket> socket,
         std::shared_ptr<Common::SPSCQueue<u8>> rx_queue) -> asio::awaitable<void> {
    std::array<u8, FIFO_SIZE> buffer{};
    while (true)
    {
      const std::size_t recv_count = FIFO_SIZE - rx_queue->Size();
      if (recv_count == 0)
      {
        DEBUG_LOG_FMT(EXPANSIONINTERFACE, "USBGecko: RX FIFO full. Sleeping.");
        co_await asio::steady_timer{socket->get_executor(), std::chrono::milliseconds{1}}
            .async_wait(asio::as_tuple);
        continue;
      }

      auto [ec, count] =
          co_await socket->async_read_some(asio::buffer(buffer.data(), recv_count), asio::as_tuple);
      if (ec)
        break;

      for (u8 b : std::span{buffer}.first(count))
        rx_queue->Emplace(b);
    }
  };

  const auto& io = socket->get_executor();

  // Note: These hold shared_ptr to the socket which they let go out of scope on r/w failure.
  // We detect disconnection via trying to lock our weak_ptr.
  asio::co_spawn(io, send_bytes(socket, m_tx_queue), asio::detached);
  asio::co_spawn(io, receive_bytes(socket, m_rx_queue), asio::detached);

  return true;
}

void CEXIGecko::ImmReadWrite(u32& _uData, u32 _uSize)
{
  // We don't really care about _uSize
  (void)_uSize;

  Connect();

  switch (_uData >> 28)
  {
  case CMD_LED_OFF:
    Core::DisplayMessage("USBGecko: No LEDs for you!", 3000);
    break;
  case CMD_LED_ON:
    Core::DisplayMessage("USBGecko: A piercing blue light is now shining in your general direction",
                         3000);
    break;

  case CMD_INIT:
    _uData = IDENT;
    break;

  // PC -> Gecko
  // |= 0x08000000 if successful
  case CMD_RECV:
  {
    u8 byte{};
    if (m_rx_queue->Pop(byte))
      _uData = 0x08000000 | (byte << 16u);
    break;
  }

  // Gecko -> PC
  // |= 0x04000000 if successful
  case CMD_SEND:
  {
    if (m_tx_queue->Size() < FIFO_SIZE)
    {
      const u8 byte = _uData >> 20;
      m_tx_queue->Emplace(byte);
      _uData |= 0x04000000;
    }
    break;
  }

  // Check if ok for Gecko -> PC, or FIFO full
  // |= 0x04000000 if FIFO is not full
  case CMD_CHK_TX:
  {
    if (m_tx_queue->Size() < FIFO_SIZE)
      _uData |= 0x04000000;
    break;
  }

  // Check if data in FIFO for PC -> Gecko, or FIFO empty
  // |= 0x04000000 if data in recv FIFO
  case CMD_CHK_RX:
  {
    if (!m_rx_queue->Empty())
      _uData |= 0x04000000;
    break;
  }

  default:
    ERROR_LOG_FMT(EXPANSIONINTERFACE, "Unknown USBGecko command {:x}", _uData);
    break;
  }
}
}  // namespace

namespace ExpansionInterface
{
std::unique_ptr<IEXIDevice> CreateEXIGecko(Core::System& system)
{
  return std::make_unique<CEXIGecko>(system);
}
}  // namespace ExpansionInterface
