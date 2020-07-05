#include "digit_casinos_socks5_http.hpp"

namespace wudi_server {
void digit_casinos_socks5_base_t::prepare_request_data(
    bool use_authentication_header) {
  using http::field;
  using http::verb;

  auto const target =
      "/Common/CheckData?DataType=telephone&DataContent=" + current_number_ +
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
  request_.set(field::host, hostname());
  request_.set(field::accept_language, "en-US,en;q=0.9");
  request_.set(field::user_agent, utilities::get_random_agent());
  request_.set(field::accept, "application/json, text/javascript, */*; q=0.01");
  request_.set(field::referer, fqn() + "/PageRegister?uid=");
  request_.set("X-Requested-With", "XMLHttpRequest");
  request_.body() = {};
  request_.prepare_payload();
}

void digit_casinos_socks5_base_t::data_received(beast::error_code ec,
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

  auto &body{response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &) {
    std::size_t const opening_brace_index = body.find_last_of('{');
    std::size_t const closing_brace_index = body.find_last_of('}');

    if (status_code != 200 || opening_brace_index == std::string::npos) {
      current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
      return this->choose_next_proxy();
    } else {
      if (closing_brace_index == std::string::npos) {
        current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
        return choose_next_proxy();
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
          return this->choose_next_proxy();
        }
      }
    }
  }
  try {
    auto object = document.get<json::object_t>();
    bool const succeeded = object["success"].get<json::boolean_t>();
    auto const code = object["Code"].get<json::number_integer_t>();
    if (succeeded) {
      if (code == 1) {
        signal_(search_result_type_e::NotRegistered, current_number_);
      } else if (code == 0) {
        signal_(search_result_type_e::Registered, current_number_);
      } else {
        signal_(search_result_type_e::Unknown, current_number_);
      }
    } else {
      signal_(search_result_type_e::Unknown, current_number_);
    }
  } catch (std::exception const &) {
    signal_(search_result_type_e::Unknown, current_number_);
    current_proxy_assign_prop(proxy_base_t::Property::ProxyUnresponsive);
    return this->choose_next_proxy();
  }
  current_number_.clear();
  send_next();
}

} // namespace wudi_server
