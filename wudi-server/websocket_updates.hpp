#pragma once

#include <boost/asio.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>

namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
namespace net = boost::asio;

namespace wudi_server {
class websocket_updates
    : public std::enable_shared_from_this<websocket_updates> {
  websocket::stream<beast::tcp_stream> websock_stream_;
  beast::flat_buffer read_buffer_;

private:
    void read_websock_data();
    void on_data_read( beast::error_code const ec, std::size_t const );
    void on_error_occurred( beast::error_code );
    void on_websocket_accepted( beast::error_code ec );
    void interpret_message( beast::flat_buffer::const_buffers_type const& data );
    void do_write( std::string const& message = {} );
public:
  websocket_updates( net::ip::tcp::socket &&tcp_socket)
      : websock_stream_{std::move(tcp_socket)} {}
  void run( beast::http::request<beast::http::empty_body> && request );
};

} // namespace wudi_server
