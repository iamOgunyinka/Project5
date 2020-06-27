#pragma once

#include "http_socket_base.hpp"
#include "socks5_http_socket_base.hpp"

namespace wudi_server {
template <typename Proxy>
class wines_http_socket_t
    : public http_socket_base_t<wines_http_socket_t<Proxy>, Proxy> {

  using base_class = http_socket_base_t<wines_http_socket_t<Proxy>, Proxy>;
  using base_class::current_number_;
  using base_class::request_;
  using base_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  wines_http_socket_t(Args &&... args)
      : base_class(std::forward<Args>(args)...) {}
  ~wines_http_socket_t() {}
  std::string hostname() const { return "wnsr9488.com"; }
};

template <typename Proxy>
class wines_socks5_socket_t
    : public socks5_http_socket_base_t<wines_socks5_socket_t<Proxy>, Proxy> {
  using parent_class =
      socks5_http_socket_base_t<wines_socks5_socket_t<Proxy>, Proxy>;
  using parent_class::current_number_;
  using parent_class::request_;
  using parent_class::signal_;

public:
  void data_received(beast::error_code, std::size_t const);
  void prepare_request_data(bool use_authentication_header);

  template <typename... Args>
  wines_socks5_socket_t(Args &&... args)
      : parent_class(std::forward<Args>(args)...) {}
  ~wines_socks5_socket_t() {}
  std::string hostname() const { return "wnsr9488.com"; }
};

template <typename Proxy>
void wines_http_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  std::string const target =
      "http://wnsr9488.com/Common/CheckData?DataType=telephone&DataContent=" +
      current_number_ + "&_=" + std::to_string(std::time(nullptr) * 1'000);
  request_.clear();
  request_.method(beast::http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.keep_alive(true);
  request_.set(http::field::host, "wnsr9488.com:80");
  request_.set(http::field::accept_language, "en-US,en;q=0.9");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.set(http::field::referer, "http://wnsr9488.com/PageRegister?uid=");
  request_.set(http::field::accept,
               "application/json, text/javascript, */*; q=0.01");
  request_.body() = {};
  request_.prepare_payload();
}

template <typename Proxy>
void wines_http_socket_t<Proxy>::data_received(beast::error_code ec,
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
    bool const success = object["success"].get<json::boolean_t>();
    if (!success) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
      return this->choose_next_proxy();
    }
    if (object.find("Code") != object.end()) {
      auto const error_code = object["Code"].get<json::number_integer_t>();
      if (error_code == 0) {
        signal_(search_result_type_e::Registered, current_number_);
      } else if (error_code == 1) {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else {
        signal_(search_result_type_e::Unknown, current_number_);
      }
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (...) {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  current_number_.clear();
  this->send_next();
}

template <typename Proxy>
void wines_socks5_socket_t<Proxy>::prepare_request_data(
    bool use_authentication_header) {
  std::string const target =
      "/Common/CheckData?DataType=telephone&DataContent=" + current_number_ +
      "&_=" + std::to_string(std::time(nullptr) * 1'000);
  request_.clear();
  request_.method(http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.keep_alive(true);
  request_.set(http::field::host, "wnsr9488.com");
  request_.set(http::field::accept_language, "en-US,en;q=0.9");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::referer, "http://wnsr9488.com/PageRegister?uid=");
  request_.set(http::field::accept,
               "application/json, text/javascript, */*; q=0.01");
  request_.body() = {};
  request_.prepare_payload();
}

template <typename Proxy>
void wines_socks5_socket_t<Proxy>::data_received(beast::error_code ec,
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
    bool const success = object["success"].get<json::boolean_t>();
    if (!success) {
      this->current_proxy_assign_prop(Proxy::Property::ProxyUnresponsive);
      return this->choose_next_proxy();
    }
    if (object.find("Code") != object.end()) {
      auto const error_code = object["Code"].get<json::number_integer_t>();
      if (error_code == 0) {
        signal_(search_result_type_e::Registered, current_number_);
      } else if (error_code == 1) {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else {
        signal_(search_result_type_e::Unknown, current_number_);
      }
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (...) {
    signal_(search_result_type_e::Unknown, current_number_);
  }
  current_number_.clear();
  this->send_next();
}
} // namespace wudi_server
