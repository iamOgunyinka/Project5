#include "lazada_http.hpp"

namespace woody_server {

void lazada_http_t::onDataReceived(beast::error_code, std::size_t) const {}

void lazada_http_t::prepareRequestData(bool) {
  auto &request = m_httpRequest.emplace();
}
} // namespace woody_server
