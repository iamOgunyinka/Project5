#include "socket_factory.hpp"
#include "auto_home_http_sock.hpp"
#include "auto_home_socks5_sock.hpp"
#include "chinese_macau_socket.hpp"
#include "jjgames_socket.hpp"
#include "lisboa_macau_socket.hpp"
#include "pc_auto_socket.hpp"
#include "pp_sports.hpp"
#include "qunar_socket.hpp"
#include "safe_proxy.hpp"
#include "watch_home_http.hpp"
#include "watch_home_socks5.hpp"
#include "wines_socket.hpp"
#include "xpuji_socket.hpp"
#include <random>

namespace wudi_server {

using ah_http = auto_home_http_socket_t<proxy_base_t>;
using ah_sk5 = auto_home_socks5_socket_t<proxy_base_t>;
using pps_http = pp_sports_http_socket_t<proxy_base_t>;
using pps_sk5 = pp_sports_socks5_socket_t<proxy_base_t>;
using jjgames_sk5 = jjgames_socket_t<proxy_base_t>;
using wh_http = watch_home_http_socket_t<proxy_base_t>;
using wh_sk5 = watch_home_socks5_socket_t<proxy_base_t>;
using qn_http = qunar_http_socket_t<proxy_base_t>;
using qn_sk5 = qunar_socks5_socket_t<proxy_base_t>;
using wines_http = wines_http_socket_t<proxy_base_t>;
using wines_sk5 = wines_socks5_socket_t<proxy_base_t>;
using xpuji_sk5 = xpuji_socks5_socket_t<proxy_base_t>;
using xpuji_http = xpuji_http_socket_t<proxy_base_t>;
using pcauto_http = pc_auto_http_socket_t<proxy_base_t>;
using pcauto_sk5 = pc_auto_socks5_socket_t<proxy_base_t>;
using lbm_http = lisboa_macau_http_socket_t<proxy_base_t>;
using lbm_sk5 = lisboa_macau_socks5_socket_t<proxy_base_t>;
using chm_http = chinese_macau_http_socket_t<proxy_base_t>;
using chm_sk5 = chinese_macau_socks5_socket_t<proxy_base_t>;

std::unique_ptr<sockets_interface> socket_factory_t::get_socket(
    website_type_e web_type, ssl::context &ssl_context, proxy_type_e proxy_type,
    bool &is_stopped, net::io_context &io_context, proxy_base_t &proxy_provider,
    number_stream_t &number_stream, int per_ip) {
  if (proxy_type == proxy_type_e::http_https_proxy) {
    switch (web_type) {
    case website_type_e::AutoHomeRegister:
      return std::make_unique<ah_http>(is_stopped, io_context, proxy_provider,
                                       number_stream, per_ip);
    case website_type_e::JJGames:
      return nullptr;
    case website_type_e::PPSports:
      return std::make_unique<pps_http>(is_stopped, io_context, proxy_provider,
                                        number_stream, per_ip);
    case website_type_e::Qunar:
      return std::make_unique<qn_http>(is_stopped, io_context, proxy_provider,
                                       number_stream, per_ip);
    case website_type_e::WatchHome:
      return std::make_unique<wh_http>(is_stopped, io_context, proxy_provider,
                                       number_stream, per_ip);
    case website_type_e::Wines:
      return std::make_unique<wines_http>(
          is_stopped, io_context, proxy_provider, number_stream, per_ip);
    case website_type_e::Xpuji:
      return std::make_unique<xpuji_http>(
          is_stopped, io_context, proxy_provider, number_stream, per_ip);
    case website_type_e::PcAuto:
      return std::make_unique<pcauto_http>(
          is_stopped, io_context, proxy_provider, number_stream, per_ip);
    case website_type_e::LisboaMacau:
      return std::make_unique<lbm_http>(is_stopped, io_context, proxy_provider,
                                        number_stream, per_ip);
    case website_type_e::ChineseMacau:
      return std::make_unique<chm_http>(is_stopped, io_context, proxy_provider,
                                        number_stream, per_ip);
    }
  } else {
    switch (web_type) {
    case website_type_e::AutoHomeRegister:
      return std::make_unique<ah_sk5>(ssl_context, is_stopped, io_context,
                                      proxy_provider, number_stream, per_ip);
    case website_type_e::Qunar:
      return std::make_unique<qn_sk5>(ssl_context, is_stopped, io_context,
                                      proxy_provider, number_stream, per_ip);
    case website_type_e::PPSports:
      return std::make_unique<pps_sk5>(is_stopped, io_context, proxy_provider,
                                       number_stream, per_ip);
    case website_type_e::WatchHome:
      return std::make_unique<wh_sk5>(is_stopped, io_context, proxy_provider,
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
    case website_type_e::PcAuto:
      return std::make_unique<pcauto_sk5>(ssl_context, is_stopped, io_context,
                                          proxy_provider, number_stream,
                                          per_ip);
    case website_type_e::LisboaMacau:
      return std::make_unique<lbm_sk5>(ssl_context, is_stopped, io_context,
                                       proxy_provider, number_stream, per_ip);
    case website_type_e::JJGames:
      return std::make_unique<jjgames_sk5>(ssl_context, is_stopped, io_context,
                                           proxy_provider, number_stream,
                                           per_ip);
    }
  }
  return nullptr;
}

time_data_t get_time_data() {
  static std::random_device rd{};
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<> dis(0.0, 1.0);
  uint64_t const current_time = std::time(nullptr) * 1'000;
  std::size_t const random_number =
      static_cast<std::size_t>(std::round(1e3 * dis(gen)));
  std::uint64_t const callback_number =
      static_cast<std::size_t>(current_time + random_number);
  return time_data_t{current_time, callback_number};
}

} // namespace wudi_server
