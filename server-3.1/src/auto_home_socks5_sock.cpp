#pragma once

#include "auto_home_socks5_sock.hpp"
#include "safe_proxy.hpp"

namespace wudi_server {
std::string auto_home_socks5_socket_t::hostname() const {
  return "account.autohome.com.cn";
}

void auto_home_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  char const *target = "/AccountApi/CheckPhone";
  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, hostname());
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::referer,
               "https://account.autohome.com.cn/register");
  request_.keep_alive(true);
  request_.set(http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() =
      "isOverSea=0&phone=" + current_number_ + "&validcodetype=1&";
  request_.prepare_payload();
}

void auto_home_socks5_socket_t::data_received(beast::error_code ec,
                                              std::size_t const) {
  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
                                                    305, 306, 307, 308};
  if (ec) {
    if (ec != http::error::end_of_stream) {
      this->current_proxy_assign_prop(
          proxy_base_t::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if (status_in_codes(status_code, redirect_codes)) {
    this->current_proxy_assign_prop(proxy_base_t::Property::ProxyBlocked);
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
