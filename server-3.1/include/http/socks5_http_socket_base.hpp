#pragma once

#include "safe_proxy.hpp"
#include "sockets_interface.hpp"
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <optional>

namespace woody_server {
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class socks5_http_socket_base_t : public socket_interface_t {
  std::optional<beast::tcp_stream> m_tcpStream;
  std::optional<beast::flat_buffer> m_buffer{};
  std::size_t m_connectCount{};
  proxy_base_t::value_type m_currentProxy{nullptr};
  std::vector<uint8_t> m_handshakeBuffer{};
  std::vector<uint8_t> m_replyBuffer{};

protected:
  std::optional<http::request<http::string_body>> m_httpRequest = std::nullopt;
  std::optional<http::response<http::string_body>> m_httpResponse =
      std::nullopt;
  std::string m_currentPhoneNumber{};

protected:
  void sendFirstRequest();
  void readFirstHandshakeResult();
  void performSocks5Handshake();
  void setAuthenticationHeader();
  void onFirstHandshakeResponseReceived(beast::error_code, size_t);
  void onFirstHandshakeInitiated(beast::error_code, size_t);
  void onAuthResponseReceived(beast::error_code, size_t);
  void readSocks5ServerResponse();
  void onHandshakeResponseReceived(beast::error_code, size_t);
  void processIPv4Response(beast::error_code, size_t);
  void performSock5SecondHandshake();

  void closeSocket();
  void connect();
  void receiveData();
  void reconnect();
  void chooseNextProxy(bool is_first_request = false);
  void onDataSent(beast::error_code, std::size_t const);
  void currentProxyAssignProperty(proxy_property_e);

  virtual void prepareRequestData(bool use_auth) = 0;
  virtual void onDataReceived(beast::error_code, std::size_t) const = 0;
  [[nodiscard]] virtual std::string hostname() const = 0;

  void onConnected(beast::error_code);
  void sendHttpData();
  void sendNext();
  void closeStream() { m_tcpStream->close(); }

public:
  socks5_http_socket_base_t(bool &, net::io_context &, proxy_base_t &,
                            number_stream_t &, int);
  void startConnect() override;
  virtual ~socks5_http_socket_base_t() {
    m_signal.disconnect_all_slots();
    closeSocket();
  }
};

} // namespace woody_server
