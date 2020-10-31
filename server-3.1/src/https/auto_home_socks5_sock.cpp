#include "auto_home_socks5_sock.hpp"

namespace wudi_server {
std::string auto_home_socks5_socket_t::hostname() const {
  return "account.autohome.com.cn";
}

void auto_home_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  request_.clear();
  request_.version(11);
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, hostname());
  request_.set(http::field::accept, "*/*");
  if (user_agent.empty()) {
    user_agent = utilities::get_random_agent();
  }
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(http::field::user_agent, user_agent);
  request_.keep_alive(true);

  if (request_type == request_type_e::PostRequest) {
    char const *const target = "/password/checkusername";
    request_.method(http::verb::post);
    request_.target(target);
    request_.set(
        http::field::referer,
        "https://account.autohome.com.cn/password/find?backurl=https%253A"
        "%252F%252Fwww.autohome.com.cn%252Fbeijing%252F");
    request_.set(http::field::content_type,
                 "application/x-www-form-urlencoded; charset=UTF-8");
    request_.set(http::field::cookie, session_id);
    request_.body() = "username=" + current_number_ + "&usertype=2&";
    request_.prepare_payload();
  } else {
    char const *const target = "/password/find";
    request_.method(http::verb::get);
    request_.target(target);
  }
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
    if (session_used_count >= 300) {
      session_used_count = 0;
      request_type = request_type_e::GetRequest;
      prepare_request_data(false);
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
    return this->choose_next_proxy();
    //return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  if (request_type == request_type_e::GetRequest) {
    auto const cookie = response_[http::field::set_cookie];
    return process_get_response(cookie);
  }
  auto &body{response_.body()};
  return process_post_response(body);
}

void auto_home_socks5_socket_t::clear_session_cache() {
  session_id.clear();
  user_agent.clear();
  request_type = request_type_e::GetRequest;
  ++session_id_request_count;

  if (session_id_request_count > 5) {
    session_id_request_count = 0;
    this->current_proxy_assign_prop(proxy_base_t::Property::ProxyBlocked);
    return this->choose_next_proxy();
  }

  prepare_request_data(false);
  send_https_data();
}

void auto_home_socks5_socket_t::process_post_response(std::string const &body) {

  static auto const not_found_str =
      u8"\"returncode\":2010203,\"message\":\"该用户名不存在\"";
  static auto const found_str = u8"\"returncode\":0";
  static auto const reload_session =
      u8"\"returncode\":2010203,\"message\":\"停留时长异常";

  if (body.find(not_found_str) != std::string::npos) {
    signal_(search_result_type_e::NotRegistered, current_number_);
  } else if (body.find(found_str) != std::string::npos) {
    signal_(search_result_type_e::Registered, current_number_);
  } else if (body.find(reload_session) != std::string::npos) {
    return clear_session_cache();
  } else {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  ++session_used_count;
  session_id_request_count = 0;
  current_number_.clear();
  send_next();
}

void auto_home_socks5_socket_t::process_get_response(
    beast::string_view const &session_id_cookie) {
  if (session_id_cookie.empty()) {
    user_agent.clear();
    session_id.clear();
    this->current_proxy_assign_prop(proxy_base_t::Property::ProxyBlocked);
    if (request_.method() == http::verb::post) {
      request_type = request_type_e::GetRequest;
      prepare_request_data(false);
    }
    return this->choose_next_proxy();
  }
  ++session_id_request_count;
  static char const *const rsession_id_str = "rsessionid=";
  static auto const length = strlen(rsession_id_str);
  auto const index = session_id_cookie.find(rsession_id_str);
  if (index == beast::string_view::npos) {
    user_agent.clear();
    session_id.clear();
    this->current_proxy_assign_prop(proxy_base_t::Property::ProxyBlocked);
    return this->choose_next_proxy();
  }
  auto const end_of_cookie_index = session_id_cookie.find(';', index + length);
  session_id =
      session_id_cookie.substr(index, end_of_cookie_index - index).to_string();

  request_type = request_type_e::PostRequest;
  prepare_request_data(false);
  send_https_data();
}
} // namespace wudi_server
