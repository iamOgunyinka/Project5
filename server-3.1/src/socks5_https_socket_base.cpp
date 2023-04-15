#include "socks5_https_socket_base.hpp"
#include "number_stream.hpp"
#include "socks5_protocol.hpp"

#include <boost/asio/strand.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

namespace woody_server {
socks5_https_socket_base_t::socks5_https_socket_base_t(
    net::ssl::context &sslContext, bool &stopped, net::io_context &ioContext,
    proxy_base_t &proxyProvider, number_stream_t &numbers, int const scanPerIP)
    : socket_interface_t(ioContext, proxyProvider, numbers, stopped, scanPerIP),
      m_sslContext(sslContext),
      m_sslStream(std::in_place, net::make_strand(ioContext), sslContext) {}

void socks5_https_socket_base_t::onConnected(beast::error_code ec) {
  if (ec) {
    return reconnect();
  }
  return performSocks5Handshake();
}

void socks5_https_socket_base_t::performSocks5Handshake() {
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

  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(10));
  beast::get_lowest_layer(*m_sslStream)
      .async_write_some(
          net::const_buffer(static_cast<char const *>(m_handshakeBuffer.data()),
                            m_handshakeBuffer.size()),
          [this](beast::error_code ec, std::size_t const sz) {
            onFirstHandshakeInitiated(ec, sz);
          });
}

void socks5_https_socket_base_t::onFirstHandshakeInitiated(
    beast::error_code const ec, std::size_t const) {
  if (ec) { // could be timeout
    // spdlog::error("first handshake failed: {}", ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  return readFirstHandshakeResult();
}

void socks5_https_socket_base_t::readFirstHandshakeResult() {
  m_replyBuffer.clear();
  m_replyBuffer.resize(2);
  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(10));
  beast::get_lowest_layer(*m_sslStream)
      .async_read_some(
          net::mutable_buffer(m_replyBuffer.data(), m_replyBuffer.size()),
          [this](beast::error_code ec, std::size_t const sz) {
            onFirstHandshakeResponseReceived(ec, sz);
          });
}

void socks5_https_socket_base_t::onFirstHandshakeResponseReceived(
    beast::error_code const ec, [[maybe_unused]] std::size_t const sz) {

  if (ec) {
    // spdlog::error(ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  char const *p1 = static_cast<char const *>(m_replyBuffer.data());
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
    std::size_t const buffers_to_write =
        (std::size_t)username_length + password_length + 3;

    m_handshakeBuffer.reserve(buffers_to_write);

    m_handshakeBuffer.push_back(0x01); // auth version
    m_handshakeBuffer.push_back(username_length);
    std::copy(username.cbegin(), username.cend(),
              std::back_inserter(m_handshakeBuffer));
    m_handshakeBuffer.push_back(password_length);
    std::copy(password.cbegin(), password.cend(),
              std::back_inserter(m_handshakeBuffer));
    return beast::get_lowest_layer(*m_sslStream)
        .async_write_some(
            net::const_buffer(m_handshakeBuffer.data(),
                              m_handshakeBuffer.size()),
            [this](beast::error_code const ec, std::size_t const) {
              if (ec) {
                // spdlog::error("Unable to write to stream: {}", ec.message());
                return chooseNextProxy();
              }
              m_replyBuffer.clear();
              m_replyBuffer.resize(2);
              beast::get_lowest_layer(*m_sslStream)
                  .expires_after(std::chrono::seconds(10));
              return beast::get_lowest_layer(*m_sslStream)
                  .async_read_some(
                      net::mutable_buffer(m_replyBuffer.data(),
                                          m_replyBuffer.size()),
                      [this](beast::error_code ec, std::size_t const sz) {
                        onAuthResponseReceived(ec, sz);
                      });
            });
  }
  if (method == SOCKS5_AUTH_NONE) {
    return performSock5SecondHandshake();
  }
  // spdlog::error("unsupported socks version");
  currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
  chooseNextProxy();
}

