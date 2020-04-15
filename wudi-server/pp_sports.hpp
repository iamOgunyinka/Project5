#pragma once

#include "socks5_http_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

class pp_sports_t : public socks5_http_socket_base_t<pp_sports_t> {
  static std::string const password_base64_hash;
  static char const *const pp_sports_hostname;

public:
  void on_data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  pp_sports_t(bool &stopped, net::io_context &io,
              proxy_provider_t &proxy_provider,
              utilities::number_stream_t &numbers);
  ~pp_sports_t();
  std::string hostname() const;
};
} // namespace wudi_server
