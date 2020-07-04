#include "sockets_instantiator.hpp"

#include "auto_home_socks5_sock.hpp"
#include "digit_casinos_socks5_https.hpp"
#include "grand_lisboa_socket.hpp"
#include "jjgames_socket.hpp"
#include "lisboa_macau_socket.hpp"
#include "macau_baccarat_socket.hpp"
#include "pc_auto_socket.hpp"
#include "qunar_socket.hpp"
#include "sockets_interface.hpp"
#include "sun_city_socket.hpp"

namespace wudi_server {

using ah_sk5 = auto_home_socks5_socket_t;
using jjgames_sk5 = jjgames_socket_t;
using qn_sk5 = qunar_socks5_socket_t;
using pcauto_sk5 = pc_auto_socks5_socket_t;
using lbm_sk5 = lisboa_macau_socks5_socket_t;
using glsb_sk5 = grand_lisboa_socks5_socket_t;
using sun_city_sk5 = sun_city_socks5_socket_t;
using mbs_sk5 = macau_baccarat_socks5_socket_t;

std::unique_ptr<sockets_interface>
http_socket_factory_t::get_socks5_https_socket(
    website_type_e web_type, ssl::context &ssl_context, bool &is_stopped,
    net::io_context &io_context, proxy_base_t &proxy_provider,
    number_stream_t &number_stream, int per_ip) {
  switch (web_type) {
  case website_type_e::AutoHomeRegister:
    return std::make_unique<ah_sk5>(ssl_context, is_stopped, io_context,
                                    proxy_provider, number_stream, per_ip);
  case website_type_e::Qunar:
    return std::make_unique<qn_sk5>(ssl_context, is_stopped, io_context,
                                    proxy_provider, number_stream, per_ip);
  case website_type_e::PcAuto:
    return std::make_unique<pcauto_sk5>(ssl_context, is_stopped, io_context,
                                        proxy_provider, number_stream, per_ip);
  case website_type_e::LisboaMacau:
    return std::make_unique<lbm_sk5>(ssl_context, is_stopped, io_context,
                                     proxy_provider, number_stream, per_ip);
  case website_type_e::JJGames:
    return std::make_unique<jjgames_sk5>(ssl_context, is_stopped, io_context,
                                         proxy_provider, number_stream, per_ip);
  case website_type_e::GrandLisboa:
    return std::make_unique<glsb_sk5>(ssl_context, is_stopped, io_context,
                                      proxy_provider, number_stream, per_ip);
  case website_type_e::SunCity:
    return std::make_unique<sun_city_sk5>(ssl_context, is_stopped, io_context,
                                          proxy_provider, number_stream,
                                          per_ip);
  case website_type_e::MacauBaccarat:
    return std::make_unique<mbs_sk5>(ssl_context, is_stopped, io_context,
                                     proxy_provider, number_stream, per_ip);

  case website_type_e::VNS:
    return std::make_unique<vns_socket_socks5_t>(ssl_context, is_stopped,
                                                 io_context, proxy_provider,
                                                 number_stream, per_ip);
  case website_type_e::Lottery81:
    return std::make_unique<lottery81_socket_socks5_t>(
        ssl_context, is_stopped, io_context, proxy_provider, number_stream,
        per_ip);
  case website_type_e::DragonFish:
    return std::make_unique<dragon_fish_socket_socks5_t>(
        ssl_context, is_stopped, io_context, proxy_provider, number_stream,
        per_ip);
  default:
    return get_socks5_http_socket(web_type, is_stopped, io_context,
                                  proxy_provider, number_stream, per_ip);
  }
  return nullptr;
}
} // namespace wudi_server
