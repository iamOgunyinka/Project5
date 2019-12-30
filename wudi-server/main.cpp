#include <thread>
#include <CLI11/CLI11.hpp>

#include "server.hpp"

int main( int argc, char* argv[] )
{
	CLI::App cli_parser{ "Wu-di: an asynchronous web server for Farasha trading" };
	wudi_server::command_line_interface args{};
	int const thread_count = std::thread::hardware_concurrency();
	args.thread_count = thread_count;

	cli_parser.add_option( "-p", args.port, "port to bind server to", true );
	cli_parser.add_option( "-a", args.ip_address, "IP address to use", true );
	cli_parser.add_option( "-t", args.thread_count, "Number of threads to use", true );
	CLI11_PARSE( cli_parser, argc, argv );

	wudi_server::asio::io_context context{ thread_count };
	auto server_instance = std::make_shared<wudi_server::server>( context, args );
	server_instance->run();

	std::vector<std::thread> threads{};
	threads.reserve( args.thread_count );
	for( std::size_t counter = 0; counter < args.thread_count; ++counter ) {
		threads.emplace_back( [&] { context.run(); } );
	}
	context.run();
	return 0;
}