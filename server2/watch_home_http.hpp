#pragma once

#include "http_socket_base.hpp"

namespace wudi_server {
template <typename Proxy>
class watch_home_http_socket_t
    : public http_socket_base_t<watch_home_http_socket_t<Proxy>, Proxy> {
  static std::string const password_base64_hash;
  static char const *const watch_home_address;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  template <typename... Args>
  watch_home_http_socket_t(Args &&... args)
      : http_socket_base_t<watch_home_http_socket_t<Proxy>, Proxy>(
            std::forward<Args>(args)...) {}
  ~watch_home_http_socket_t() {}
  std::string hostname() const { return "www.xbiao.com"; }
};

template <typename Proxy>
void watch_home_http_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  std::string const target = "http://www.xbiao.com/user/login";
  request_.clear();
  request_.method(beast::http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(beast::http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.keep_alive(true);
  request_.set(beast::http::field::host, "www.xbiao.com:80");
  request_.set(beast::http::field::cache_control, "no-cache");
  request_.set(beast::http::field::user_agent, utilities::get_random_agent());
  request_.set(beast::http::field::accept, "*/*");
  request_.set(beast::http::field::referer, "http://www.xbiao.com/user/login");
  request_.set(beast::http::field::content_type,
               "application/x-www-form-urlencoded");
  request_.body() = {};
  request_.prepare_payload();
}

template <typename Proxy>
void watch_home_http_socket_t<Proxy>::data_received(beast::error_code ec,
                                                    std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
      tcp_stream_.close();
    }
    choose_next_proxy();
    return connect();
  }

  std::size_t const status_code = response_.result_int();
  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    set_authentication_header();
    return connect();
  }

  auto iterator_pair = response_.equal_range(http::field::set_cookie);
  if (iterator_pair.first == iterator_pair.second) {
    signal_(search_result_type_e::Unknown, current_number_);
    current_number_.clear();
    return send_next();
  }
  return send_next();
}

} // namespace wudi_server
