#include "sockets_instantiator.hpp"

#include "auto_home_http_sock.hpp"
#include "auto_home_socks5_sock.hpp"
#include "chinese_macau_socket.hpp"
#include "grand_lisboa_socket.hpp"
#include "jjgames_socket.hpp"
#include "lisboa_macau_socket.hpp"
#include "macau_baccarat_socket.hpp"
#include "pc_auto_socket.hpp"
#include "pp_sports.hpp"
#include "qunar_socket.hpp"
#include "sockets_interface.hpp"
#include "socks_initiator.hpp"
#include "sun_city_socket.hpp"
#include "wines_socket.hpp"
#include "xpuji_socket.hpp"

namespace wudi_server {

using ah_http = auto_home_http_socket_t;
using pps_http = pp_sports_http_socket_t;
using qn_http = qunar_http_socket_t;
using wines_http = wines_http_socket_t;
using xpuji_http = xpuji_http_socket_t;
using pcauto_http = pc_auto_http_socket_t;
using lbm_http = lisboa_macau_http_socket_t;
using chm_http = chinese_macau_http_socket_t;
using glsb_http = grand_lisboa_http_socket_t;
using sun_city_http = sun_city_http_socket_t;
using mbs_http = macau_baccarat_http_socket_t;

std::unique_ptr<sockets_interface> socket_instantiator::get_socket(
    website_type_e web_type, ssl::context &ssl_context, proxy_type_e proxy_type,
    bool &is_stopped, net::io_context &io_context, proxy_base_t &proxy_provider,
    number_stream_t &number_stream, int per_ip) {
  if (proxy_type == proxy_type_e::http_https_proxy) {
    return http_socket_factory_t::get_http_socket(web_type, is_stopped,
                                                  io_context, proxy_provider,
                                                  number_stream, per_ip);
  }
  return socks5_socket_factory_t::get_socks5_socket(
      web_type, ssl_context, is_stopped, io_context, proxy_provider,
      number_stream, per_ip);
}

std::unique_ptr<sockets_interface> http_socket_factory_t::get_http_socket(
    website_type_e web_type, bool &is_stopped, net::io_context &io_context,
    proxy_base_t &proxy_provider, number_stream_t &number_stream, int per_ip) {
  switch (web_type) {
  case website_type_e::AutoHomeRegister:
    return std::make_unique<ah_http>(is_stopped, io_context, proxy_provider,
                                     number_stream, per_ip);
  case website_type_e::PPSports:
    return std::make_unique<pps_http>(is_stopped, io_context, proxy_provider,
                                      number_stream, per_ip);
  case website_type_e::Qunar:
    return std::make_unique<qn_http>(is_stopped, io_context, proxy_provider,
                                     number_stream, per_ip);
  case website_type_e::Wines:
    return std::make_unique<wines_http>(is_stopped, io_context, proxy_provider,
                                        number_stream, per_ip);
  case website_type_e::Xpuji:
    return std::make_unique<xpuji_http>(is_stopped, io_context, proxy_provider,
                                        number_stream, per_ip);
  case website_type_e::PcAuto:
    return std::make_unique<pcauto_http>(is_stopped, io_context, proxy_provider,
                                         number_stream, per_ip);
  case website_type_e::LisboaMacau:
    return std::make_unique<lbm_http>(is_stopped, io_context, proxy_provider,
                                      number_stream, per_ip);
  case website_type_e::ChineseMacau:
    return std::make_unique<chm_http>(is_stopped, io_context, proxy_provider,
                                      number_stream, per_ip);
  case website_type_e::GrandLisboa:
    return std::make_unique<glsb_http>(is_stopped, io_context, proxy_provider,
                                       number_stream, per_ip);
  case website_type_e::SunCity:
    return std::make_unique<sun_city_http>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::MacauBaccarat:
    return std::make_unique<mbs_http>(is_stopped, io_context, proxy_provider,
                                      number_stream, per_ip);
  }
  return nullptr;
}

} // namespace wudi_server