void socks5_https_socket_base_t::onAuthResponseReceived(
    beast::error_code const ec, [[maybe_unused]] std::size_t const sz) {
  if (ec) {
    // spdlog::error("[auth_response_received] {}", ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  char const *p1 = static_cast<char const *>(m_replyBuffer.data());
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

void socks5_https_socket_base_t::performSock5SecondHandshake() {
  m_handshakeBuffer.clear();

  std::string const host_name = hostname();
  std::size_t const bytes_to_write = host_name.size() + 7;

  m_handshakeBuffer.reserve(std::max<std::size_t>(bytes_to_write, 22));

  m_handshakeBuffer.push_back(SOCKS_VERSION_5);
  m_handshakeBuffer.push_back(SOCKS_CMD_CONNECT);
  m_handshakeBuffer.push_back(0x00);
  m_handshakeBuffer.push_back(SOCKS5_ATYP_DOMAINNAME);
  m_handshakeBuffer.push_back(static_cast<uint8_t>(host_name.size()));
  std::copy(host_name.cbegin(), host_name.cend(),
            std::back_inserter(m_handshakeBuffer));

  uint16_t const connection_port = port();
  m_handshakeBuffer.push_back(((char *)&connection_port)[1]);
  m_handshakeBuffer.push_back(((char *)&connection_port)[0]);

  beast::get_lowest_layer(*m_sslStream)
      .async_write_some(
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

void socks5_https_socket_base_t::readSocks5ServerResponse() {
  m_replyBuffer.clear();
  m_replyBuffer.resize(10);
  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(10));
  beast::get_lowest_layer(*m_sslStream)
      .async_read_some(
          net::mutable_buffer(m_replyBuffer.data(), m_replyBuffer.size()),
          [this](beast::error_code ec, std::size_t const sz) {
            onHandshakeResponseReceived(ec, sz);
          });
}

void socks5_https_socket_base_t::onHandshakeResponseReceived(
    beast::error_code ec, std::size_t const sz) {
  if (ec) {
    // spdlog::error("[second_handshake_read_error] {} {}", ec.message(), sz);
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  char const *p1 = static_cast<char const *>(m_replyBuffer.data());
  BOOST_ASSERT(p1 != nullptr);

  auto const version = readByte<uint8_t>(p1);
  readByte<uint8_t>(p1); // response, not needed
  readByte<uint8_t>(p1); // reserved byte, not needed
  int const a_type = readByte<uint8_t>(p1);

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
    std::size_t const domain_length = readByte<uint8_t>(p1);
    std::size_t const current_size = m_replyBuffer.size();
    m_replyBuffer.resize(current_size + domain_length - 3);
    beast::get_lowest_layer(*m_sslStream)
        .expires_after(std::chrono::seconds(10));
    auto buf = static_cast<char *>(m_replyBuffer.data());
    return beast::get_lowest_layer(*m_sslStream)
        .async_read_some(
            net::mutable_buffer(buf + current_size, domain_length - 3),
            [this](beast::error_code ec, std::size_t const sz) {
              processIPv4Response(ec, sz);
            });
  }

  if (a_type == SOCKS5_ATYP_IPV6) {
    auto const curr_size = m_replyBuffer.size();
    m_replyBuffer.resize(curr_size + 12);
    auto buf = static_cast<char *>(m_replyBuffer.data());
    beast::get_lowest_layer(*m_sslStream)
        .expires_after(std::chrono::seconds(10));
    return beast::get_lowest_layer(*m_sslStream)
        .async_read_some(net::mutable_buffer(buf + curr_size, 12),
                         [this](beast::error_code ec, std::size_t const sz) {
                           processIPv4Response(ec, sz);
                         });
  }
  return processIPv4Response({}, sz);
}

void socks5_https_socket_base_t::processIPv4Response(
    beast::error_code ec, [[maybe_unused]] std::size_t const sz) {
  if (ec) {
    // spdlog::error("[processIPv4Response] {}", ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }

  char const *p1 = static_cast<char const *>(m_replyBuffer.data());
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
    auto const port = readByte<uint16_t>(p1);
    (void)port;
    (void)domain_name;
    // spdlog::info("DN: {} and {}", domain_name, port);
  } else if (a_type == SOCKS5_ATYP_IPV4) {
    tcp::endpoint remote_endp(net::ip::address_v4(readByte<uint32_t>(p1)),
                              readByte<uint16_t>(p1));
    // spdlog::info("v4: {} and {}", remote_endp.address().to_string(),
    //             remote_endp.port());
    (void)remote_endp;
  } else if (a_type == SOCKS5_ATYP_IPV6) {
    net::ip::address_v6::bytes_type bytes;
    for (auto i = 0; i != 16; ++i) {
      bytes[i] = readByte<uint8_t>(p1);
    }
    tcp::endpoint remote_endp(net::ip::address_v6(bytes),
                              readByte<uint16_t>(p1));
    // spdlog::info("v6: {} and {}", remote_endp.address().to_string(),
    //             remote_endp.port());
    (void)remote_endp;
  } else {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  return performSslHandshake();
}

void socks5_https_socket_base_t::performSslHandshake() {
  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(15));
  m_sslStream->async_handshake(
      net::ssl::stream_base::client,
      [this](beast::error_code ec) { return onSslHandshake(ec); });
}

void socks5_https_socket_base_t::onSslHandshake(beast::error_code const ec) {
  if (ec.category() == net::error::get_ssl_category() &&
#ifdef SSL_R_SHORT_READ
      ec.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ)
#else
      // OpenSSL 1.1.0
      ec.value() == boost::asio::ssl::error::stream_truncated
#endif
  ) {
    return sendHttpsData();
  }
  if (ec) {
    // spdlog::error("SSL handshake: {}", ec.message());
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  sendHttpsData();
}

void socks5_https_socket_base_t::performSslRitual() {
  auto const host_name = hostname();
  if (!SSL_set_tlsext_host_name(m_sslStream->native_handle(),
                                host_name.c_str())) {
    [[maybe_unused]] beast::error_code ec{static_cast<int>(::ERR_get_error()),
                                          net::error::get_ssl_category()};
    // spdlog::error("Unable to set TLS because: {}", ec.message());
  }
}

void socks5_https_socket_base_t::reconnect() {
  ++m_connectCount;
  if (m_connectCount >= MaxRetries) {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  connect();
}

void socks5_https_socket_base_t::chooseNextProxy(bool const is_first_request) {
  m_connectCount = 0;
  m_currentProxy = m_proxyProvider.nextEndpoint();
  if (!m_currentProxy) {
    m_mobileNumbers.append(m_currentNumber);
    m_currentNumber.clear();
    return m_signal(search_result_type_e::RequestStop, m_currentNumber);
  }
  if (!is_first_request) {
    m_sslStream->async_shutdown([=](beast::error_code) {
      m_sslStream.emplace(net::make_strand(m_ioContext), m_sslContext);
      performSslRitual();
      connect();
    });
  }
}

void socks5_https_socket_base_t::currentProxyAssignProperty(
    proxy_property_e property) {
  if (m_currentProxy)
    m_currentProxy->property = property;
}

void socks5_https_socket_base_t::connect() {
  if (!m_currentProxy || m_appStopped) {
    if (m_appStopped && !m_currentNumber.empty())
      m_mobileNumbers.append(m_currentNumber);
    m_currentNumber.clear();
    return;
  }
  beast::get_lowest_layer(*m_sslStream)
      .expires_after(std::chrono::milliseconds(TimeoutMilliseconds));
  beast::get_lowest_layer(*m_sslStream)
      .async_connect(*m_currentProxy,
                     [this](auto const &ec) { onConnected(ec); });
}

void socks5_https_socket_base_t::sendFirstRequest() {
  if (m_appStopped) {
    if (!m_currentNumber.empty()) {
      m_mobileNumbers.append(m_currentNumber);
    }
    m_currentNumber.clear();
    return;
  }
  try {
    m_currentNumber = m_mobileNumbers.get();
    prepareRequestData(false);
    connect();
  } catch (empty_container_exception_t &) {
  }
}

void socks5_https_socket_base_t::receiveData() {
  m_httpResponse.emplace();
  m_buffer.emplace();

  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(10));
  http::async_read(*m_sslStream, *m_buffer, *m_httpResponse,
                   [this](beast::error_code ec, std::size_t const sz) {
                     onDataReceived(ec, sz);
                   });
}

void socks5_https_socket_base_t::startConnect() {
  performSslRitual();
  chooseNextProxy(true);
  if (m_currentProxy)
    sendFirstRequest();
}

void socks5_https_socket_base_t::sendHttpsData() {
  beast::get_lowest_layer(*m_sslStream).expires_after(std::chrono::seconds(10));
  http::async_write(
      *m_sslStream, *m_httpRequest,
      beast::bind_front_handler(&socks5_https_socket_base_t::onDataSent, this));
}

void socks5_https_socket_base_t::onDataSent(
    beast::error_code ec, [[maybe_unused]] std::size_t const s) {
  if (ec) {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  receiveData();
}

void socks5_https_socket_base_t::closeSocket() {
  beast::get_lowest_layer(*m_sslStream).cancel();
  m_sslStream->async_shutdown([this](beast::error_code) {
    beast::get_lowest_layer(*m_sslStream).close();
  });
}

void socks5_https_socket_base_t::closeStream() {
  beast::get_lowest_layer(*m_sslStream).close();
}

void socks5_https_socket_base_t::sendNext() {
  if (m_appStopped) {
    if (!m_currentNumber.empty()) {
      m_mobileNumbers.append(m_currentNumber);
    }
    m_currentNumber.clear();
    return;
  }
  try {
    m_currentNumber = m_mobileNumbers.get();
    prepareRequestData(false);
    if (m_scansPerIP != 0 && m_currentProxy->numberScanned >= m_scansPerIP) {
      currentProxyAssignProperty(proxy_property_e::ProxyMaxedOut);
      return chooseNextProxy();
    }
    ++m_currentProxy->numberScanned;
    return sendHttpsData();
  } catch (empty_container_exception_t &) {
  }
}

void socks5_https_socket_base_t::setAuthenticationHeader() {
  prepareRequestData(true);
}
} // namespace woody_server
