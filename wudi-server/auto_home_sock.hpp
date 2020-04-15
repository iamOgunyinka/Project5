#pragma once

#include "socks5_https_socket_base.hpp"

namespace wudi_server {
class auto_home_socket_t
    : public socks5_https_socket_base_t<auto_home_socket_t> {
  static std::string const password_base64_hash;
  static std::string const auto_home_hostname_;

public:
  void on_data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  auto_home_socket_t(bool &stopped, net::io_context &io,
                     proxy_provider_t &proxy_provider,
                     utilities::number_stream_t &numbers, ssl::context &);
  ~auto_home_socket_t();
  std::string hostname() const;
};
} // namespace wudi_server
