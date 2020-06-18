#pragma once

#include "http_socket_base.hpp"
#include "socks5_https_socket_base.hpp"
#include <fstream>
#include <iostream>

namespace wudi_server {
namespace net = boost::asio;

using tcp = boost::asio::ip::tcp;
using namespace fmt::v6::literals;

template <typename Proxy>
class pc_auto_http_socket_t
    : public http_socket_base_t<pc_auto_http_socket_t<Proxy>, Proxy> {
  using super_class = http_socket_base_t<pc_auto_http_socket_t<Proxy>, Proxy>;

  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::send_next;
  using super_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  pc_auto_http_socket_t<Proxy>(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~pc_auto_http_socket_t() {}
  std::string hostname() const;
};

template <typename Proxy>
std::string pc_auto_http_socket_t<Proxy>::hostname() const {
  return "passport3.pcauto.com.cn";
}

template <typename Proxy>
void pc_auto_http_socket_t<Proxy>::prepare_request_data(
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

template <typename Proxy>
void pc_auto_http_socket_t<Proxy>::data_received(beast::error_code ec,
                                                 std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if ((status_code / 100) == 3) {
    this->current_proxy_assign_prop(Proxy::Property::ProxyBlocked);
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

//////////////////////////////////////////////////////////////

template <typename Proxy>
class pc_auto_socks5_socket_t
    : public socks5_https_socket_base_t<pc_auto_socks5_socket_t<Proxy>, Proxy> {
  using super_class =
      socks5_https_socket_base_t<pc_auto_socks5_socket_t<Proxy>, Proxy>;

  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::send_next;
  using super_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  pc_auto_socks5_socket_t<Proxy>(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~pc_auto_socks5_socket_t() {}
  std::string hostname() const;
};

template <typename Proxy>
std::string pc_auto_socks5_socket_t<Proxy>::hostname() const {
  return "passport3.pcauto.com.cn";
}

template <typename Proxy>
void pc_auto_socks5_socket_t<Proxy>::prepare_request_data(
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

template <typename Proxy>
void pc_auto_socks5_socket_t<Proxy>::data_received(beast::error_code ec,
                                                   std::size_t const) {

  if (ec) {
    if (ec != http::error::end_of_stream) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if ((status_code / 100) == 3) {
    this->current_proxy_assign_prop(Proxy::Property::ProxyBlocked);
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
