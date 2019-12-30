#include "server.hpp"
#include <iostream>

namespace wudi_server
{
	server::server( asio::io_context& context, command_line_interface const& args ) :
		io_context_{ context }, endpoint_{ asio::ip::make_address( args.ip_address ), args.port },
		acceptor_{ asio::make_strand( io_context_ ) }, args_{ args }
	{
		beast::error_code ec{}; // used when we don't need to throw all around
		acceptor_.open( endpoint_.protocol(), ec );
		if( ec ) {
			std::cerr << "could not open socket: " << ec.message() << "\n";
			return;
		}
		acceptor_.set_option( asio::socket_base::reuse_address( true ), ec );
		if( ec ) {
			std::cerr << "set_option failed: " << ec.message() << "\n";
			return;
		}
		acceptor_.bind( endpoint_, ec );
		if( ec ) {
			std::cerr << "bind failed: " << ec.message() << "\n";
			return;
		}
		acceptor_.listen( asio::socket_base::max_listen_connections, ec );
		if( ec ) {
			std::cerr << "not listening: " << ec.message() << "\n";
			return;
		}
		is_open = true;
	}

	void server::run()
	{
		if( !is_open ) return;
		accept_connections();
	}

	void server::on_connection_accepted( beast::error_code const& ec, asio::ip::tcp::socket socket )
	{
		if( ec ) {
			std::cerr << "Error on connection: " << ec.message() << "\n";
		} else {
			sessions_.push_back( std::make_shared<session>( std::move( socket ), args_ ) );
			sessions_.back()->run();
		}
		accept_connections();
	}
	void server::accept_connections()
	{
		acceptor_.async_accept( asio::make_strand( io_context_ ),
			beast::bind_front_handler( &server::on_connection_accepted, shared_from_this() ) );
	}
}