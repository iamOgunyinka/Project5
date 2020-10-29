#include "auto_home_http_sock.hpp"
#include "safe_proxy.hpp"

namespace wudi_server {
void auto_home_http_socket_t::prepare_request_data(
    bool use_authentication_header) {
  char const *address =
      "https://account.autohome.com.cn/password/checkusername";
  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target(address);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, "account.autohome.com.cn:443");
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(
      http::field::referer,
      "https://account.autohome.com.cn/password/find?backurl=https%253A"
      "%252F%252Fwww.autohome.com.cn%252Fbeijing%252F");
  request_.keep_alive(true);
  request_.set(http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = "username=" + current_number_ + "&usertype=2&";
  request_.prepare_payload();
}

void auto_home_http_socket_t::data_received(beast::error_code ec,
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

  static auto const not_found_str =
      u8"\"returncode\":2010203,\"message\":\"该用户名不存在\"";
  static char const *const found_str = "\"returncode\":0";

  if (body.find(not_found_str) != std::string::npos) {
    signal_(search_result_type_e::NotRegistered, current_number_);
  } else if (body.find(found_str) != std::string::npos) {
    signal_(search_result_type_e::Registered, current_number_);
  } else {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  current_number_.clear();
  send_next();
}
} // namespace wudi_server
