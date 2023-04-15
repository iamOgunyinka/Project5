#include "sockets_instantiator.hpp"

#include "lazada_http.hpp"

namespace woody_server {
std::unique_ptr<socket_interface_t> socket_instantiator_t::get_socket(
    supported_websites_e web_type, ssl::context &ssl_context,
    proxy_type_e proxy_type, bool &is_stopped, net::io_context &io_context,
    proxy_base_t &proxy_provider, number_stream_t &number_stream, int per_ip) {
  if (proxy_type == proxy_type_e::http_https_proxy) {
    return http_socket_factory_t::get_http_socket(web_type, is_stopped,
                                                  io_context, proxy_provider,
                                                  number_stream, per_ip);
  }
  return http_socket_factory_t::get_socks5_https_socket(
      web_type, ssl_context, is_stopped, io_context, proxy_provider,
      number_stream, per_ip);
}

std::unique_ptr<socket_interface_t> http_socket_factory_t::get_http_socket(
    supported_websites_e web_type, bool &is_stopped,
    net::io_context &io_context, proxy_base_t &proxy_provider,
    number_stream_t &number_stream, int per_ip) {
  if (web_type == supported_websites_e::LacazaPhillipines) {
    return std::make_unique<lazada_http_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  }
  return nullptr;
}

} // namespace woody_server
