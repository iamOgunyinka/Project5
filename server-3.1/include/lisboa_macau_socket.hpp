#pragma once

#include "http_socket_base.hpp"
#include "socks5_https_socket_base.hpp"

namespace wudi_server {
namespace net = boost::asio;

using namespace fmt::v6::literals;

template <typename Proxy>
class lisboa_macau_http_socket_t
    : public http_socket_base_t<lisboa_macau_http_socket_t<Proxy>, Proxy> {
  using super_class =
      http_socket_base_t<lisboa_macau_http_socket_t<Proxy>, Proxy>;

  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::send_next;
  using super_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  lisboa_macau_http_socket_t<Proxy>(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~lisboa_macau_http_socket_t() {}
  std::string hostname() const;
};

template <typename Proxy>
std::string lisboa_macau_http_socket_t<Proxy>::hostname() const {
  return "yy99345.am";
}

template <typename Proxy>
void lisboa_macau_http_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  using http::field;

  auto target = "https://yy99345.am/Common/CheckData?DataType="
                "telephone&DataContent={}&_={}"_format(
                    current_number_, std::time(nullptr) * 1'000);
  request_.clear();
  request_.method(http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(field::connection, "keep-alive");
  request_.set(field::host, "yy99345.am:443");
  request_.set(field::accept_language, "en-US,en;q=0.9");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "application/json, text/javascript, */*; q=0.01");
  request_.set(field::referer, "https://yy99345.am/PageRegister?uid=");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.body() = {};
  request_.prepare_payload();
}

template <typename Proxy>
void lisboa_macau_http_socket_t<Proxy>::data_received(beast::error_code ec,
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

  try {
    static char const *const not_registered = "\"success\":true,\"Code\":1";
    static char const *const registered = "\"success\":true,\"Code\":0";
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

template <typename Proxy>
class lisboa_macau_socks5_socket_t
    : public socks5_https<lisboa_macau_socks5_socket_t<Proxy>, Proxy> {
  using super_class = socks5_https<lisboa_macau_socks5_socket_t<Proxy>, Proxy>;

  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::send_next;
  using super_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  lisboa_macau_socks5_socket_t<Proxy>(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~lisboa_macau_socks5_socket_t() {}
  std::string hostname() const;
};

template <typename Proxy>
std::string lisboa_macau_socks5_socket_t<Proxy>::hostname() const {
  return "yy99345.am";
}

template <typename Proxy>
void lisboa_macau_socks5_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  using http::field;
  using http::verb;

  auto target = "/Common/CheckData?DataType=telephone&DataContent"
                "={}&_={}"_format(current_number_, std::time(nullptr) * 1'000);
  request_.clear();
  request_.method(verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(field::connection, "keep-alive");
  request_.set(field::host, "yy99345.am");
  request_.set(field::accept_language, "en-US,en;q=0.9");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "application/json, text/javascript, */*; q=0.01");
  request_.set(field::referer, "https://yy99345.am/PageRegister?uid=");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.body() = {};
  request_.prepare_payload();
}

template <typename Proxy>
void lisboa_macau_socks5_socket_t<Proxy>::data_received(beast::error_code ec,
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
  try {
    static char const *const not_registered = "\"success\":true,\"Code\":1";
    static char const *const registered = "\"success\":true,\"Code\":0";
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
