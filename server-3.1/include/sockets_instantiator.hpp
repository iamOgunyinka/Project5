#pragma once

#include <memory>

namespace boost::asio {
class io_context;
namespace ssl {
class context;
} // namespace ssl
} // namespace boost::asio

namespace woody_server {
namespace net = boost::asio;
namespace ssl = net::ssl;

// forward declarations
enum class supported_websites_e;
enum class proxy_type_e;

class proxy_base_t;
class number_stream_t;
class socket_interface_t;

struct socket_instantiator_t {
  static std::unique_ptr<socket_interface_t>
  get_socket(supported_websites_e, ssl::context &, proxy_type_e, bool &,
             net::io_context &, proxy_base_t &, number_stream_t &, int);
};

class http_socket_factory_t {
  friend struct socket_instantiator_t;
  static std::unique_ptr<socket_interface_t>
  get_http_socket(supported_websites_e, bool &, net::io_context &,
                  proxy_base_t &, number_stream_t &, int);

  static std::unique_ptr<socket_interface_t>
  get_socks5_https_socket(supported_websites_e, ssl::context &, bool &,
                          net::io_context &, proxy_base_t &, number_stream_t &,
                          int);

  static std::unique_ptr<socket_interface_t>
  get_socks5_http_socket(supported_websites_e, bool &, net::io_context &,
                         proxy_base_t &, number_stream_t &, int);
};
} // namespace woody_server
