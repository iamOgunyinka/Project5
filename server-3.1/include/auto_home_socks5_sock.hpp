#pragma once

#include "socks5_https_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;
using namespace fmt::v6::literals;

template <typename Proxy>
class auto_home_socks5_socket_t
    : public socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>,
                                        Proxy> {
  using super_class =
      socks5_https_socket_base_t<auto_home_socks5_socket_t<Proxy>, Proxy>;

  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::send_next;
  using super_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  auto_home_socks5_socket_t<Proxy>(ssl::context &ssl_context, Args &&... args)
      : super_class(ssl_context, std::forward<Args>(args)...) {}
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
  request_.set(beast::http::field::user_agent, utilities::get_random_agent());
  request_.set(beast::http::field::accept, "*/*");
  request_.set(http::field::referer,
               "https://account.autohome.com.cn/register");
  request_.keep_alive(true);
  request_.set(beast::http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() =
      "isOverSea=0&phone={}&validcodetype=1&"_format(current_number_);
  request_.prepare_payload();
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
  if (utilities::status_in_codes(status_code, redirect_codes)) {
    this->current_proxy_assign_prop(Proxy::Property::ProxyBlocked);
    return this->choose_next_proxy();
  }
  if (status_code == 400) {
    return signal_(search_result_type_e::RequestStop, current_number_);
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
      signal_(search_result_type_e::Unknown, current_number_);
      return send_next();
    } else {
      if (closing_brace_index == std::string::npos) {
        signal_(search_result_type_e::Unknown, current_number_);
        return send_next();
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          signal_(search_result_type_e::Unknown, current_number_);
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
        signal_(search_result_type_e::Registered, current_number_);
      } else if (msg == "Msg.MobileSuccess" || msg == "MobileSuccess") {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else if (msg == "Msg.MobileNotExist" || msg == "MobileNotExist") {
        signal_(search_result_type_e::Registered, current_number_);
      } else {
        signal_(search_result_type_e::Registered, current_number_);
      }
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }

  } catch (...) {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  current_number_.clear();
  send_next();
}
} // namespace wudi_server
