#include "lazada_socks5.hpp"
#include <spdlog/spdlog.h>

namespace woody_server {
namespace utilities {
std::string getRandomUserAgent();
std::string getRandomString(size_t);
} // namespace utilities

std::string lazada_socks5_t::hostname() const { return "member.lazada.com.ph"; }

void lazada_socks5_t::prepareRequestData(bool use_authentication_header) {
  using http::field;
  auto &request = m_httpRequest.emplace();
  auto const payload =
      fmt::format("{\"lzdAppVersion\": \"1.0\",\"loginName\":\""
                  "{}\",\"password\":\"{}\"}",
                  m_currentNumber, utilities::getRandomString(10));
  request.method(http::verb::post);              // post request
  request.target("/user/api/login?_bx-v=2.2.3"); // path
  request.version(11);                           // HTTP v1.1
  request.set(field::host, hostname());
  request.set(field::accept_encoding, "gzip, deflate, br");
  request.set(field::accept, "application/json, text/plain, */*");
  request.set(field::user_agent, utilities::getRandomUserAgent());
  request.set(field::connection, "keep-alive");

  if (use_authentication_header)
    request.set(field::authorization, "");
  request.set(field::referer, "https://member.lazada.com.ph/user/login?"
                              "spm=a2o4l.login_signup.0.0.53ac3e17eX2YqX&"
                              "redirect=https://www.lazada.com.ph/");
  request.set(field::origin, "https://member.lazada.com.ph");
  request.body() = payload;
  request.prepare_payload();
}

void lazada_socks5_t::onDataReceived(beast::error_code ec, size_t const) {
//  static std::array<std::size_t, 10> redirect_codes{300, 301, 302, 303, 304,
//                                                    305, 306, 307, 308};
#ifdef _DEBUG
  spdlog::info("Received(lazada_http_t): {}", m_httpResponse->body());
#endif

  sendNext();
}

} // namespace woody_server
