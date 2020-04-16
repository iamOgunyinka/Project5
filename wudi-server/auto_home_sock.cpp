#include "auto_home_sock.hpp"
#include <iostream>
#include <map>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

enum Constants { PROXY_REQUIRES_AUTHENTICATION = 407 };

namespace wudi_server {
using utilities::request_handler;
using namespace fmt::v6::literals;

std::string const auto_home_socket_t::auto_home_hostname_ =
    "account.autohome.com.cn";

std::string const auto_home_socket_t::password_base64_hash{
    "bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw=="};

auto_home_socket_t::auto_home_socket_t(bool &stopped,
                                       net::io_context &io_context,
                                       proxy_provider_t &proxy_provider,
                                       utilities::number_stream_t &numbers,
                                       ssl::context &ssl_context)
    : socks5_https_socket_base_t(stopped, io_context, proxy_provider, numbers,
                                 ssl_context) {}

auto_home_socket_t::~auto_home_socket_t() {}

std::string auto_home_socket_t::hostname() const { return auto_home_hostname_; }

void auto_home_socket_t::prepare_request_data(bool use_authentication_header) {
  request_.clear();
  request_.method(beast::http::verb::post);
  request_.version(11);
  request_.target("/AccountApi/CheckPhone");
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic " + password_base64_hash);
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, "account.autohome.com.cn");
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

void auto_home_socket_t::on_data_received(beast::error_code ec,
                                          std::size_t const) {

  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
                                                    305, 306, 307, 308};
  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    }
    return choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if (utilities::status_in_codes(status_code, redirect_codes)) {
    current_proxy_assign_prop(ProxyProperty::ProxyBlocked);
    return choose_next_proxy();
  }

  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    set_authentication_header();
    return connect();
  }

  auto &body{response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');

    if (status_code != 200 || opening_brace_index == std::string::npos) {
      return choose_next_proxy();
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
          return choose_next_proxy();
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
      return choose_next_proxy();
    }

  } catch (...) {
    return choose_next_proxy();
  }
  current_number_.clear();
  send_next();
}
} // namespace wudi_server
