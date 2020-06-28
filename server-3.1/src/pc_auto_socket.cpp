#include "pc_auto_socket.hpp"

namespace wudi_server {
std::string pc_auto_http_socket_t::hostname() const {
  return "passport3.pcauto.com.cn";
}

void pc_auto_http_socket_t::prepare_request_data(
    bool use_authentication_header) {
  std::string const target = "https://passport3.pcauto.com.cn/passport3/api/"
                             "validate_mobile.jsp?mobile=" +
                             current_number_ + "&req_enc=UTF-8";
  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, "passport3.pcauto.com.cn:443");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::accept_encoding, "gzip, deflate, br");
  request_.set(http::field::accept_language, "en-US,en;q=0.9");
  request_.set(http::field::referer,
               "https://my.pcauto.com.cn/passport/mobileRegister.jsp");
  request_.set(http::field::content_type, "application/x-www-form-urlencoded");
  request_.body() = "{}";
  request_.prepare_payload();
}

void pc_auto_http_socket_t::data_received(beast::error_code ec,
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
  auto &body = response_.body();
  try {
    if (body.find("\"desc\":\"OK\"") != std::string::npos) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (body.find("\"status\":43") != std::string::npos) {
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

std::string pc_auto_socks5_socket_t::hostname() const {
  return "passport3.pcauto.com.cn";
}

void pc_auto_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  std::string const target =
      "/passport3/api/validate_mobile.jsp?mobile=" + current_number_ +
      "&req_enc=UTF-8";
  request_.clear();
  request_.method(http::verb::post);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(http::field::connection, "keep-alive");
  request_.set(http::field::host, "passport3.pcauto.com.cn");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::accept_encoding, "gzip, deflate, br");
  request_.set(http::field::accept_language, "en-US,en;q=0.9");
  request_.set(http::field::referer,
               "https://my.pcauto.com.cn/passport/mobileRegister.jsp");
  request_.set(http::field::content_type, "application/x-www-form-urlencoded");
  request_.body() = "{}";
  request_.prepare_payload();
}

void pc_auto_socks5_socket_t::data_received(beast::error_code ec,
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

  auto &body = response_.body();
  try {
    if (body.find("\"desc\":\"OK\"") != std::string::npos) {
      signal_(search_result_type_e::NotRegistered, current_number_);
    } else if (body.find("\"status\":43") != std::string::npos) {
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
