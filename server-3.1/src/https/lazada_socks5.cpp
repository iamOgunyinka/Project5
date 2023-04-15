#include "lazada_socks5.hpp"

namespace woody_server {
std::string lazada_socks5_t::hostname() const {
  return "account.autohome.com.cn";
}

void lazada_socks5_t::prepareRequestData(bool use_authentication_header) {
  auto &request = m_httpRequest.emplace();
  request.clear();
  request.version(11);
  request.set(http::field::connection, "keep-alive");
  request.set(http::field::host, hostname());
  request.set(http::field::accept, "*/*");
}

void lazada_socks5_t::onDataReceived(beast::error_code ec, size_t const) {
  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
                                                    305, 306, 307, 308};
  /*
static auto const not_found_str =
    u8"\"returncode\":2010203,\"message\":\"该用户名不存在\"";
static auto const found_str = u8"\"returncode\":0";
static auto const reload_session =
    u8"\"returncode\":2010203,\"message\":\"停留时长异常";

auto& body = m_httpResponse->body();
if (body.find(not_found_str) != std::string::npos) {
  m_signal(search_result_type_e::NotRegistered, m_currentNumber);
} else if (body.find(found_str) != std::string::npos) {
  m_signal(search_result_type_e::Registered, m_currentNumber);
} else if (body.find(reload_session) != std::string::npos) {
  return clear_session_cache();
} else {
  m_signal(search_result_type_e::Unknown, current_number_);
}
   */
  sendNext();
}

} // namespace woody_server
