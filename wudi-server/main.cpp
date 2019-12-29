#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <CLI11/CLI11.hpp>
#include <iostream>
#include <memory>
#include <set>
#include <functional>

#define private_functions private

namespace wudi_server
{
	namespace http = boost::beast::http;
	namespace asio = boost::asio;
	namespace beast = boost::beast;
	using Callback = std::function<http::response<http::string_body>( http::request<http::string_body> const& )>;

	std::string decode_url( std::string const & encoded_string )
	{
		std::string src{};
		for( size_t i = 0; i < encoded_string.size(); ) {
			char c = encoded_string[i];
			if( c != '%' ) {
				src.push_back( c );
				++i;
			} else {
				char c1 = encoded_string[i + 1];
				unsigned int localui1 = 0L;
				if( '0' <= c1 && c1 <= '9' ) {
					localui1 = c1 - '0';
				} else if( 'A' <= c1 && c1 <= 'F' ) {
					localui1 = c1 - 'A' + 10;
				} else if( 'a' <= c1 && c1 <= 'f' ) {
					localui1 = c1 - 'a' + 10;
				}

				char c2 = encoded_string[i + 2];
				unsigned int localui2 = 0L;
				if( '0' <= c2 && c2 <= '9' ) {
					localui2 = c2 - '0';
				} else if( 'A' <= c2 && c2 <= 'F' ) {
					localui2 = c2 - 'A' + 10;
				} else if( 'a' <= c2 && c2 <= 'f' ) {
					localui2 = c2 - 'a' + 10;
				}

				unsigned int ui = localui1 * 16 + localui2;
				src.push_back( ui );

				i += 3;
			}
		}

		return src;
	}
	std::vector<std::string> split_string( std::string const& str )
	{
		
	}

	void handle_requests( http::request<http::string_body> const& request )
	{

	}

	struct command_line_interface
	{
		std::size_t thread_count = std::thread::hardware_concurrency();
		uint16_t port{ 3456 };
		uint16_t timeout_secs{ 15 };
		std::string ip_address{ "127.0.0.1" };
	};

	struct Endpoint
	{
		std::vector<
	};

	struct Rule
	{
		std::string route{};
		std::array<http::verb, 5> verbs{};
		Callback route_callback;
	};

	class session : public std::enable_shared_from_this<session>
	{
		beast::tcp_stream tcp_stream_;
		command_line_interface const& args_;
		beast::flat_buffer buffer_{};
		http::request<http::string_body> client_request_{};
	private_functions:
		void http_read_data()
		{
			client_request_ = {};
			//auto callback = beast::bind_front_handler( &session::on_data_read, shared_from_this() );
			beast::get_lowest_layer( tcp_stream_ ).expires_after( std::chrono::seconds( args_.timeout_secs ) );
			http::async_read( tcp_stream_, buffer_, client_request_, 
				beast::bind_front_handler( &session::on_data_read, shared_from_this() ) );
		}

		void on_data_read( beast::error_code ec, std::size_t const )
		{
			if( ec == http::error::end_of_stream ) { // end of connection

			} else if( ec ) {
				std::cerr << ec.message() << "\n";
			} else {
				handle_requests( client_request_ );
			}
		}
	public:
		session( asio::ip::tcp::socket&& socket, command_line_interface const& args ) :
			tcp_stream_{ std::move( socket ) }, args_{ args }
		{
		}
		void run()
		{
			http_read_data();
		}
	};

	class server : public std::enable_shared_from_this<server>
	{
		asio::io_context& io_context_;
		asio::ip::tcp::endpoint const endpoint_;
		asio::ip::tcp::acceptor acceptor_;
		bool is_open{ false };
		command_line_interface const& args_;
	public:
		server( asio::io_context& context, command_line_interface const& args ) :
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

		void run()
		{
			if( !is_open ) return;
			accept_connections();
		}

		void accept_connections()
		{
			acceptor_.async_accept( asio::make_strand( io_context_ ),
				beast::bind_front_handler( &server::on_connection_accepted, shared_from_this() ) );
		}

		void on_connection_accepted( beast::error_code const& ec, asio::ip::tcp::socket socket )
		{
			if( ec ) {
				std::cerr << "Error on connection: " << ec.message() << "\n";
			} else {
				std::make_shared<session>( std::move( socket ), args_ )->run();
			}
			accept_connections();
		}
	};
}

int main( int argc, char* argv[] )
{
	CLI::App cli_parser{ "Wu-di: an asynchronous web server for Farasha trading" };
	wudi_server::command_line_interface args{};
	cli_parser.add_option( "-p", args.port, "port to bind server to", true );
	cli_parser.add_option( "-a", args.ip_address, "IP address to use", true );
	cli_parser.add_option( "-t", args.thread_count, "Number of threads to use", true );
	CLI11_PARSE( cli_parser, argc, argv );

	wudi_server::asio::io_context context{};

	auto server_instance = std::make_shared<wudi_server::server>( context, args );
	server_instance->run();

	std::vector<std::thread> threads{};
	threads.reserve( args.thread_count );
	for( uint16_t counter = 0; counter < args.thread_count; ++counter ) {
		threads.emplace_back( [&] { context.run(); } );
	}
	context.run();
	return 0;
}