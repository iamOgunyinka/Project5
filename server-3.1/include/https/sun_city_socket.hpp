#pragma once

#include "http_socket_base.hpp"
#include "socks5_https_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;

class sun_city_http_socket_t
    : public http_socket_base_t<sun_city_http_socket_t> {
  using super_class = http_socket_base_t<sun_city_http_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  sun_city_http_socket_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~sun_city_http_socket_t() {}
  std::string hostname() const;
};

//////////////////////////////////////////////////////////////

class sun_city_socks5_socket_t : public socks5_https<sun_city_socks5_socket_t> {
  using super_class = socks5_https<sun_city_socks5_socket_t>;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  sun_city_socks5_socket_t(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~sun_city_socks5_socket_t() {}
  std::string hostname() const;
};
} // namespace wudi_server
