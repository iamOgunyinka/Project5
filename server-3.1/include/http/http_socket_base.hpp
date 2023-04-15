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
namespace net = boost::asio;
namespace http = beast::http;

using tcp = boost::asio::ip::tcp;

class http_socket_base_t : public socket_interface_t {
  std::optional<beast::tcp_stream> m_tcpStream = std::nullopt;
  std::optional<beast::flat_buffer> m_buffer = std::nullopt;
  std::size_t m_connectCount{};

protected:
  std::optional<http::request<http::string_body>> m_httpRequest = std::nullopt;
  std::optional<http::response<http::string_body>> m_httpResponse =
      std::nullopt;
  std::string m_currentPhoneNumber{};
  proxy_base_t::value_type m_currentProxy{nullptr};

protected:
  void closeSocket();
  void connect();
  void receiveData();
  void reconnect();
  void closeStream();
  void chooseNextProxy(bool is_first_request = false);
  void sendHttpData();
  void setAuthenticationHeader();
  void onDataSent(beast::error_code, size_t);
  void currentProxyAssignProperty(proxy_property_e);
  void sendFirstRequest();
  virtual void onConnected(beast::error_code);
  void sendNext();

  virtual void prepareRequestData(bool use_auth) = 0;
  virtual void onDataReceived(beast::error_code, size_t) const = 0;

public:
  http_socket_base_t(bool &stopped, net::io_context &, proxy_base_t &,
                     number_stream_t &, int scansPerIP);
  void startConnect() override;

public:
  ~http_socket_base_t() override;
};

} // namespace woody_server
