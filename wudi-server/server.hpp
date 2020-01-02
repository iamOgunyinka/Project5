#pragma once
#include <memory>
#include <list>
#include "session.hpp"

#define private_functions private

namespace wudi_server
{
	using utilities::command_line_interface;
	
	class server : public std::enable_shared_from_this<server>
	{
		asio::io_context& io_context_;
		asio::ip::tcp::endpoint const endpoint_;
		asio::ip::tcp::acceptor acceptor_;
		bool is_open{ false };
		command_line_interface const& args_;
		std::list<std::shared_ptr<session>> sessions_;
		std::shared_ptr<DatabaseConnector> db_;
	public:
		server( asio::io_context& context, command_line_interface const& args, std::shared_ptr<DatabaseConnector>db );
		void run();
	private_functions:
		void accept_connections();
		void on_connection_accepted( beast::error_code const& ec, asio::ip::tcp::socket socket );
	};
}

#undef private_functions