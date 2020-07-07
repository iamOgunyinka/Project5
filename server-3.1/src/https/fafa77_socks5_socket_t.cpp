#include "fafa77_socks5_socket.hpp"
//#include "protocol.hpp"
#include <iostream>

namespace wudi_server {
using namespace fmt::v6::literals;

std::string fafa77_socks5_socket_t::hostname() const {
  return "www.fafa77.com";
}

void fafa77_socks5_socket_t::prepare_preliminary_request() {
  request_.method(http::verb::get);
  request_.version(11);
  request_.target("/reg");
  request_.set(http::field::host, hostname());
  request_.set(http::field::user_agent,
               "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
               "(KHTML, like Gecko) Chrome/83.0.4103.116 Safari/537.36");
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::referer, "https://www.fafa77.com/reg");
  request_.body() = {};
  request_.prepare_payload();
}

void fafa77_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  request_.clear();

  if (bearer_token.empty() || proxy_endpoint != current_proxy_->endpoint_) {
    proxy_endpoint = current_proxy_->endpoint_;
    if (!bearer_token.empty()) {
      bearer_token.clear();
    }
    session_cookie.clear();
    return prepare_preliminary_request();
  }

  auto const time_data = get_time_data();
  std::string const md5_hash =
      utilities::md5("{}"_format(time_data.current_time));
  auto const seconds = time_data.current_time / 1'000;
  std::string const cookie = "{}Hm_lvt_{}={}; Hm_lpvt_{}={}"_format(
      (session_cookie.empty() ? "" : session_cookie + "; "), md5_hash, seconds,
      md5_hash, seconds);

  request_.method(http::verb::post);
  request_.version(11);
  request_.target("/api/System/check");
  request_.set(http::field::host, hostname());
  request_.set(http::field::content_type, "application/x-www-form-urlencoded");
  request_.set(http::field::user_agent,
               "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
               "(KHTML, like Gecko) Chrome/83.0.4103.116 Safari/537.36");
  request_.set(http::field::accept, "application/json, text/plain, */*");
  request_.set(http::field::referer, "https://www.fafa77.com/reg");
  // request_.set(http::field::origin, "https://www.fafa77.com");
  request_.set(http::field::authorization, "Bearer " + bearer_token);
  request_.set(http::field::cookie, cookie);
  request_.body() = "key=mobile&value=" + current_number_;
  request_.prepare_payload();
}

void fafa77_socks5_socket_t::extract_bearer_token(std::string const &str) {
  if (str.empty()) {
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  static auto const meta_data = "<meta name=\"baseToken\"";
  static auto const meta_data_content = "content=\"";
  static auto const meta_data_length = strlen(meta_data);
  static auto const meta_data_content_length = strlen(meta_data_content);

  std::string::size_type pos = str.find(meta_data);
  if (pos == std::string::npos) {
    current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
    close_stream();
    return choose_next_proxy();
  }
  pos += meta_data_length;
  auto content_index = str.find(meta_data_content, pos);
  if (content_index == std::string::npos) {
    current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
    close_stream();
    return choose_next_proxy();
  }
  content_index += meta_data_content_length;
  auto const bearer_token_end = str.find('"', content_index);
  bearer_token = std::string(str.cbegin() + content_index,
                             str.cbegin() + bearer_token_end);
  session_cookie = response_[http::field::set_cookie].to_string();
  auto const first_cookie_pos = session_cookie.find(';');
  if (first_cookie_pos != std::string::npos) {
    session_cookie.erase(session_cookie.begin() + first_cookie_pos,
                         session_cookie.end());
  }
  /*
  auto cookies = utilities::split_string_view(session_cookie, ";");
  if (!cookies.empty()) {
    session_cookie = cookies[0].to_string();
  }
  */
  std::cout << session_cookie << std::endl;
  prepare_request_data(false);
  send_https_data();
}

void fafa77_socks5_socket_t::data_received(beast::error_code ec,
                                           std::size_t const) {
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
  if ((status_code / 100) == 3) {
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

  if (bearer_token.empty()) {
    return extract_bearer_token(body);
  }

  std::cout << body << std::endl;
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
    bool const status = object["status"].get<json::boolean_t>();
    auto const raw_data = object["data"];
    if (status == false || raw_data.is_null()) {
      bearer_token.clear();
      return this->choose_next_proxy();
    }
    bool const number_registered = raw_data.get<json::boolean_t>();
    if (number_registered) {
      signal_(search_result_type_e::Registered, current_number_);
    } else {
      signal_(search_result_type_e::NotRegistered, current_number_);
    }
  } catch (...) {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  current_number_.clear();
  send_next();
}
} // namespace wudi_server
