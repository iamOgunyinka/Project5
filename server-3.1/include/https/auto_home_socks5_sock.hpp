#pragma once

#include "socks5_https_socket_base.hpp"

namespace wudi_server {
namespace utilities {
std::string get_random_agent();
}

namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;

class auto_home_socks5_socket_t
    : public socks5_https_socket_base_t<auto_home_socks5_socket_t> {
  using super_class = socks5_https_socket_base_t<auto_home_socks5_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  auto_home_socks5_socket_t(ssl::context &ssl_context, Args &&... args)
      : super_class(ssl_context, std::forward<Args>(args)...) {}
  ~auto_home_socks5_socket_t() {}
  std::string hostname() const;
};
} // namespace wudi_server
