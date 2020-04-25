#pragma once

#include "socks5_https_socket_base.hpp"
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <spdlog/spdlog.h>

namespace wudi_server {
enum Constants { PROXY_REQUIRES_AUTHENTICATION = 407 };
using utilities::request_handler;
using namespace fmt::v6::literals;

template <typename Proxy>
class auto_home_socket_t
    : public socks5_https_socket_base_t<auto_home_socket_t<Proxy>, Proxy> {
  static std::string const password_base64_hash;
  static std::string const auto_home_hostname_;

  using socks5_https_socket_base_t<auto_home_socket_t<Proxy>, Proxy>::request_;
  using socks5_https_socket_base_t<auto_home_socket_t<Proxy>, Proxy>::signal_;
  using socks5_https_socket_base_t<auto_home_socket_t<Proxy>,
                                   Proxy>::current_number_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  auto_home_socket_t(bool &stopped, net::io_context &io, Proxy &proxy_provider,
                     utilities::number_stream_t &numbers, ssl::context &);
  ~auto_home_socket_t();
  std::string hostname() const;
};

template <typename Proxy>
std::string const auto_home_socket_t<Proxy>::auto_home_hostname_ =
    "account.autohome.com.cn";

template <typename Proxy>
std::string const auto_home_socket_t<Proxy>::password_base64_hash{
    "MTgzNzE1NTU1NDE6d2F6ZzIwMjA="};

template <typename Proxy>
auto_home_socket_t<Proxy>::auto_home_socket_t(
    bool &stopped, net::io_context &io_context, Proxy &proxy_provider,
    utilities::number_stream_t &numbers, ssl::context &ssl_context)
    : socks5_https_socket_base_t<auto_home_socket_t<Proxy>, Proxy>(
          stopped, io_context, proxy_provider, numbers, ssl_context) {}

template <typename Proxy> auto_home_socket_t<Proxy>::~auto_home_socket_t() {}

template <typename Proxy>
std::string auto_home_socket_t<Proxy>::hostname() const {
  return auto_home_hostname_;
}

template <typename Proxy>
void auto_home_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  request_.clear();
  request_.method(beast::http::verb::post);
  request_.version(11);
  request_.target("/AccountApi/CheckPhone");
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic " + password_base64_hash);
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, "account.autohome.com.cn:443");
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::accept_language, "en-US,en;q=0.5");
  request_.set(http::field::accept_encoding, "gzip, deflate, br");
  request_.set(http::field::origin, "https://account.autohome.com.cn");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::referer,
               "https://account.autohome.com.cn/register");
  request_.keep_alive(true);
  request_.set(http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() =
      "isOverSea=0&phone={}&validcodetype=1"_format(current_number_);
  request_.prepare_payload();
}

template <typename Proxy>
void auto_home_socket_t<Proxy>::data_received(beast::error_code ec,
                                              std::size_t const) {

  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
                                                    305, 306, 307, 308};
  if (ec) {
    spdlog::error(ec.message());
    if (ec != http::error::partial_message) {
      this->current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = this->response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if (utilities::status_in_codes(status_code, redirect_codes)) {
    std::string const f =
        boost::lexical_cast<std::string>(this->current_proxy_->endpoint);
    spdlog::error("{} is blocked", f);
    this->current_proxy_assign_prop(ProxyProperty::ProxyBlocked);
    return this->choose_next_proxy();
  }

  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    this->set_authentication_header();
    return this->connect();
  }

  auto &body{this->response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');

    if (status_code != 200 || opening_brace_index == std::string::npos) {
      return this->choose_next_proxy();
    } else {
      if (closing_brace_index == std::string::npos) {
        this->signal_(search_result_type_e::Unknown, current_number_);
        return this->send_next();
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          return this->choose_next_proxy();
        }
      }
    }
  }

  try {
    json::object_t object = document.get<json::object_t>();
    if (object.find("success") != object.end()) {
      std::string const msg = object["Msg"].get<json::string_t>();
      if (msg == "Msg.MobileExist" || msg == "MobileExist") {
        signal_(search_result_type_e::Registered, current_number_);
      } else if (msg == "Msg.MobileSuccess" || msg == "MobileSuccess") {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else if (msg == "Msg.MobileNotExist" || msg == "MobileNotExist") {
        signal_(search_result_type_e::Registered, current_number_);
      } else {
        signal_(search_result_type_e::Registered, current_number_);
      }
    } else {
      return this->choose_next_proxy();
    }

  } catch (...) {
    return this->choose_next_proxy();
  }
  current_number_.clear();
  this->send_next();
}

} // namespace wudi_server
