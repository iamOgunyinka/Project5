#pragma once

#include "web_base.hpp"

namespace wudi_server {
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class watch_home_t : public web_base<watch_home_t> {
  static std::string const password_base64_hash;
  static char const *const watch_home_address;

public:
  void on_data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  watch_home_t(bool &stopped, net::io_context &io,
               proxy_provider_t &proxy_provider,
               utilities::number_stream_t &numbers);
  ~watch_home_t();
};
} // namespace wudi_server
