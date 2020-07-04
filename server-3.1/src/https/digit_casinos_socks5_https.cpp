#include "digit_casinos_socks5_https.hpp"

namespace wudi_server {

void digit_casinos_ssocks5_base_t::prepare_request_data(
    bool use_authentication_header) {
  using http::field;
  using http::verb;

  auto const target = "/Common/CheckData?DataType=telephone"
                      "&DataContent=" +
                      current_number_ +
                      "&_=" + std::to_string(std::time(nullptr) * 1'000);
  request_.clear();
  request_.method(verb::get);
  request_.version(11);
  request_.target(target);
  if (use_authentication_header) {
    request_.set(field::proxy_authorization,
                 "Basic bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw==");
  }
  request_.set(field::connection, "keep-alive");
  request_.set(field::host, hostname() + ":" + std::to_string(port()));
  request_.set(field::accept_language, "en-US,en;q=0.9");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "application/json, text/javascript, */*; q=0.01");
  request_.set(field::referer, fqn() + "/PageRegister?uid=");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.body() = {};
  request_.prepare_payload();
}

void digit_casinos_ssocks5_base_t::data_received(beast::error_code ec,
                                                 std::size_t const) {

  if (ec) {
    if (ec != http::error::end_of_stream) {
      current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
      close_stream();
    }
    return choose_next_proxy();
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

  static char const *const not_registered = "\"success\":true,\"Code\":1";
  static char const *const registered = "\"success\":true,\"Code\":0";
  if (response_.body().find(not_registered) != std::string::npos) {
    signal_(search_result_type_e::NotRegistered, current_number_);
  } else if (response_.body().find(registered) != std::string::npos) {
    signal_(search_result_type_e::Registered, current_number_);
  } else {
    signal_(search_result_type_e::Unknown, current_number_);
    current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
    return this->choose_next_proxy();
  }
  current_number_.clear();
  send_next();
}
} // namespace wudi_server
