#pragma once

#include "socks5_https_socket_base.hpp"
#include <spdlog/fmt/fmt.h>

namespace wudi_server {
using namespace fmt::v6::literals;

using utilities::request_handler;
using namespace fmt::v6::literals;

template <typename Proxy>
class jjgames_socket
    : public socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy> {
  static std::string jjgames_hostname;
  void process_response(std::string const &);

  using super_class = socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy>;
  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::signal_;

public:
  template <typename... Args>
  jjgames_socket(ssl::context &ssl_context, Args &&... args)
      : socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy>{
            ssl_context, std::forward<Args>(args)...} {}

  ~jjgames_socket() {}
  void prepare_request_data(bool use_auth = false);
  void data_received(beast::error_code, std::size_t const);
  std::string hostname() const;
};

template <typename Proxy>
std::string jjgames_socket<Proxy>::jjgames_hostname{"a4.srv.jj.cn"};

template <typename Proxy> std::string jjgames_socket<Proxy>::hostname() const {
  return jjgames_hostname;
}

template <typename Proxy>
void jjgames_socket<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  auto const time_data = utilities::get_time_data();
  std::string const target =
      "/reg/check_loginname.php?regtype=2&t={}&n=1&loginname={}&callback="
      "JSONP_{}"_format(time_data.current_time, current_number_,
                        time_data.callback_number);
  std::string const md5_hash =
      utilities::md5(std::to_string(time_data.current_time));
  auto const seconds = time_data.current_time / 1'000;
  std::string const cookie =
      "Hm_lvt_{}={}; visitorId=4460870697_{}; Hm_lpvt_{}={}"_format(
          md5_hash, seconds, time_data.current_time, md5_hash, seconds + 3);

  request_.clear();
  request_.method(beast::http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.keep_alive(true);
  request_.set(http::field::host, jjgames_hostname);
  request_.set(beast::http::field::user_agent, utilities::get_random_agent());
  request_.set("sec-fetch-dest", "script");
  request_.set(beast::http::field::accept, "*/*");
  request_.set(http::field::referer,
               "https://www.jj.cn/reg/reg.html?type=phone");
  request_.set("sec-fetch-site", "same-site");
  request_.set("sec-fetch-mode", "no-cors");
  request_.set(http::field::accept_language, "en-US,en;q=0.5 --compressed");
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::referer,
               "https://www.jj.cn/reg/reg.html?type=phone");
  request_.set(http::field::accept_language, "en-US,en;q=0.5 --compressed");
  request_.set(http::field::cookie, cookie);
  request_.body() = {};
  request_.prepare_payload();
}

template <typename Proxy>
void jjgames_socket<Proxy>::process_response(std::string const &message_body) {
  static char const *const already_registered{
      "%E8%AF%A5%E6%89%8B%E6%9C%BA%E5%8F%B7%E5%B7%B2%E6%B3%A8%E5%86%8C%EF%"
      "BC%8C%E8%AF%B7%E6%9B%B4%E6%8D%A2"};
  static const char *const not_registered{
      "%E5%B8%90%E6%88%B7%E5%8F%AF%E4%BB%A5%E4%BD%BF%E7%94%A8"};
  static char const *const blocked{
      "%E6%93%8D%E4%BD%9C%E5%BC%82%E5%B8%B8%EF%BC%8C%E8%AF%B7%E7%A8%8D%E5%"
      "90%8E%E9%87%8D%E8%AF%95"};
  static char const *const blocked_2{
      "%E8%AE%BF%E9%97%AE%E5%BC%82%E5%B8%B8%EF%BC%8C%E8%AF%B7%E7%A8%8D%E5%"
      "90%8E%E5%86%8D%E8%AF%95"};
  static char const *const blocked_3{
      "%E7%99%BB%E5%BD%95%E5%90%8D%E9%9D%9E%E6%B3%95"};
  try {
    // badly formed JSON response, blame the server
    json json_response = json::parse(message_body);
    json::object_t object = json_response.get<json::object_t>();
    std::string const msg = object["MSG"].get<json::string_t>();
    if (msg.find(not_registered) != std::string::npos) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (msg.find(already_registered) != std::string::npos) {
      signal_(search_result_type_e::Registered, current_number_);
    } else if (msg.find(blocked) != std::string::npos ||
               msg.find(blocked_2) != std::string::npos ||
               msg.find(blocked_3) != std::string::npos) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyBlocked);
      return this->choose_next_proxy();
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (std::exception const &e) {
    signal_(search_result_type_e::Unknown, current_number_);
  }

  current_number_.clear();
  this->send_next();
}

template <typename Proxy>
void jjgames_socket<Proxy>::data_received(beast::error_code ec,
                                          std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
    }
    return this->choose_next_proxy();
  }
  std::size_t const status_code = response_.result_int();
  if (status_code == 407) {
    this->set_authentication_header();
    return this->choose_next_proxy();
  }

  auto &response_body{response_.body()};

  std::size_t opening_brace_index = response_body.find_first_of('{');
  std::size_t closing_brace_index = response_body.find_last_of('}');
  if ((opening_brace_index == std::string::npos ||
       closing_brace_index == std::string::npos)) {
    return this->choose_next_proxy();
  }
  std::string const body_temp(std::string(
      response_body.begin() + static_cast<int>(opening_brace_index),
      response_body.begin() + static_cast<int>(closing_brace_index + 1)));
  return process_response(body_temp);
}

} // namespace wudi_server
