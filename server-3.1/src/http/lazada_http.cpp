#include "lazada_http.hpp"
#include <spdlog/spdlog.h>

namespace woody_server {
namespace utilities {
std::string getRandomUserAgent();
std::string getRandomString(size_t);
} // namespace utilities

void lazada_http_t::onDataReceived(beast::error_code const ec, size_t const) {
#ifdef _DEBUG
  spdlog::info("Received(lazada_http_t): {}", m_httpResponse->body());
#endif
  sendNext();
}

void lazada_http_t::prepareRequestData(bool const use_auth) {
  using http::field;

  static auto const &fullyQualifiedName = fqn();
  static auto const path =
      fmt::format("{}/user/api/login?_bx-v=2.2.3", fullyQualifiedName);
  static auto const referer =
      fmt::format("{}/user/login?spm=a2o4l.login_signup.0.0.53ac3e17eX2YqX&"
                  "redirect=https://www.lazada.com.ph/",
                  fullyQualifiedName);

  auto const payload =
      fmt::format("{\"lzdAppVersion\": \"1.0\",\"loginName\":\""
                  "{}\",\"password\":\"{}\"}",
                  m_currentPhoneNumber, utilities::getRandomString(10));
#ifdef _DEBUG
  spdlog::info("Payload(lazada_http_t): {}", payload);
#endif

  auto &request = m_httpRequest.emplace();
  request.method(http::verb::post); // post request
  request.target(path);             // path
  request.version(11);              // HTTP v1.1
  request.set(field::host, hostname());
  request.set(field::accept_encoding, "gzip, deflate, br");
  request.set(field::accept, "application/json, text/plain, */*");
  request.set(field::user_agent, utilities::getRandomUserAgent());
  request.set(field::connection, "keep-alive");
  request.set(field::referer, referer);
  request.set(field::origin, fullyQualifiedName);

  if (use_auth)
    request.set(field::authorization, "");
  request.body() = payload;
  request.prepare_payload();
}
} // namespace woody_server
