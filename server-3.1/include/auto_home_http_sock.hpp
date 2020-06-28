#pragma once

#include "http_socket_base.hpp"

namespace wudi_server {
namespace utilities {
std::string get_random_agent();
}

namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;

class auto_home_http_socket_t
    : public http_socket_base_t<auto_home_http_socket_t> {
  using super_class = http_socket_base_t<auto_home_http_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  auto_home_http_socket_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~auto_home_http_socket_t() {}
};

} // namespace wudi_server
