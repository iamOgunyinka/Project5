#pragma once

#include "socks5_http_socket_base.hpp"

namespace wudi_server {
template <typename Proxy>
class watch_home_socks5_socket_t
    : public socks5_http_socket_base_t<watch_home_socks5_socket_t<Proxy>,
                                       Proxy> {
  static std::string const password_base64_hash;
  static char const *const watch_home_address;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  
  template <typename... Args>
  watch_home_socks5_socket_t(Args &&... args)
      : socks5_http_socket_base_t<watch_home_socks5_socket_t<Proxy>, Proxy>(
            std::forward<Args>(args)...) {}
  ~watch_home_socks5_socket_t() {}
  std::string hostname() const { return "www.xbiao.com"; }
};

template <typename Proxy>
void watch_home_socks5_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  std::string const target = "/user/login";
  this->request_.clear();
  this->request_.method(beast::http::verb::get);
  this->request_.version(11);
  this->request_.target(target);
  if (use_authentication_header) {
    this->request_.set(beast::http::field::proxy_authorization,
                       "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  this->request_.keep_alive(true);
  this->request_.set(beast::http::field::host, "www.xbiao.com");
  this->request_.set(beast::http::field::cache_control, "no-cache");
  this->request_.set(beast::http::field::user_agent,
                     utilities::get_random_agent());
  this->request_.set(beast::http::field::accept, "*/*");
  this->request_.set(beast::http::field::referer,
                     "http://www.xbiao.com/user/login");
  this->request_.set(beast::http::field::content_type,
                     "application/x-www-form-urlencoded");
  this->request_.body() = {};
  this->request_.prepare_payload();
}

template <typename Proxy>
void watch_home_socks5_socket_t<Proxy>::data_received(beast::error_code ec,
                                                      std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      this->current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = this->response_.result_int();
  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  auto iterator_pair = this->response_.equal_range(http::field::set_cookie);
  if (iterator_pair.first == iterator_pair.second) {
    this->signal_(search_result_type_e::Unknown, this->current_number_);
    this->current_number_.clear();
    return this->send_next();
  }
  return this->send_next();
}

} // namespace wudi_server