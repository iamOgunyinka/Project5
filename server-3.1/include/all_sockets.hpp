#pragma once

#include "auto_home_http_sock.hpp"
#include "auto_home_socks5_sock.hpp"
#include "jjgames_socket.hpp"
#include "pp_sports.hpp"
#include "qunar_socket.hpp"
#include "safe_proxy.hpp"
#include "watch_home_http.hpp"
#include "watch_home_socks5.hpp"

#include <variant>

namespace wudi_server {
using ah_https = auto_home_http_socket_t<proxy_provider_t>;
using ah_sk5 = auto_home_socks5_socket_t<proxy_provider_t>;
using pps_http = pp_sports_http_socket_t<proxy_provider_t>;
using pps_sk5 = pp_sports_socks5_socket_t<proxy_provider_t>;
using jjgames_sk5 = jjgames_socket<proxy_provider_t>;
using wh_http = watch_home_http_socket_t<proxy_provider_t>;
using wh_sk5 = watch_home_socks5_socket_t<proxy_provider_t>;
using qn_http = qunar_http_socket<proxy_provider_t>;
using qn_sk5 = qunar_socks5_socket<proxy_provider_t>;

using vsocket_type = std::variant<wh_http, wh_sk5, ah_https, ah_sk5, pps_http,
                                  pps_sk5, jjgames_sk5, qn_http, qn_sk5>;
} // namespace wudi_server
