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

std::string const auto_home_socket_t::address_ =
    "https://account.autohome.com.cn/AccountApi/CheckPhone";

std::string const auto_home_socket_t::password_base64_hash{
    "bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw=="};

auto_home_socket_t::auto_home_socket_t(bool &stopped,
                                       net::io_context &io_context,
                                       safe_proxy &proxy_provider,
                                       utilities::number_stream_t &numbers)
    : web_base(stopped, io_context, proxy_provider, numbers) {}

auto_home_socket_t::~auto_home_socket_t() {}

void auto_home_socket_t::prepare_request_data(bool use_authentication_header) {
  post_request_.clear();
  post_request_.method(beast::http::verb::post);
  post_request_.version(11);
  post_request_.target(address_);
  if (use_authentication_header) {
    post_request_.set(beast::http::field::proxy_authorization,
                      "Basic " + password_base64_hash);
  }
  post_request_.set(beast::http::field::connection, "keep-alive");
  post_request_.set(beast::http::field::host,
                    utilities::uri{address_}.host() + ":443");
  post_request_.set(beast::http::field::cache_control, "no-cache");
  post_request_.set(beast::http::field::user_agent,
                    utilities::get_random_agent());
  post_request_.set(beast::http::field::accept, "*/*");
  post_request_.set(beast::http::field::content_type,
                    "application/x-www-form-urlencoded; charset=UTF-8");
  post_request_.body() =
      "isOverSea=0&phone={}&validcodetype=1"_format(current_number_);
  post_request_.prepare_payload();
}

void auto_home_socket_t::on_data_received(beast::error_code ec,
                                          std::size_t const) {

  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
                                                    305, 306, 307, 308};
  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
      tcp_stream_.close();
    }
    choose_next_proxy();
    connect();
    return;
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if (utilities::status_in_codes(status_code, redirect_codes)) {
    current_proxy_assign_prop(ProxyProperty::ProxyBlocked);
    choose_next_proxy();
    connect();
    return;
  }

  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    set_authentication_header();
    connect();
    return;
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
      send_next();
      return;
    } else {
      if (closing_brace_index == std::string::npos) {
        signal_(search_result_type_e::Unknown, current_number_);
        send_next();
        return;
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          signal_(search_result_type_e::Unknown, current_number_);
          send_next();
          return;
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
        signal_(search_result_type_e::Unknown, current_number_);
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
