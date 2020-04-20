#pragma once

#include "socks5_http_socket_base.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace wudi_server {
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

template <typename Proxy>
class pp_sports_t
    : public socks5_http_socket_base_t<pp_sports_t<Proxy>, Proxy> {
  static std::string const password_base64_hash;
  static char const *const pp_sports_hostname;

  using socks5_http_socket_base_t<pp_sports_t<Proxy>, Proxy>::request_;
  using socks5_http_socket_base_t<pp_sports_t<Proxy>, Proxy>::response_;
  using socks5_http_socket_base_t<pp_sports_t<Proxy>, Proxy>::current_number_;
  using socks5_http_socket_base_t<pp_sports_t<Proxy>, Proxy>::signal_;

public:
  void on_data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);
  pp_sports_t(bool &stopped, net::io_context &io, Proxy &proxy_provider,
              utilities::number_stream_t &numbers);
  ~pp_sports_t();
  std::string hostname() const;
};

using utilities::request_handler;
using namespace fmt::v6::literals;

template <typename Proxy>
char const *const pp_sports_t<Proxy>::pp_sports_hostname =
    "api.passport.pptv.com";

template <typename Proxy>
std::string const pp_sports_t<Proxy>::password_base64_hash{
    "bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw=="};

template <typename Proxy>
pp_sports_t<Proxy>::pp_sports_t(bool &stopped, net::io_context &io_context,
                                Proxy &proxy_provider,
                                utilities::number_stream_t &numbers)
    : socks5_http_socket_base_t<pp_sports_t<Proxy>, Proxy>(
          stopped, io_context, proxy_provider, numbers) {}

template <typename Proxy> pp_sports_t<Proxy>::~pp_sports_t() {}

template <typename Proxy> std::string pp_sports_t<Proxy>::hostname() const {
  return pp_sports_hostname;
}

template <typename Proxy>
void pp_sports_t<Proxy>::prepare_request_data(bool use_authentication_header) {
  std::string address =
      "/checkLogin?cb=checklogin&loginid={}&sceneFlag=1&channel=208"
      "000103001&format=jsonp"_format(current_number_);
  request_.clear();
  request_.method(beast::http::verb::get);
  request_.version(11);
  request_.target(address);
  if (use_authentication_header) {
    request_.set(beast::http::field::proxy_authorization,
                 "Basic " + password_base64_hash);
  }
  request_.keep_alive(true);
  request_.set(beast::http::field::host, pp_sports_hostname);
  request_.set(beast::http::field::cache_control, "no-cache");
  request_.set(beast::http::field::user_agent, utilities::get_random_agent());
  request_.set(beast::http::field::accept, "*/*");
  request_.set(beast::http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = {};
  request_.prepare_payload();
}

template <typename Proxy>
void pp_sports_t<Proxy>::on_data_received(beast::error_code ec,
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
    return this->connect();
  }

  auto &body{response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');
    // we possibly got 477(Client error), ignore, choose a new proxy
    if (status_code != 200 || opening_brace_index == std::string::npos) {
      return this->choose_next_proxy();
    } else {
      if (closing_brace_index == std::string::npos) {
        return this->choose_next_proxy();
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
    if (object.find("errorCode") != object.end()) {
      std::string const error_code = object["errorCode"].get<json::string_t>();
      if (error_code == "0") {
        if (object.find("status") != object.end()) {
          std::string const status = object["status"].get<json::string_t>();
          if (status == "1") {
            signal_(search_result_type_e::Registered, current_number_);
          } else {
            signal_(search_result_type_e::Registered2, current_number_);
          }
        } else {
          signal_(search_result_type_e::Registered, current_number_);
        }
      } else if (error_code == "5") {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else {
        signal_(search_result_type_e::Unknown, current_number_);
      }
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (...) {
    return this->choose_next_proxy();
  }
  current_number_.clear();
  this->send_next();
}

} // namespace wudi_server
