#include "sun_city_socket.hpp"
#include <iostream>

namespace wudi_server {

std::string sun_city_http_socket_t::hostname() const {
  return "www.hyi680.com";
}

void sun_city_http_socket_t::prepare_request_data(
    bool use_authentication_header) {
  using http::field;

  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target("https://www.hyi680.com/verifyphone.jhtml");
  if (use_authentication_header) {
    request_.set(field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(field::host, "www.hyi680.com:443");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "*/*");
  request_.set(field::content_type, "application/x-www-form-urlencoded");
  request_.body() = "phone=" + current_number_;
  request_.prepare_payload();
}

void sun_city_http_socket_t::data_received(beast::error_code ec,
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
    static char const *const not_registered = "true";
    static char const *const registered = "false";
    if (response_.body().find(not_registered) != std::string::npos) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (response_.body().find(registered) != std::string::npos) {
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

//////////////////////////////////////////////////////////////

std::string sun_city_socks5_socket_t::hostname() const {
  return "www.hyi680.com";
}

void sun_city_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  using http::field;
  using http::verb;

  auto const target = "/verifyphone.jhtml";

  request_.clear();
  request_.method(verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(field::host, "www.hyi680.com");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "*/*");
  request_.set(field::content_type, "application/x-www-form-urlencoded");
  request_.body() = "phone=" + current_number_;
  request_.prepare_payload();
}

void sun_city_socks5_socket_t::data_received(beast::error_code ec,
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
  std::cout << response_.body() << std::endl;

  try {
    static char const *const not_registered = "true";
    static char const *const registered = "false";

    if (response_.body().find(not_registered) != std::string::npos) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (response_.body().find(registered) != std::string::npos) {
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
