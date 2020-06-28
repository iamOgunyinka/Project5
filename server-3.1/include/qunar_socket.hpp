#pragma once

#include "http_socket_base.hpp"
#include "socks5_https_socket_base.hpp"

namespace wudi_server {
using tcp = boost::asio::ip::tcp;

class qunar_http_socket_t : public http_socket_base_t<qunar_http_socket_t> {
  using super_class = http_socket_base_t<qunar_http_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  qunar_http_socket_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~qunar_http_socket_t() {}
  std::string hostname() const;
};

//////////////////////////////////////////////////////////////

class qunar_socks5_socket_t : public socks5_https<qunar_socks5_socket_t> {
  using super_class = socks5_https<qunar_socks5_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  qunar_socks5_socket_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~qunar_socks5_socket_t() {}
  std::string hostname() const;
};

} // namespace wudi_server
