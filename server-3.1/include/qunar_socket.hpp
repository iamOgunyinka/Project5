#pragma once

#include "http_socket_base.hpp"
#include "socks5_https_socket_base.hpp"

namespace wudi_server {

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using namespace fmt::v6::literals;

template <typename Proxy>
class qunar_http_socket_t
    : public http_socket_base_t<qunar_http_socket_t<Proxy>, Proxy> {
  using super_class = http_socket_base_t<qunar_http_socket_t<Proxy>, Proxy>;

  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::send_next;
  using super_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  qunar_http_socket_t<Proxy>(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~qunar_http_socket_t() {}
  std::string hostname() const;
};

template <typename Proxy>
std::string qunar_http_socket_t<Proxy>::hostname() const {
  return "user.qunar.com";
}

template <typename Proxy>
void qunar_http_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
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
  request_.body() = "method={}&prenum=86&vcode=null"_format(current_number_);
  request_.prepare_payload();
}

template <typename Proxy>
void qunar_http_socket_t<Proxy>::data_received(beast::error_code ec,
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
    json::object_t root_object = json::parse(response_.body());
    auto const error_code =
        root_object["errCode"].get<json::number_integer_t>();
    if (error_code == 21017) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyToldToWait);
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

//////////////////////////////////////////////////////////////

template <typename Proxy>
class qunar_socks5_socket_t
    : public socks5_https_socket_base_t<qunar_socks5_socket_t<Proxy>, Proxy> {
  using super_class =
      socks5_https_socket_base_t<qunar_socks5_socket_t<Proxy>, Proxy>;

  using super_class::current_number_;
  using super_class::request_;
  using super_class::response_;
  using super_class::send_next;
  using super_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  qunar_socks5_socket_t<Proxy>(Args &&... args)
      : super_class(std::forward<Args>(args)...) {}
  ~qunar_socks5_socket_t() {}
  std::string hostname() const;
};

template <typename Proxy>
std::string qunar_socks5_socket_t<Proxy>::hostname() const {
  return "user.qunar.com";
}

template <typename Proxy>
void qunar_socks5_socket_t<Proxy>::prepare_request_data(
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
  request_.body() = "method={}&prenum=86&vcode=null"_format(current_number_);
  request_.prepare_payload();
}

template <typename Proxy>
void qunar_socks5_socket_t<Proxy>::data_received(beast::error_code ec,
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
    json::object_t root_object = json::parse(response_.body());
    auto const error_code =
        root_object["errCode"].get<json::number_integer_t>();
    if (error_code == 21017) {
      this->current_proxy_->time_last_used = std::time(nullptr);
      this->current_proxy_assign_prop(Proxy::Property::ProxyToldToWait);
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
