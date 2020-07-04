#include "pp_sports.hpp"

namespace wudi_server {

void pp_sports_http_socket_t::prepare_request_data(
    bool use_authentication_header) {
  std::string const target =
      "http://api.passport.pptv.com/checkLogin?cb=checklogin&loginid=" +
      current_number_ + "&sceneFlag=1&channel=208000103001&format=jsonp";
  request_.clear();
  request_.method(http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.keep_alive(true);
  request_.set(http::field::host, "api.passport.pptv.com:80");
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = {};
  request_.prepare_payload();
}

void pp_sports_http_socket_t::data_received(beast::error_code ec,
                                            std::size_t const) {
  if (ec) {
    if (ec != beast::http::error::end_of_stream) {
      this->current_proxy_assign_prop(
          proxy_base_t::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();

  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  auto &body{response_.body()};
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
            signal_(search_result_type_e::Registered, current_number_);
          } else {
            signal_(search_result_type_e::Registered2, current_number_);
          }
        } else {
          signal_(search_result_type_e::Registered, current_number_);
        }
      } else if (error_code == "5") {
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

void pp_sports_socks5_socket_t::data_received(beast::error_code ec,
                                              std::size_t const) {
  if (ec) {
    if (ec != beast::http::error::end_of_stream) {
      this->current_proxy_assign_prop(
          proxy_base_t::Property::ProxyUnresponsive);
      this->close_stream();
    }
    return this->choose_next_proxy();
  }

  std::size_t const status_code = response_.result_int();

  if (status_code == 407) {
    this->set_authentication_header();
    return this->connect();
  }

  auto &body{response_.body()};
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
            signal_(search_result_type_e::Registered, current_number_);
          } else {
            signal_(search_result_type_e::Registered2, current_number_);
          }
        } else {
          signal_(search_result_type_e::Registered, current_number_);
        }
      } else if (error_code == "5") {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else {
        signal_(search_result_type_e::Unknown, current_number_);
      }
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (...) {
    this->close_stream();
    return this->choose_next_proxy();
  }
  current_number_.clear();
  this->send_next();
}

void pp_sports_socks5_socket_t::prepare_request_data(
    bool use_authentication_header) {
  std::string target = "/checkLogin?cb=checklogin&loginid=" + current_number_ +
                       "&sceneFlag=1&channel=208000103001&format=jsonp";

  request_.clear();
  request_.method(http::verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(http::field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.keep_alive(true);
  request_.set(http::field::host, "api.passport.pptv.com");
  request_.set(http::field::cache_control, "no-cache");
  request_.set(http::field::user_agent, utilities::get_random_agent());
  request_.set(http::field::accept, "*/*");
  request_.set(http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = {};
  request_.prepare_payload();
}
} // namespace wudi_server
