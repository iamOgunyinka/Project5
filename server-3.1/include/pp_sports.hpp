#pragma once

#include "http_socket_base.hpp"
#include "socks5_http_socket_base.hpp"

namespace wudi_server {

class pp_sports_http_socket_t
    : public http_socket_base_t<pp_sports_http_socket_t> {
  using base_class = http_socket_base_t<pp_sports_http_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  pp_sports_http_socket_t(Args &&... args)
      : base_class(std::forward<Args>(args)...) {}
  ~pp_sports_http_socket_t() {}
  std::string hostname() const { return "api.passport.pptv.com"; }
};

class pp_sports_socks5_socket_t
    : public socks5_http_socket_base_t<pp_sports_socks5_socket_t> {
  using base_class = socks5_http_socket_base_t<pp_sports_socks5_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  pp_sports_socks5_socket_t(Args &&... args)
      : base_class(std::forward<Args>(args)...) {}
  ~pp_sports_socks5_socket_t() {}
  std::string hostname() const { return "api.passport.pptv.com"; }
};

} // namespace wudi_server
