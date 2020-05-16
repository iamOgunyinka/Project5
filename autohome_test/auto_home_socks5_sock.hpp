#pragma once

#include "socks5_https_socket_base.hpp"
#include <array>
#include <nlohmann/json.hpp>

namespace wudi_server {
namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;
using nlohmann::json;

template <typename Proxy>
class auto_home_socks5_socket_t
    : public socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                        Proxy> {
  using socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                   Proxy>::signal_;

  using socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                   Proxy>::current_proxy_str_;
  using socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                   Proxy>::request_;
  using socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                   Proxy>::response_;
  using socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                   Proxy>::send_next;
  using socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                   Proxy>::current_number_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  auto_home_socks5_socket_t<Proxy>(ssl::context &ssl_context, Args &&... args)
      : socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>, Proxy>(
            ssl_context, std::forward<Args>(args)...) {}
  ~auto_home_socks5_socket_t() {}
  std::string hostname() const;
};

template <typename Proxy>
std::string auto_home_socks5_socket_t<Proxy>::hostname() const {
  return "account.autohome.com.cn";
}

template <typename Proxy>
void auto_home_socks5_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  char const *target = "/AccountApi/CheckPhone";
  request_.clear();
  request_.method(beast::http::verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(beast::http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(beast::http::field::connection, "keep-alive");
  request_.set(beast::http::field::host, hostname());
  request_.set(beast::http::field::cache_control, "no-cache");
  request_.set(beast::http::field::user_agent,
               "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:67.0) Gecko/20100101 "
               "Firefox/67.0");
  request_.set(beast::http::field::accept, "*/*");
  request_.keep_alive(true);
  request_.set(beast::http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = "isOverSea=0&phone=" + current_number_ + "&validcodetype=1";
  request_.prepare_payload();
}

template <std::size_t N>
bool status_in_codes(std::size_t const code,
                     std::array<std::size_t, N> const &codes) {
  for (auto const &stat_code : codes)
    if (code == stat_code)
      return true;
  return false;
}

template <typename Proxy>
void auto_home_socks5_socket_t<Proxy>::data_received(beast::error_code ec,
                                                     std::size_t const) {
  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
                                                    305, 306, 307, 308};
  if (ec) {
    if (ec != http::error::end_of_stream) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if (status_in_codes(status_code, redirect_codes)) {
    spdlog::info("{} proxy blocked", current_proxy_str_);
    this->current_proxy_assign_prop(Proxy::Property::ProxyBlocked);
    return this->choose_next_proxy();
  }
  if (status_code == 400) {
    return;
  }
  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  auto &body{response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');

    if (status_code != 200 || opening_brace_index == std::string::npos) {
      signal_(current_proxy_str_);
      return send_next();
    } else {
      if (closing_brace_index == std::string::npos) {
        signal_(current_proxy_str_);
        return send_next();
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          signal_(current_proxy_str_);
          return send_next();
        }
      }
    }
  }

  try {
    json::object_t object = document.get<json::object_t>();
    if (object.find("success") != object.end()) {
      std::string const msg = object["Msg"].get<json::string_t>();
      if (msg == "Msg.MobileExist" || msg == "MobileExist") {
        signal_(current_proxy_str_);
      } else if (msg == "Msg.MobileSuccess" || msg == "MobileSuccess") {
        signal_(current_proxy_str_);
      } else if (msg == "Msg.MobileNotExist" || msg == "MobileNotExist") {
        signal_(current_proxy_str_);
      } else {
        signal_(current_proxy_str_);
      }
    } else {
      signal_(current_proxy_str_);
    }

  } catch (...) {
    signal_(current_proxy_str_);
  }
  current_number_.clear();
  send_next();
}
} // namespace wudi_server
