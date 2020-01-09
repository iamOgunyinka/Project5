#include "auto_home_sock.hpp"
#include <map>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

enum Constants { PROXY_REQUIRES_AUTHENTICATION = 407 };

namespace wudi_server {
using utilities::request_handler;
using namespace fmt::v6::literals;

std::string const auto_home_socket::password_base64_hash{
    "bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw=="};

auto_home_socket::auto_home_socket(
    net::io_context &io_context, safe_proxy &proxy_provider,
    utilities::threadsafe_vector<std::string> &numbers,
    std::string const &address, result_callback callback)
    : web_base(io_context, proxy_provider, numbers), address_{address},
      callback_{std::move(callback)} {}

void auto_home_socket::prepare_request_data(bool use_authentication_header) {
  std::string const payload{
      "phone={}&isOverSea=0&validcodetype=1"_format(current_number_)};
  post_request_.clear();
  post_request_.method(beast::http::verb::post);
  post_request_.version(11);
  post_request_.target(address_);
  post_request_.set(beast::http::field::connection, "keep-alive");
  if (use_authentication_header) {
    post_request_.set(beast::http::field::proxy_authorization,
                      "Basic " + password_base64_hash);
  }
  post_request_.set(beast::http::field::host,
                    utilities::uri{address_}.host() + ":443");
  post_request_.set(beast::http::field::cache_control, "no-cache");
  post_request_.set(beast::http::field::user_agent,
                    utilities::get_random_agent());
  post_request_.set(beast::http::field::content_type,
                    "application/x-www-form-urlencoded; charset=UTF-8");
  post_request_.body() = payload;
  post_request_.prepare_payload();
}

void auto_home_socket::result_available(SearchResultType type,
                                        std::string_view number) {
  callback_(type, number);
}

void auto_home_socket::on_data_received(beast::error_code ec,
                                        std::size_t const) {
  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
                                                    305, 306, 307, 308, 400};
  if (ec) {
    current_proxy_assign_prop(ProxyProperty::ProxyUnresponsive);
    choose_next_proxy();
    connect();
    return;
  }

  std::size_t const status_code = response_.result_int();
  // check if we've been redirected, most likely due to IP ban
  if (utilities::status_in_codes(status_code, redirect_codes)) {
    current_proxy_assign_prop(ProxyProperty::ProxyBlocked);
    choose_next_proxy();
    connect();
    return;
  }

  if (status_code == PROXY_REQUIRES_AUTHENTICATION) {
    set_authentication_header();
    connect();
    return;
  }

  auto &body{response_.body()};
  json document;
  try {
    document = json::parse(body);
  } catch (std::exception const &exception) {
    std::size_t const opening_brace_index = body.find('{');
    std::size_t const closing_brace_index = body.find('}');

    if (status_code != 200 || opening_brace_index == std::string::npos) {
      emit result_available(SearchResultType::Unknown, current_number_);
      send_next();
      return;
    } else {
      if (closing_brace_index == std::string::npos) {
        emit result_available(SearchResultType::Unknown, current_number_);
        send_next();
        return;
      } else {
        body = std::string(body.begin() + opening_brace_index,
                           body.begin() + closing_brace_index + 1);
        try {
          document = json::parse(body);
        } catch (std::exception const &) {
          emit result_available(SearchResultType::Unknown, current_number_);
          send_next();
          return;
        }
      }
    }
  }

  try {
    json::object_t object = document.get<json::object_t>();
    auto result{object["success"].get<json::number_integer_t>()};
    emit result_available(result == 0 ? SearchResultType::Registered
                                      : SearchResultType::NotRegistered,
                          current_number_);
  } catch (...) {
    emit result_available(SearchResultType::Unknown, current_number_);
  }
  send_next();
}
} // namespace wudi_server

#ifdef emit
#undef emit
#endif // emit
