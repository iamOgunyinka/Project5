#include "sockets_instantiator.hpp"
#include "sockets_interface.hpp"

namespace woody_server {

std::unique_ptr<socket_interface_t>
http_socket_factory_t::get_socks5_http_socket(supported_websites_e, bool &,
                                              net::io_context &, proxy_base_t &,
                                              number_stream_t &, int) {
  return nullptr;
}
} // namespace woody_server
