#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <iostream>
#include <memory>

namespace net = boost::asio;
namespace beast = boost::beast;
using tcp = net::ip::tcp;

using string_request = beast::http::request<beast::http::string_body>;
using string_response = beast::http::response<beast::http::string_body>;

class server : public std::enable_shared_from_this<server> {
  net::io_context &io_context_;
  tcp::acceptor tcp_acceptor_;
  tcp::endpoint tcp_endpoint_;
  bool is_open_ = false;

public:
  server(net::io_context &io_, std::string const &ip_address,
         uint16_t const port)
      : io_context_{io_}, tcp_acceptor_{net::make_strand(io_)},
        tcp_endpoint_{net::ip::make_address(ip_address), port} {
    beast::error_code ec{};
    tcp_acceptor_.open(tcp_endpoint_.protocol(), ec);
    if (ec) {
      std::cerr << "Unable to open acceptor because: " << ec.message()
                << std::endl;
      return;
    }
    tcp_acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      std::cerr << "Unable to set option because: " << ec.message()
                << std::endl;
      return;
    }
    tcp_acceptor_.bind(tcp_endpoint_, ec);
    if (ec) {
      std::cerr << "Unable to bind address because: " << ec.message()
                << std::endl;
      return;
    }
    ec = {};
    tcp_acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      std::cerr << "Unable to listen to listen on that port because: "
                << ec.message() << std::endl;
      return;
    }
    is_open_ = true;
  }
  void run();
  void on_connection_accepted(beast::error_code, tcp::socket socket);
};

void server::run() {
  if (!is_open_)
    return;
  tcp_acceptor_.async_accept(
      net::make_strand(io_context_),
      std::bind(&server::on_connection_accepted, shared_from_this(),
                std::placeholders::_1, std::placeholders::_2));
}

class session : public std::enable_shared_from_this<session> {
  beast::tcp_stream tcp_stream_;
  beast::flat_buffer read_buffer_;
  beast::http::request_parser<beast::http::empty_body> empty_body_parser_;
  std::shared_ptr<void> resp_;

public:
  session(tcp::socket &&socket) : tcp_stream_{std::move(socket)} {}
  void run();
  void on_data_read(beast::error_code, std::size_t);
  void send_response(string_response &&res);
  void on_data_written(beast::error_code ec, std::size_t);
  string_response make_response(std::string const &);
};

void session::on_data_written(beast::error_code ec, std::size_t const) {
  if (ec == beast::http::error::end_of_stream) {
    beast::error_code ec{};
    beast::get_lowest_layer(tcp_stream_)
        .socket()
        .shutdown(net::socket_base::shutdown_both, ec);
    return;
  } else if (ec) {
    std::cerr << ec.message() << std::endl;
    return;
  }
  run();
}

string_response session::make_response(std::string const &str) {
  string_response response{beast::http::status::ok, string_request{}.version()};
  response.set(beast::http::field::content_type, "application/txt");
  response.keep_alive(false);
  response.body() = str;
  response.prepare_payload();
  return response;
}

void session::run() {
  read_buffer_.consume(read_buffer_.size());
  beast::get_lowest_layer(tcp_stream_).expires_after(std::chrono::seconds(5));
  beast::http::async_read(tcp_stream_, read_buffer_, empty_body_parser_,
                          std::bind(&session::on_data_read, shared_from_this(),
                                    std::placeholders::_1,
                                    std::placeholders::_2));
}

void session::send_response(string_response &&response) {
  auto resp = std::make_shared<string_response>(std::move(response));
  resp_ = resp;
  beast::http::async_write(
      tcp_stream_, *resp,
      beast::bind_front_handler(&session::on_data_written, shared_from_this()));
}

void session::on_data_read(beast::error_code const ec, std::size_t const) {
  if (ec == beast::http::error::end_of_stream) {
    beast::error_code ec{};
    beast::get_lowest_layer(tcp_stream_)
        .socket()
        .shutdown(net::socket_base::shutdown_both, ec);
    return;
  } else if (ec) {
    std::cerr << ec.message() << std::endl;
    return;
  }
  std::string remote_ep =
      tcp_stream_.socket().remote_endpoint().address().to_string();
  return send_response(make_response(remote_ep));
}

void server::on_connection_accepted(beast::error_code const ec,
                                    tcp::socket socket) {
  if (ec) {
    std::cerr << "On connection accepted: " << ec.message() << std::endl;
  } else {
    std::make_shared<session>(std::move(socket))->run();
  }
  run();
}

int main(int argc, char **argv) {
  if (argc != 3)
    return -1;
  std::string const ip_address{argv[1]};
  uint16_t ip_port = std::stoi(argv[2]);

  net::io_context context{};
  std::make_shared<server>(context, ip_address, ip_port)->run();
  context.run();

  return 0;
}
