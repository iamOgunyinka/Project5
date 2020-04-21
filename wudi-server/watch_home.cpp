#include "watch_home.hpp"
#include <spdlog/spdlog.h>

enum Constants { PROXY_REQUIRES_AUTHENTICATION = 407 };

namespace wudi_server {
using utilities::request_handler;
using namespace fmt::v6::literals;

char const *const watch_home_t::watch_home_address =
    "http://www.xbiao.com/user/login";

std::string const watch_home_t::password_base64_hash{
    "bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw=="};

watch_home_t::watch_home_t(bool &stopped, net::io_context &io_context,
                           proxy_provider_t &proxy_provider,
                           utilities::number_stream_t &numbers)
    : http_proxy_web_base(stopped, io_context, proxy_provider, numbers) {}

watch_home_t::~watch_home_t() {}
void watch_home_t::prepare_request_data(bool use_authentication_header) {
  request_.clear();
  request_.method(beast::http::verb::get);
  request_.version(11);
  request_.target(watch_home_address);
  if (use_authentication_header) {
    request_.set(beast::http::field::proxy_authorization,
                 "Basic " + password_base64_hash);
  }
  request_.keep_alive(true);
  request_.set(beast::http::field::host,
               utilities::uri{watch_home_address}.host() + ":80");
  request_.set(beast::http::field::cache_control, "no-cache");
  request_.set(beast::http::field::user_agent, utilities::get_random_agent());
  request_.set(beast::http::field::accept, "*/*");
  request_.set(beast::http::field::referer, "http://www.xbiao.com/user/login");
  request_.set(beast::http::field::content_type,
               "application/x-www-form-urlencoded");
  request_.body() = {};
  request_.prepare_payload();
}

void watch_home_t::on_data_received(beast::error_code ec, std::size_t const) {
  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
      // tcp_stream_.close();
    }
    choose_next_proxy();
    return connect();
  }

  std::size_t const status_code = response_.result_int();
  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    set_authentication_header();
    return connect();
  }

  auto iterator_pair = response_.equal_range(http::field::set_cookie);
  if (iterator_pair.first == iterator_pair.second) {
    signal_(search_result_type_e::Unknown, current_number_);
    current_number_.clear();
    return send_next();
  }
  return send_next();
}
} // namespace wudi_server
