#include "PP_sports.hpp"
#include <spdlog/spdlog.h>
#include <string>
#include <vector>
#include <fstream>
enum Constants { PROXY_REQUIRES_AUTHENTICATION = 407 };

namespace wudi_server {
using utilities::request_handler;
using namespace fmt::v6::literals;

char const *const pp_sports_t::address_ =
    "http://api.passport.pptv.com/checkLogin";

std::string const pp_sports_t::password_base64_hash{
    "bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw=="};

pp_sports_t::pp_sports_t(bool &stopped, net::io_context &io_context,
                         proxy_provider_t &proxy_provider,
                         utilities::number_stream_t &numbers)
    : web_base(stopped, io_context, proxy_provider, numbers) {}

pp_sports_t::~pp_sports_t() {}

void pp_sports_t::prepare_request_data(bool use_authentication_header) {
  std::string address =
      "http://api.passport.pptv.com/checkLogin?cb="
      "checklogin&loginid={}&sceneFlag=1&channel=208000103001&format=jsonp"_format(
          current_number_);
  request_.clear();
  request_.method(beast::http::verb::get);
  request_.version(11);
  request_.target(address_);
  if (use_authentication_header) {
    request_.set(beast::http::field::proxy_authorization,
                 "Basic " + password_base64_hash);
  }
  request_.keep_alive(true);
  request_.set(beast::http::field::host,
               utilities::uri{address_}.host() + ":80");
  request_.set(beast::http::field::cache_control, "no-cache");
  request_.set(beast::http::field::user_agent, utilities::get_random_agent());
  request_.set(beast::http::field::accept, "*/*");
  request_.set(beast::http::field::content_type,
               "application/x-www-form-urlencoded; charset=UTF-8");
  request_.body() = {};
  request_.prepare_payload();
}

void pp_sports_t::on_data_received(beast::error_code ec, std::size_t const) {

  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
      tcp_stream_.close();
    }
    choose_next_proxy();
    return connect();
  }

  std::size_t const status_code = response_.result_int();

  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    set_authentication_header();
    return connect();
  }

  auto &body{response_.body()};
  std::cout << body << std::endl;
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');

    if (status_code != 200 || opening_brace_index == std::string::npos) {
      signal_(search_result_type_e::Unknown, current_number_);
      return send_next();
    } else {
      if (closing_brace_index == std::string::npos) {
        signal_(search_result_type_e::Unknown, current_number_);
        return send_next();
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          signal_(search_result_type_e::Unknown, current_number_);
          return send_next();
        }
      }
    }
  }

  try {
    json::object_t object = document.get<json::object_t>();
    if (object.find("errorCode") != object.end()) {
      std::string const error_code = object["errorCode"].get<json::string_t>();
      if (error_code == "0") {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else if (error_code == "5") {
        signal_(search_result_type_e::Registered, current_number_);
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
  send_next();
}
} // namespace wudi_server
