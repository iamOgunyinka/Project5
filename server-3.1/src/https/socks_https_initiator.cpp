#include "sockets_instantiator.hpp"

#include "lazada_socks5.hpp"

namespace woody_server {

std::unique_ptr<socket_interface_t>
http_socket_factory_t::get_socks5_https_socket(
    supported_websites_e web_type, ssl::context &ssl_context, bool &is_stopped,
    net::io_context &io_context, proxy_base_t &proxy_provider,
    number_stream_t &number_stream, int per_ip) {
  switch (web_type) {
  case supported_websites_e::LacazaPhillipines:
    return std::make_unique<lazada_socks5_t>(ssl_context, is_stopped,
                                             io_context, proxy_provider,
                                             number_stream, per_ip);
  default:
    return get_socks5_http_socket(web_type, is_stopped, io_context,
                                  proxy_provider, number_stream, per_ip);
  }
}
} // namespace woody_server
