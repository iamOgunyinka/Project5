#pragma once

#include "sockets_interface.hpp"
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
enum class proxy_type_e;

enum class website_type_e {
  Unknown,
  AutoHomeRegister,
  JJGames,
  PPSports,
  Qunar,
  WatchHome,
  Wines,
  Xpuji,
  PcAuto,
  LisboaMacau,
  ChineseMacau,
  MacauBaccarat,
  SunCity,
  GrandLisboa
};

class proxy_base_t;
class number_stream_t;

struct socket_factory_t {
  static std::unique_ptr<sockets_interface>
  get_socket(website_type_e, ssl::context &, proxy_type_e, bool &,
             net::io_context &, proxy_base_t &, number_stream_t &, int);
};
} // namespace wudi_server
