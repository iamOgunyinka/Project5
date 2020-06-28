#include "qunar_socket.hpp"

namespace wudi_server {

std::string qunar_http_socket_t::hostname() const { return "user.qunar.com"; }

void qunar_http_socket_t::prepare_request_data(bool use_authentication_header) {
  char const *target = "https://user.qunar.com/ajax/validator.jsp";
  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, "user.qunar.com:443");
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept,
               "application/json, text/javascript, */*; q=0.01");
  request_.keep_alive(true);
  request_.set(http::field::referer, "https://user.qunar.com/passport/"
                                     "register.jsp?ret=https%3A%2F%2Fwww.qunar."
                                     "com%2F%3Fex_track%3Dauto_4e0d874a");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.set(http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = "method=" + current_number_ + "&prenum=86&vcode=null";
  request_.prepare_payload();
}

void qunar_http_socket_t::data_received(beast::error_code ec,
                                        std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if ((status_code / 100) == 3) {
    current_proxy_assign_prop(proxy_base_t::Property::ProxyBlocked);
    return this->choose_next_proxy();
  }
  if (status_code == 400) {
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  try {
    json::object_t root_object = json::parse(response_.body());
    auto const error_code =
        root_object["errCode"].get<json::number_integer_t>();
    if (error_code == 21017) {
      current_proxy_assign_prop(proxy_base_t::Property::ProxyToldToWait);
      this->current_proxy_->time_last_used = std::time(nullptr);
      return this->choose_next_proxy();
    } else if (error_code == 21006) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (error_code == 11009) {
      signal_(search_result_type_e::Registered, current_number_);
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (std::exception const &) {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  current_number_.clear();
  send_next();
}

std::string qunar_socks5_socket_t::hostname() const { return "user.qunar.com"; }

void qunar_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  char const *target = "/ajax/validator.jsp";
  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, "user.qunar.com");
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept,
               "application/json, text/javascript, */*; q=0.01");
  request_.set(http::field::referer, "https://user.qunar.com/passport/"
                                     "register.jsp?ret=https%3A%2F%2Fwww.qunar."
                                     "com%2F%3Fex_track%3Dauto_4e0d874a");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.set(http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = "method=" + current_number_ + "&prenum=86&vcode=null";
  request_.prepare_payload();
}

void qunar_socks5_socket_t::data_received(beast::error_code ec,
                                          std::size_t const) {

  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if ((status_code / 100) == 3) {
    current_proxy_assign_prop(proxy_base_t::Property::ProxyBlocked);
    return this->choose_next_proxy();
  }
  if (status_code == 400) {
    return signal_(search_result_type_e::RequestStop, current_number_);
  }
  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  try {
    json::object_t root_object = json::parse(response_.body());
    auto const error_code =
        root_object["errCode"].get<json::number_integer_t>();
    if (error_code == 21017) {
      this->current_proxy_->time_last_used = std::time(nullptr);
      this->current_proxy_assign_prop(proxy_base_t::Property::ProxyToldToWait);
      return this->choose_next_proxy();
    } else if (error_code == 21006) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (error_code == 11009) {
      signal_(search_result_type_e::Registered, current_number_);
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (std::exception const &) {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  current_number_.clear();
  send_next();
}
} // namespace wudi_server
