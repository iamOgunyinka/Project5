#pragma once

#include "http_socket_base.hpp"
#include "socks5_http_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;

class chinese_macau_http_socket_t
    : public http_socket_base_t<chinese_macau_http_socket_t> {
  using super_class = http_socket_base_t<chinese_macau_http_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  chinese_macau_http_socket_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~chinese_macau_http_socket_t() {}
  std::string hostname() const;
};

//////////////////////////////////////////////////////////////

class chinese_macau_socks5_socket_t
    : public socks5_http<chinese_macau_socks5_socket_t> {
  using super_class = socks5_http<chinese_macau_socks5_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  chinese_macau_socks5_socket_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~chinese_macau_socks5_socket_t() {}
  std::string hostname() const;
};

} // namespace wudi_server
