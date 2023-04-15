#include "http_socket_base.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/write.hpp>

#include "enumerations.hpp"
#include "number_stream.hpp"

namespace woody_server {
http_socket_base_t::http_socket_base_t(bool &stopped,
                                       net::io_context &ioContext,
                                       proxy_base_t &proxyProvider,
                                       number_stream_t &numbers,
                                       int const scansPerIP)
    : socket_interface_t(ioContext, proxyProvider, numbers, stopped,
                         scansPerIP),
      m_tcpStream(std::in_place, net::make_strand(ioContext)) {}

http_socket_base_t::~http_socket_base_t() { closeSocket(); }

void http_socket_base_t::closeSocket() {
  m_signal.disconnect_all_slots();
  if (!m_tcpStream)
    return;

  m_tcpStream->cancel();
  beast::error_code ec{};
  beast::get_lowest_layer(*m_tcpStream)
      .socket()
      .shutdown(net::socket_base::shutdown_both, ec);
  ec = {};
  beast::get_lowest_layer(*m_tcpStream).socket().close(ec);
}

void http_socket_base_t::sendHttpData() {
  m_tcpStream->expires_after(std::chrono::milliseconds(TimeoutMilliseconds));
  http::async_write(*m_tcpStream, *m_httpRequest,
                    [this](beast::error_code const ec, size_t const size) {
                      onDataSent(ec, size);
                    });
}

void http_socket_base_t::onDataSent(beast::error_code const ec,
                                    [[maybe_unused]] std::size_t const s) {
  if (ec) {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  receiveData();
}

void http_socket_base_t::receiveData() {
  m_tcpStream->expires_after(
      std::chrono::milliseconds(TimeoutMilliseconds * 4)); // 4*5secs
  m_httpResponse.emplace();
  m_buffer.emplace();
  http::async_read(*m_tcpStream, *m_buffer, *m_httpResponse,
                   [this](beast::error_code const ec, size_t const size) {
                     onDataReceived(ec, size);
                   });
}

void http_socket_base_t::startConnect() {
  chooseNextProxy(true);
  if (m_currentProxy)
    sendFirstRequest();
}

void http_socket_base_t::sendFirstRequest() {
  if (m_appStopped) {
    if (!m_currentPhoneNumber.empty())
      m_mobileNumbers.append(m_currentPhoneNumber);
    return m_currentPhoneNumber.clear();
  }
  try {
    m_currentPhoneNumber = m_mobileNumbers.get();
    prepareRequestData(false);
    connect();
  } catch (empty_container_exception_t &) {
  }
}

void http_socket_base_t::sendNext() {
  if (m_appStopped) {
    if (!m_currentPhoneNumber.empty())
      m_mobileNumbers.append(m_currentPhoneNumber);
    return m_currentPhoneNumber.clear();
  }
  try {
    m_currentPhoneNumber = m_mobileNumbers.get();
    prepareRequestData(false);

    if (m_scansPerIP != 0 && m_currentProxy->numberScanned >= m_scansPerIP) {
      currentProxyAssignProperty(proxy_property_e::ProxyMaxedOut);
      return chooseNextProxy();
    }
    ++m_currentProxy->numberScanned;
    return sendHttpData();
  } catch (empty_container_exception_t &) {
  }
}

void http_socket_base_t::reconnect() {
  ++m_connectCount;
  if (m_connectCount >= MaxRetries) {
    currentProxyAssignProperty(proxy_property_e::ProxyUnresponsive);
    return chooseNextProxy();
  }
  connect();
}

void http_socket_base_t::connect() {
  if (!m_currentProxy || m_appStopped) {
    if (m_appStopped && !m_currentPhoneNumber.empty())
      m_mobileNumbers.append(m_currentPhoneNumber);
    return m_currentPhoneNumber.clear();
  }
  m_tcpStream->expires_after(std::chrono::milliseconds(TimeoutMilliseconds));
  m_tcpStream->async_connect(
      *m_currentProxy, [this](beast::error_code const ec) { onConnected(ec); });
}

void http_socket_base_t::onConnected(beast::error_code ec) {
  if (ec)
    return reconnect();
  sendHttpData();
}

void http_socket_base_t::closeStream() { m_tcpStream->close(); }

void http_socket_base_t::chooseNextProxy(bool const is_first_request) {
  m_connectCount = 0;
  m_currentProxy = m_proxyProvider.nextEndpoint();
  if (!m_currentProxy) {
    m_mobileNumbers.append(m_currentPhoneNumber);
    m_currentPhoneNumber.clear();
    return m_signal(search_result_type_e::RequestStop, m_currentPhoneNumber);
  }
  if (!is_first_request) {
    m_tcpStream->close();
    m_tcpStream.emplace(net::make_strand(m_ioContext));
    return connect();
  }
}

void http_socket_base_t::currentProxyAssignProperty(
    proxy_property_e const property) {
  if (m_currentProxy)
    m_currentProxy->property = property;
}

void http_socket_base_t::setAuthenticationHeader() { prepareRequestData(true); }

} // namespace woody_server
