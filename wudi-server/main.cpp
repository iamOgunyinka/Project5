#include <thread>
#include <CLI11/CLI11.hpp>
#include <spdlog/spdlog.h>
#include "server.hpp"
#ifndef WUDI_BUILD_TYPE
#define WUDI_BUILD_TYPE "development"
#endif // !WUDI_BUILD_TYPE

int main( int argc, char* argv[] )
{
	CLI::App cli_parser{ "Wu-di: an asynchronous web server for Farasha trading" };
	wudi_server::command_line_interface args{};
	int const thread_count = std::thread::hardware_concurrency();
	args.thread_count = thread_count;
	
	cli_parser.add_option( "-p", args.port, "port to bind server to", true );
	cli_parser.add_option( "-a", args.ip_address, "IP address to use", true );
	cli_parser.add_option( "-t", args.thread_count, "Number of threads to use", true );
	cli_parser.add_option( "-d", args.database_config_filename, "Database config filename", false );
	CLI11_PARSE( cli_parser, argc, argv );

	auto database_connector{ wudi_server::DatabaseConnector::GetDBConnector() };
	auto db_config = wudi_server::utilities::parse_database_file( args.database_config_filename, WUDI_BUILD_TYPE );
	if( !db_config ) {
		std::cerr << "Unable to get database configuration values\n";
		return EXIT_FAILURE;
	}

	(*database_connector)
		.username( db_config.username_ )
		.password( db_config.password_ )
		.database_name( db_config.database_name_ );
	if( !database_connector->connect() ) return EXIT_FAILURE;
	wudi_server::asio::io_context context{ thread_count };
	auto server_instance = std::make_shared<wudi_server::server>( context, args, database_connector );
	server_instance->run();

	std::vector<std::thread> threads{};
	threads.reserve( args.thread_count );
	for( std::size_t counter = 0; counter < args.thread_count; ++counter ) {
		threads.emplace_back( [&] { context.run(); } );
	}
	context.run();
	return EXIT_SUCCESS;
}