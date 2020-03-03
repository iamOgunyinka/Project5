#include "server.hpp"
#include "session.hpp"
#include <boost/lexical_cast.hpp>
#include <spdlog/spdlog.h>

namespace wudi_server {
server::server(asio::io_context &context, command_line_interface const &args)
    : io_context_{context}, endpoint_{asio::ip::make_address(args.ip_address),
                                      args.port},
      acceptor_{asio::make_strand(io_context_)}, args_{args} {
  beast::error_code ec{}; // used when we don't need to throw all around
  acceptor_.open(endpoint_.protocol(), ec);
  if (ec) {
    spdlog::error("Could not open socket: {}", ec.message());
    return;
  }
  acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec) {
    spdlog::error("set_option failed: {}", ec.message());
    return;
  }
  acceptor_.bind(endpoint_, ec);
  if (ec) {
    spdlog::error("binding failed: {}", ec.message());
    return;
  }
  acceptor_.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) {
    spdlog::error("not able to listen: {}", ec.message());
    return;
  }
  is_open = true;
}

void server::run() {
  if (!is_open)
    return;
  accept_connections();
}

void server::on_connection_accepted(beast::error_code const &ec,
                                    asio::ip::tcp::socket socket) {
  if (ec) {
    spdlog::error("error on connection: {}", ec.message());
  } else {
    if (sessions_.size() > 20) {
      auto beg = std::remove_if(sessions_.begin(), sessions_.end(),
                                [](std::shared_ptr<session> &session) {
                                  return session->is_closed();
                                });
      if (beg != sessions_.end()) {
        sessions_.erase(beg, sessions_.end());
      }
    }
    spdlog::info("connected to: {}",
                 boost::lexical_cast<std::string>(socket.remote_endpoint()));
    sessions_.push_back(
        std::make_shared<session>(io_context_, std::move(socket)));
    sessions_.back()->run();
  }
  accept_connections();
}
void server::accept_connections() {
  acceptor_.async_accept(
      asio::make_strand(io_context_),
      beast::bind_front_handler(&server::on_connection_accepted,
                                shared_from_this()));
}
} // namespace wudi_server
