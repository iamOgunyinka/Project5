#pragma once

#include "http_socket_base.hpp"
#include "safe_proxy.hpp"
#include "socks5_http_socket_base.hpp"

namespace wudi_server {
class xpuji_http_socket_t : public http_socket_base_t<xpuji_http_socket_t> {

  using base_class = http_socket_base_t<xpuji_http_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  xpuji_http_socket_t(Args &&... args)
      : base_class(std::forward<Args>(args)...) {}
  ~xpuji_http_socket_t() {}
  std::string hostname() const { return "s32689.net"; }
};

class xpuji_socks5_socket_t
    : public socks5_http_socket_base_t<xpuji_socks5_socket_t> {
  using parent_class = socks5_http_socket_base_t<xpuji_socks5_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  xpuji_socks5_socket_t(Args &&... args)
      : parent_class(std::forward<Args>(args)...) {}
  ~xpuji_socks5_socket_t() {}
  std::string hostname() const { return "s32689.net"; }
};

} // namespace wudi_server
