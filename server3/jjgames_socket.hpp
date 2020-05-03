#pragma once

#include "socks5_https_socket_base.hpp"
#include <spdlog/spdlog.h>

namespace wudi_server {

using utilities::request_handler;
using namespace fmt::v6::literals;

struct time_data_t {
  uint64_t current_time{};
  uint64_t callback_number{};
};

template <typename Proxy>
class jjgames_socket
    : public socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy> {
  static std::string jjgames_hostname;
  std::size_t success_sent_count_{};
  void process_response(std::string const &);

  using socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy>::request_;
  using socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy>::signal_;
  using socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy>::response_;
  using socks5_https_socket_base_t<jjgames_socket<Proxy>,
                                   Proxy>::current_number_;

public:
  jjgames_socket(bool &stopped, net::io_context &, Proxy &,
                 utilities::number_stream_t &, net::ssl::context &);
  ~jjgames_socket() {}
  void send_next() override;
  void prepare_request_data(bool use_auth = false);
  void data_received(beast::error_code, std::size_t const);
  std::string hostname() const;
};

template <typename Proxy>
std::string jjgames_socket<Proxy>::jjgames_hostname{"a4.srv.jj.cn"};

template <typename Proxy>
jjgames_socket<Proxy>::jjgames_socket(bool &stopped,
                                      net::io_context &io_context,
                                      Proxy &proxy_provider,
                                      utilities::number_stream_t &numbers,
                                      net::ssl::context &ssl_context)
    : socks5_https_socket_base_t<jjgames_socket<Proxy>, Proxy>{
          stopped, io_context, proxy_provider, numbers, ssl_context} {}

template <typename Proxy> std::string jjgames_socket<Proxy>::hostname() const {
  return jjgames_hostname;
}

time_data_t get_time_data() {
  static std::random_device rd{};
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<> dis(0.0, 1.0);
  uint64_t const current_time = std::time(nullptr) * 1'000;
  std::size_t const random_number =
      static_cast<std::size_t>(std::round(1e3 * dis(gen)));
  std::uint64_t const callback_number =
      static_cast<std::size_t>(current_time + random_number);
  return time_data_t{current_time, callback_number};
}

template <typename Proxy>
void jjgames_socket<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  auto const time_data = get_time_data();
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
  try {
    // badly formed JSON response, blame the server
    json json_response = json::parse(message_body);
    json::object_t object = json_response.get<json::object_t>();
    bool const status = object["REV"].get<json::boolean_t>();
    if (status) {
      signal_(search_result_type_e::NotRegistered, current_number_);
      // return get_form_hash();
    } else {
      static char const *const already_registered{
          "%E8%AF%A5%E6%89%8B%E6%9C%BA%E5%8F%B7%E5%B7%B2%E6%B3%A8%E5%86%8C%EF%"
          "BC%8C%E8%AF%B7%E6%9B%B4%E6%8D%A2"};
      static char const *const blocked{
          "%E6%93%8D%E4%BD%9C%E5%BC%82%E5%B8%B8%EF%BC%8C%E8%AF%B7%E7%A8%8D%E5%"
          "90%8E%E9%87%8D%E8%AF%95"};
      static char const *const blocked_2{
          "%E8%AE%BF%E9%97%AE%E5%BC%82%E5%B8%B8%EF%BC%8C%E8%AF%B7%E7%A8%8D%E5%"
          "90%8E%E5%86%8D%E8%AF%95"};
      static char const *const blocked_3{
          "%E7%99%BB%E5%BD%95%E5%90%8D%E9%9D%9E%E6%B3%95"};
      std::string const server_message = object["MSG"].get<json::string_t>();
      if (server_message.find(already_registered) != std::string::npos) {
        signal_(search_result_type_e::Registered, current_number_);
      } else if (server_message.find(blocked) != std::string::npos ||
                 server_message.find(blocked_2) != std::string::npos ||
                 server_message.find(blocked_3) != std::string::npos) {
        this->current_proxy_assign_prop(ProxyProperty::ProxyBlocked);
        return this->choose_next_proxy();
      } else {
        return this->choose_next_proxy();
      }
    }
  } catch (std::exception const &e) {
    spdlog::error("exception in process_normal_response: {}", e.what());
    return this->choose_next_proxy();
  }

  ++success_sent_count_;
  current_number_.clear();
  this->send_next();
}

template <typename Proxy> void jjgames_socket<Proxy>::send_next() {
  if (this->stopped_) {
    if (!current_number_.empty()) {
      this->numbers_.push_back(current_number_);
    }
    current_number_.clear();
    return;
  }
  try {
    current_number_ = this->numbers_.get();
    prepare_request_data();
    if (success_sent_count_ == 20) {
      success_sent_count_ = 0;
      return this->choose_next_proxy();
    }
    this->send_https_data();
  } catch (utilities::empty_container_exception_t &) {
  }
}

template <typename Proxy>
void jjgames_socket<Proxy>::data_received(beast::error_code ec,
                                          std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      this->current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    }
    return this->choose_next_proxy();
  }
  std::size_t const status_code = response_.result_int();
  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    this->set_authentication_header();
    return this->choose_next_proxy();
  }

  auto &response_body{response_.body()};

  using utilities::search_result_type_e;
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