#include "socks5_http_socket_base.hpp"
#include "number_stream.hpp"
#include "socks5_protocol.hpp"

#include <boost/asio/strand.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

namespace woody_server {
socks5_http_socket_base_t::socks5_http_socket_base_t(
    bool &stopped, net::io_context &ioContext, proxy_base_t &proxyProvider,
    number_stream_t &numbers, int const scansPerIP)
    : socket_interface_t(ioContext, proxyProvider, numbers, stopped,
                         scansPerIP),
      m_tcpStream(std::in_place, net::make_strand(ioContext)) {}
void socks5_http_socket_base_t::onConnected(beast::error_code const ec) {
  if (ec)
    return reconnect();
  performSocks5Handshake();
}

void socks5_http_socket_base_t::performSocks5Handshake() {
  m_handshakeBuffer.clear();

  bool const using_auth = !m_currentProxy->getUsername().empty();
  std::size_t const bytes_to_write = using_auth ? 4 : 3;

  m_handshakeBuffer.reserve(bytes_to_write);
  m_handshakeBuffer.push_back(SOCKS_VERSION_5); // version
  if (using_auth) {
    m_handshakeBuffer.push_back(2);                // using 2 methods
    m_handshakeBuffer.push_back(SOCKS5_AUTH_NONE); // using no auth
    m_handshakeBuffer.push_back(SOCKS5_AUTH);      // using auth
  } else {
    m_handshakeBuffer.push_back(1);                // using 1 method
    m_handshakeBuffer.push_back(SOCKS5_AUTH_NONE); // no auth
  }

  m_tcpStream->expires_after(std::chrono::milliseconds(5'000));
  m_tcpStream->async_write_some(
      net::const_buffer(
          reinterpret_cast<char const *>(m_handshakeBuffer.data()),
          m_handshakeBuffer.size()),
      [this](beast::error_code const ec, std::size_t const size) {
        onFirstHandshakeInitiated(ec, size);
      });
}

void socks5_http_socket_base_t::onFirstHandshakeInitiated(
    beast::error_code const ec, std::size_t const) {
  if (ec) { // could be timeout
    // spdlog::error("first handshake failed: {}", ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  readFirstHandshakeResult();
}

void socks5_http_socket_base_t::readFirstHandshakeResult() {
  m_replyBuffer.clear();
  m_replyBuffer.resize(2);
  m_tcpStream->expires_after(std::chrono::seconds(10));
  m_tcpStream->async_read_some(
      net::mutable_buffer(m_replyBuffer.data(), m_replyBuffer.size()),
      [this](beast::error_code ec, size_t const sz) {
        onFirstHandshakeResponseReceived(ec, sz);
      });
}

void socks5_http_socket_base_t::onFirstHandshakeResponseReceived(
    beast::error_code const ec, [[maybe_unused]] size_t const sz) {
  if (ec) {
    // spdlog::error(ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }

  char const *p1 = reinterpret_cast<char const *>(m_replyBuffer.data());
  BOOST_ASSERT(p1 != nullptr);

  auto const version = p1[0];
  auto const method = p1[1];
  if (version != SOCKS_VERSION_5) {
    // spdlog::error("version used on SOCKS server is not v5 but `{}`",
    // version);
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }

  if (method == SOCKS5_AUTH) {
    if (m_currentProxy->getUsername().empty()) {
      // spdlog::error("Proxy demanded username/password, but we don't have
      // it");
      currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
      return chooseNextProxy();
    }
    m_handshakeBuffer.clear();

    auto &username = m_currentProxy->getUsername();
    auto &password = m_currentProxy->getPassword();
    auto const username_length = static_cast<uint8_t>(username.size());
    auto const password_length = static_cast<uint8_t>(password.size());
    std::size_t const buffers_to_write = username_length + password_length + 3;

    m_handshakeBuffer.reserve(buffers_to_write);

    m_handshakeBuffer.push_back(0x01); // auth version
    m_handshakeBuffer.push_back(username_length);
    std::copy(username.cbegin(), username.cend(),
              std::back_inserter(m_handshakeBuffer));
    m_handshakeBuffer.push_back(password_length);
    std::copy(password.cbegin(), password.cend(),
              std::back_inserter(m_handshakeBuffer));
    return m_tcpStream->async_write_some(
        net::const_buffer(m_handshakeBuffer.data(), m_handshakeBuffer.size()),
        [this](beast::error_code const ec, std::size_t const) {
          if (ec) {
            // spdlog::error("Unable to write to stream: {}", ec.message());
            currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
            return chooseNextProxy();
          }
          m_replyBuffer.clear();
          m_replyBuffer.resize(2);
          m_tcpStream->expires_after(std::chrono::seconds(10));
          m_tcpStream->async_read_some(
              net::mutable_buffer(m_replyBuffer.data(), m_replyBuffer.size()),
              [this](beast::error_code ec, std::size_t const sz) {
                onAuthResponseReceived(ec, sz);
              });
        });
  }
  if (method == SOCKS5_AUTH_NONE)
    return performSock5SecondHandshake();
  // spdlog::error("unsupported socks version");
  currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
  chooseNextProxy();
}

void socks5_http_socket_base_t::onAuthResponseReceived(
    beast::error_code const ec, [[maybe_unused]] size_t const sz) {
  if (ec) {
    // spdlog::error("[auth_response_received] {}", ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  char const *p1 = reinterpret_cast<char const *>(m_replyBuffer.data());
  BOOST_ASSERT(p1 != nullptr);
  auto const version = p1[0];
  auto const status = p1[1];
  if (version != 0x01) {
    // spdlog::error("unsupported authentication type");
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  if (status != 0x00) {
    // spdlog::error("authentication error");
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  performSock5SecondHandshake();
}

void socks5_http_socket_base_t::performSock5SecondHandshake() {
  m_handshakeBuffer.clear();

  std::string const host_name = hostname();
  std::size_t const bytes_to_write = host_name.size() + 7;
  m_handshakeBuffer.reserve(std::max<std::size_t>(bytes_to_write, 22));

  m_handshakeBuffer.push_back(SOCKS_VERSION_5);        // socks version
  m_handshakeBuffer.push_back(SOCKS_CMD_CONNECT);      // CONNECT command
  m_handshakeBuffer.push_back(0x00);                   // RESERVED
  m_handshakeBuffer.push_back(SOCKS5_ATYP_DOMAINNAME); // use domain name
  m_handshakeBuffer.push_back(static_cast<uint8_t>(host_name.size()));
  std::copy(host_name.cbegin(), host_name.cend(),
            std::back_inserter(m_handshakeBuffer));
  m_handshakeBuffer.push_back(80 >> 8);
  m_handshakeBuffer.push_back(80 & 0xFF);

  m_tcpStream->async_write_some(
      net::const_buffer(m_handshakeBuffer.data(), m_handshakeBuffer.size()),
      [this](beast::error_code ec, std::size_t const) {
        if (ec) {
          // spdlog::error("[second_socks_write] {}", ec.message());
          currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
          return chooseNextProxy();
        }
        return readSocks5ServerResponse();
      });
}

void socks5_http_socket_base_t::readSocks5ServerResponse() {
  m_replyBuffer.clear();
  m_replyBuffer.resize(10);
  m_tcpStream->expires_after(std::chrono::seconds(10));
  m_tcpStream->async_read_some(
      net::mutable_buffer(m_replyBuffer.data(), m_replyBuffer.size()),
      [this](beast::error_code ec, std::size_t const sz) {
        onHandshakeResponseReceived(ec, sz);
      });
}

void socks5_http_socket_base_t::onHandshakeResponseReceived(
    beast::error_code ec, std::size_t const sz) {
  if (ec) {
    // spdlog::error("[second_handshake_read_error] {}", ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  char const *p1 = reinterpret_cast<char const *>(m_replyBuffer.data());
  BOOST_ASSERT(p1 != nullptr);

  auto const version = p1[0];
  auto const reserved_byte = p1[2];
  int const a_type = p1[3];

  if (version != SOCKS_VERSION_5) {
    // spdlog::error("version supported is not sk5");
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }

  if (a_type != SOCKS5_ATYP_IPV4 && a_type != SOCKS5_ATYP_DOMAINNAME &&
      a_type != SOCKS5_ATYP_IPV6) {
    // spdlog::error("SOCKS5 general failure");
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }

  if (a_type == SOCKS5_ATYP_DOMAINNAME) {
    std::size_t const domain_length = p1[4];
    std::size_t const current_size = m_replyBuffer.size();
    m_replyBuffer.resize(current_size + domain_length - 3);
    m_tcpStream->expires_after(std::chrono::seconds(10));
    return m_tcpStream->async_read_some(
        net::mutable_buffer(m_replyBuffer.data() + current_size,
                            domain_length - 3),
        [this](beast::error_code ec, std::size_t const sz) {
          processIPv4Response(ec, sz);
        });
  }

  if (a_type == SOCKS5_ATYP_IPV6) {
    auto const curr_size = m_replyBuffer.size();
    m_replyBuffer.resize(curr_size + 12);
    m_tcpStream->expires_after(std::chrono::seconds(10));
    return m_tcpStream->async_read_some(
        net::mutable_buffer(m_replyBuffer.data() + curr_size, 12),
        [this](beast::error_code ec, std::size_t const sz) {
          processIPv4Response(ec, sz);
        });
  }

  return processIPv4Response({}, sz);
}

void socks5_http_socket_base_t::processIPv4Response(
    beast::error_code ec, [[maybe_unused]] size_t const sz) {
  if (ec) {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }

  char const *p1 = reinterpret_cast<char const *>(m_replyBuffer.data());
  BOOST_ASSERT(p1 != nullptr);
  readByte<uint8_t>(p1); // version
  [[maybe_unused]] auto const rep = readByte<uint8_t>(p1);
  readByte<uint8_t>(p1);
  auto const a_type = readByte<uint8_t>(p1);

  if (a_type == SOCKS5_ATYP_DOMAINNAME) {
    auto const domain_length = readByte<uint8_t>(p1);
    std::string domain_name{};
    domain_name.reserve(domain_length);
    for (int i = 0; i != domain_length; ++i) {
      domain_name.push_back(readByte<uint8_t>(p1));
    }
    [[maybe_unused]] auto const port = readByte<uint16_t>(p1);
    (void)domain_name;
  } else if (a_type == SOCKS5_ATYP_IPV4) {
    [[maybe_unused]] tcp::endpoint remote_endp(
        net::ip::address_v4(readByte<uint32_t>(p1)), readByte<uint16_t>(p1));
  } else if (a_type == SOCKS5_ATYP_IPV6) {
    net::ip::address_v6::bytes_type bytes;
    for (auto i = 0; i != 16; ++i) {
      bytes[i] = readByte<uint8_t>(p1);
    }

    [[maybe_unused]] tcp::endpoint remote_endp(net::ip::address_v6(bytes),
                                               readByte<uint16_t>(p1));
  } else {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  return sendHttpData();
}

void socks5_http_socket_base_t::reconnect() {
  ++m_connectCount;
  if (m_connectCount >= MaxRetries) {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  connect();
}

void socks5_http_socket_base_t::chooseNextProxy(bool const is_first_request) {
  m_connectCount = 0;
  m_currentProxy = m_proxyProvider.nextEndpoint();
  if (!m_currentProxy) {
    // spdlog::error("error getting next endpoint");
    m_mobileNumbers.append(m_currentPhoneNumber);
    m_currentPhoneNumber.clear();
    return m_signal(search_result_type_e::RequestStop, m_currentPhoneNumber);
  }
  if (!is_first_request) {
    closeStream();
    m_tcpStream.emplace(net::make_strand(m_ioContext));
    return connect();
  }
}

void socks5_http_socket_base_t::currentProxyAssignProperty(
    proxy_property_e property) {
  if (m_currentProxy)
    m_currentProxy->property = property;
}

void socks5_http_socket_base_t::connect() {
  if (!m_currentProxy || m_appStopped) {
    if (m_appStopped && !m_currentPhoneNumber.empty())
      m_mobileNumbers.append(m_currentPhoneNumber);
    return m_currentPhoneNumber.clear();
  }
  m_tcpStream->expires_after(std::chrono::milliseconds(TimeoutMilliseconds));
  m_tcpStream->async_connect(static_cast<tcp::endpoint>(*m_currentProxy),
                             [=](auto const &ec) { onConnected(ec); });
}

void socks5_http_socket_base_t::sendFirstRequest() {
  if (m_appStopped) {
    if (!m_currentPhoneNumber.empty())
      m_mobileNumbers.append(m_currentPhoneNumber);
    return m_currentPhoneNumber.clear();
  }
  try {
    m_currentPhoneNumber = m_mobileNumbers.get();
    prepareRequestData(false);
    connect();
  } catch (empty_container_exception_t &) {
  }
}

void socks5_http_socket_base_t::receiveData() {
  m_tcpStream->expires_after(
      std::chrono::milliseconds(TimeoutMilliseconds * 4)); // 4*3secs
  m_httpResponse.emplace();
  m_buffer.emplace();
  http::async_read(*m_tcpStream, *m_buffer, *m_httpResponse,
                   [this](beast::error_code const ec, size_t const size) {
                     onDataReceived(ec, size);
                   });
}

void socks5_http_socket_base_t::startConnect() {
  chooseNextProxy(true);
  if (m_currentProxy)
    sendFirstRequest();
}

void socks5_http_socket_base_t::sendHttpData() {
  m_tcpStream->expires_after(std::chrono::milliseconds(TimeoutMilliseconds));
  http::async_write(*m_tcpStream, *m_httpRequest,
                    [this](beast::error_code const ec, size_t const size) {
                      onDataSent(ec, size);
                    });
}

void socks5_http_socket_base_t::onDataSent(beast::error_code ec,
                                           size_t const s) {
  (void)s;
  if (ec) {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  } else
    receiveData();
}

void socks5_http_socket_base_t::closeSocket() {
  m_tcpStream->cancel();
  m_signal.disconnect_all_slots();
  beast::error_code ec{};
  beast::get_lowest_layer(*m_tcpStream)
      .socket()
      .shutdown(net::socket_base::shutdown_both, ec);
  ec = {};
  beast::get_lowest_layer(*m_tcpStream).socket().close(ec);
}

void socks5_http_socket_base_t::sendNext() {
  if (m_appStopped) {
    if (!m_currentPhoneNumber.empty())
      m_mobileNumbers.append(m_currentPhoneNumber);
    return m_currentPhoneNumber.clear();
  }
  try {
    m_currentPhoneNumber = m_mobileNumbers.get();
    prepareRequestData(false);
    if (m_scansPerIP != 0 && m_currentProxy->numberScanned >= m_scansPerIP) {
      currentProxyAssignProperty(proxy_property_e::ProxyMaxedOut);
      return chooseNextProxy();
    }
    ++m_currentProxy->numberScanned;
    return sendHttpData();
  } catch (empty_container_exception_t &) {
  }
}

void socks5_http_socket_base_t::setAuthenticationHeader() {
  prepareRequestData(true);
}
} // namespace woody_server
