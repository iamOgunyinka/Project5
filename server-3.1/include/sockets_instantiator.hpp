#pragma once

#include <memory>

namespace boost {
namespace asio {
class io_context;
namespace ssl {

class context;
}
} // namespace asio
} // namespace boost

namespace wudi_server {
namespace ssl = boost::asio::ssl;
namespace net = boost::asio;

enum class website_type_e;

enum class proxy_type_e;
class proxy_base_t;
class number_stream_t;
class sockets_interface;

struct socket_instantiator {
  static std::unique_ptr<sockets_interface>
  get_socket(website_type_e, ssl::context &, proxy_type_e, bool &,
             net::io_context &, proxy_base_t &, number_stream_t &, int);
};

class http_socket_factory_t {
  friend struct socket_instantiator;
  static std::unique_ptr<sockets_interface>
  get_http_socket(website_type_e, bool &, net::io_context &, proxy_base_t &,
                  number_stream_t &, int);

  static std::unique_ptr<sockets_interface>
  get_socks5_https_socket(website_type_e, ssl::context &, bool &,
                          net::io_context &, proxy_base_t &, number_stream_t &,
                          int);

  static std::unique_ptr<sockets_interface>
  get_socks5_http_socket(website_type_e, bool &, net::io_context &,
                         proxy_base_t &, number_stream_t &, int);
};
} // namespace wudi_server
