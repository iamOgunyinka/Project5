#include "sockets_instantiator.hpp"

#include "chinese_macau_socket.hpp"
#include "digit_casinos_socks5_http.hpp"
#include "pp_sports.hpp"
#include "sockets_interface.hpp"

namespace wudi_server {

using pps_sk5 = pp_sports_socks5_socket_t;
using wines_sk5 = wines_socks5_socket_t;
using xpuji_sk5 = xpuji_socks5_socket_t;
using chm_sk5 = chinese_macau_socks5_socket_t;

std::unique_ptr<sockets_interface>
http_socket_factory_t::get_socks5_http_socket(
    website_type_e web_type, bool &is_stopped, net::io_context &io_context,
    proxy_base_t &proxy_provider, number_stream_t &number_stream, int per_ip) {
  switch (web_type) {
  case website_type_e::PPSports:
    return std::make_unique<pps_sk5>(is_stopped, io_context, proxy_provider,
                                     number_stream, per_ip);
  case website_type_e::Wines:
    return std::make_unique<wines_sk5>(is_stopped, io_context, proxy_provider,
                                       number_stream, per_ip);
  case website_type_e::Xpuji:
    return std::make_unique<xpuji_sk5>(is_stopped, io_context, proxy_provider,
                                       number_stream, per_ip);
  case website_type_e::ChineseMacau:
    return std::make_unique<chm_sk5>(is_stopped, io_context, proxy_provider,
                                     number_stream, per_ip);
  case website_type_e::DevilsHorn:
    return std::make_unique<devils_horn_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::FourtyFour:
    return std::make_unique<fourty_four_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::JSThree:
    return std::make_unique<js_three_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::Lebo:
    return std::make_unique<lebo_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::SugarRaise:
    return std::make_unique<sugar_raise_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::TigerFortress:
    return std::make_unique<tiger_fortress_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::Vip5:
    return std::make_unique<vip5_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  case website_type_e::Zed3:
    return std::make_unique<zed_three_socket_socks5_t>(
        is_stopped, io_context, proxy_provider, number_stream, per_ip);
  }
  return nullptr;
}
} // namespace wudi_server
