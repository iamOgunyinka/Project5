#pragma once

#include "safe_proxy.hpp"
#include "sockets_interface.hpp"

#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/ssl.hpp>
#include <optional>

namespace woody_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
namespace ssl = net::ssl;

using tcp = boost::asio::ip::tcp;

class socks5_https_socket_base_t : public socket_interface_t {
  net::ssl::context &m_sslContext;
  std::optional<beast::ssl_stream<beast::tcp_stream>> m_sslStream =
      std::nullopt;
  std::optional<beast::flat_buffer> m_buffer{};
  std::vector<char> m_replyBuffer{};
  std::vector<char> m_handshakeBuffer{};
  size_t m_connectCount{};

protected:
  proxy_base_t::value_type m_currentProxy{nullptr};
  std::optional<http::request<http::string_body>> m_httpRequest = std::nullopt;
  std::optional<http::response<http::string_body>> m_httpResponse =
      std::nullopt;
  std::string m_currentNumber{};

protected:
  void sendFirstRequest();
  void performSocks5Handshake();
  void onFirstHandshakeInitiated(beast::error_code, size_t);
  void readFirstHandshakeResult();
  void onFirstHandshakeResponseReceived(beast::error_code, size_t);
  void performSock5SecondHandshake();
  void onAuthResponseReceived(beast::error_code, size_t);
  void processIPv4Response(beast::error_code, size_t);
  void onHandshakeResponseReceived(beast::error_code, size_t);
  void readSocks5ServerResponse();
  void setAuthenticationHeader();
  void performSslHandshake();
  void onSslHandshake(beast::error_code);

  void closeSocket();
  void closeStream();
  void performSslRitual();
  void connect();
  void receiveData();
  void reconnect();
  void chooseNextProxy(bool first_request = false);
  void sendHttpsData();
  void onDataSent(beast::error_code, size_t);
  void currentProxyAssignProperty(proxy_property_e);
  void onConnected(beast::error_code);

  virtual void onDataReceived(beast::error_code, size_t) = 0;
  virtual std::string hostname() const = 0;
  virtual void prepareRequestData(bool use_auth) = 0;

  void sendNext();
  virtual uint16_t port() const { return 443; }

public:
  socks5_https_socket_base_t(net::ssl::context &, bool &, net::io_context &,
                             proxy_base_t &, number_stream_t &, int);
  void startConnect() override;
  virtual ~socks5_https_socket_base_t() {
    m_signal.disconnect_all_slots();
    closeSocket();
  }
};

} // namespace woody_server
