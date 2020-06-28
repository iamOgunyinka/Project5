#include "chinese_macau_socket.hpp"

namespace wudi_server {

std::string chinese_macau_http_socket_t::hostname() const {
  return "18850i.com";
}

void chinese_macau_http_socket_t::prepare_request_data(
    bool use_authentication_header) {
  using http::field;

  char const *const target = "https://18850i.com/?c=Register&a=Verify";
  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(field::connection, "keep-alive");
  request_.set(field::host, "18850i.com:80");
  request_.set(field::origin, "http://18850i.com");
  request_.set(field::accept_language, "en-US,en;q=0.9");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "application/json, text/javascript, */*; q=0.01");
  request_.set(field::referer, "http://18850i.com/?c=Register");
  request_.set(field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.body() = "v=" + current_number_ + "&type=telphone";
  request_.prepare_payload();
}

void chinese_macau_http_socket_t::data_received(beast::error_code ec,
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

  try {
    if (response_.body().find("\"200\"") != std::string::npos) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (response_.body().find("\"300\"") != std::string::npos) {
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

std::string chinese_macau_socks5_socket_t::hostname() const {
  return "18850i.com";
}

void chinese_macau_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  using http::field;
  using http::verb;

  char const *const target = "/?c=Register&a=Verify";
  request_.clear();
  request_.method(verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(field::connection, "keep-alive");
  request_.set(field::host, "18850i.com");
  request_.set(field::origin, "http://18850i.com");
  request_.set(field::accept_language, "en-US,en;q=0.9");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "application/json, text/javascript, */*; q=0.01");
  request_.set(field::referer, "http://18850i.com/?c=Register");
  request_.set(field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.body() = "v=" + current_number_ + "&type=telphone";
  request_.prepare_payload();
}

void chinese_macau_socks5_socket_t::data_received(beast::error_code ec,
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

  try {
    if (response_.body().find("\"200\"") != std::string::npos) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (response_.body().find("\"300\"") != std::string::npos) {
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
