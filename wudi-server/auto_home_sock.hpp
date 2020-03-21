#pragma once

#include "web_base.hpp"

namespace wudi_server {
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class auto_home_socket_t : public web_base<auto_home_socket_t> {
  static std::string const password_base64_hash;
  static std::string const address_;

public:
  void on_data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  auto_home_socket_t(bool &stopped, net::io_context &io,
                     safe_proxy &proxy_provider, CustomTCH cth,
                     utilities::number_stream_t &numbers);
  ~auto_home_socket_t();
};
} // namespace wudi_server
