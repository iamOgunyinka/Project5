#pragma once

#include "http_socket_base.hpp"
#include "socks5_http_socket_base.hpp"

namespace wudi_server {

using namespace fmt::v6::literals;

template <typename Proxy>
class pp_sports_http_socket_t
    : public http_socket_base_t<pp_sports_http_socket_t<Proxy>, Proxy> {
public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  pp_sports_http_socket_t(Args &&... args)
      : http_socket_base_t<pp_sports_http_socket_t<Proxy>, Proxy>(
            std::forward<Args>(args)...) {}
  ~pp_sports_http_socket_t {}
  std::string hostname() const { return "api.passport.pptv.com"; }
};

template <typename Proxy>
class pp_sports_socks5_socket_t
    : public socks5_http_socket_base_t<pp_sports_http_socket_t<Proxy>, Proxy> {
public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  pp_sports_socks5_socket_t(Args &&... args)
      : socks5_http_socket_base_t<pp_sports_http_socket_t<Proxy>, Proxy>(
            std::forward<Args>(args)...) {}
  ~pp_sports_socks5_socket_t() {}
  std::string hostname() const { return "api.passport.pptv.com"; }
};

template <typename Proxy>
void pp_sports_http_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  std::string const target =
      "http://api.passport.pptv.com/checkLogin?cb=checklogin&loginid={}"
      "&sceneFlag=1&channel=208000103001&format=jsonp"_format(
          this->current_number_);
  this->request_.clear();
  this->request_.method(beast::http::verb::get);
  this->request_.version(11);
  this->request_.target(target);
  if (use_authentication_header) {
    this->request_.set(beast::http::field::proxy_authorization,
                       "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  this->request_.keep_alive(true);
  this->request_.set(beast::http::field::host, "api.passport.pptv.com:80");
  this->request_.set(beast::http::field::cache_control, "no-cache");
  this->request_.set(beast::http::field::user_agent,
                     utilities::get_random_agent());
  this->request_.set(beast::http::field::accept, "*/*");
  this->request_.set(beast::http::field::content_type,
                     "application/x-www-form-urlencoded; charset=UTF-8");
  this->request_.body() = {};
  this->request_.prepare_payload();
}

template <typename Proxy>
void pp_sports_http_socket_t<Proxy>::data_received(beast::error_code ec,
                                                   std::size_t const) {
  if (ec) {
    if (ec != beast::http::error::end_of_stream) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = this->response_.result_int();

  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  auto &body{this->response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');
    // we possibly got 477(Client error), ignore, choose a new proxy
    if (status_code != 200 || opening_brace_index == std::string::npos) {
      return this->choose_next_proxy();
    } else {
      if (closing_brace_index == std::string::npos) {
        return this->choose_next_proxy();
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          return this->choose_next_proxy();
        }
      }
    }
  }

  try {
    json::object_t object = document.get<json::object_t>();
    if (object.find("errorCode") != object.end()) {
      std::string const error_code = object["errorCode"].get<json::string_t>();
      if (error_code == "0") {
        if (object.find("status") != object.end()) {
          std::string const status = object["status"].get<json::string_t>();
          if (status == "1") {
            this->signal_(search_result_type_e::Registered,
                          this->current_number_);
          } else {
            this->signal_(search_result_type_e::Registered2,
                          this->current_number_);
          }
        } else {
          this->signal_(search_result_type_e::Registered,
                        this->current_number_);
        }
      } else if (error_code == "5") {
        this->signal_(search_result_type_e::NotRegistered,
                      this->current_number_);
      } else {
        this->signal_(search_result_type_e::Unknown, this->current_number_);
      }
    } else {
      this->signal_(search_result_type_e::Unknown, this->current_number_);
    }
  } catch (...) {
    this->close_stream();
    return this->choose_next_proxy();
  }
  this->current_number_.clear();
  this->send_next();
}

template <typename Proxy>
void pp_sports_socks5_socket_t<Proxy>::data_received(beast::error_code ec,
                                                     std::size_t const) {
  if (ec) {
    if (ec != beast::http::error::end_of_stream) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = this->response_.result_int();

  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  auto &body{this->response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');
    // we possibly got 477(Client error), ignore, choose a new proxy
    if (status_code != 200 || opening_brace_index == std::string::npos) {
      return this->choose_next_proxy();
    } else {
      if (closing_brace_index == std::string::npos) {
        return this->choose_next_proxy();
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          return this->choose_next_proxy();
        }
      }
    }
  }

  try {
    json::object_t object = document.get<json::object_t>();
    if (object.find("errorCode") != object.end()) {
      std::string const error_code = object["errorCode"].get<json::string_t>();
      if (error_code == "0") {
        if (object.find("status") != object.end()) {
          std::string const status = object["status"].get<json::string_t>();
          if (status == "1") {
            this->signal_(search_result_type_e::Registered,
                          this->current_number_);
          } else {
            this->signal_(search_result_type_e::Registered2,
                          this->current_number_);
          }
        } else {
          this->signal_(search_result_type_e::Registered,
                        this->current_number_);
        }
      } else if (error_code == "5") {
        this->signal_(search_result_type_e::NotRegistered,
                      this->current_number_);
      } else {
        this->signal_(search_result_type_e::Unknown, this->current_number_);
      }
    } else {
      this->signal_(search_result_type_e::Unknown, this->current_number_);
    }
  } catch (...) {
    this->close_stream();
    return this->choose_next_proxy();
  }
  this->current_number_.clear();
  this->send_next();
}

template <typename Proxy>
void pp_sports_socks5_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  std::string target =
      "/checkLogin?cb=checklogin&loginid={}&sceneFlag=1&channel=20800010"
      "3001&format=jsonp"_format(current_number_);

  this->request_.clear();
  this->request_.method(beast::http::verb::get);
  this->request_.version(11);
  this->request_.target(target);
  if (use_authentication_header) {
    this->request_.set(beast::http::field::proxy_authorization,
                       "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  this->request_.keep_alive(true);
  this->request_.set(beast::http::field::host, hostname());
  this->request_.set(beast::http::field::cache_control, "no-cache");
  this->request_.set(beast::http::field::user_agent,
                     utilities::get_random_agent());
  this->request_.set(beast::http::field::accept, "*/*");
  this->request_.set(beast::http::field::content_type,
                     "application/x-www-form-urlencoded; charset=UTF-8");
  this->request_.body() = {};
  this->request_.prepare_payload();
}
} // namespace wudi_server
