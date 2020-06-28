#pragma once

#include "http_socket_base.hpp"
#include "safe_proxy.hpp"
#include "socks5_https_socket_base.hpp"

namespace wudi_server {
class macau_baccarat_http_socket_t
    : public http_socket_base_t<macau_baccarat_http_socket_t> {
  using base_class = http_socket_base_t<macau_baccarat_http_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  macau_baccarat_http_socket_t(Args &&... args)
      : base_class(std::forward<Args>(args)...) {}
  ~macau_baccarat_http_socket_t() {}
  std::string hostname() const { return "55222077.com"; }
};

class macau_baccarat_socks5_socket_t
    : public socks5_https_socket_base_t<macau_baccarat_socks5_socket_t> {
  using base_class =
      socks5_https_socket_base_t<macau_baccarat_socks5_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  macau_baccarat_socks5_socket_t(Args &&... args)
      : base_class(std::forward<Args>(args)...) {}
  ~macau_baccarat_socks5_socket_t() {}
  std::string hostname() const { return "55222077.com"; }
  uint16_t port() const override { return 8080; }
};

} // namespace wudi_server
