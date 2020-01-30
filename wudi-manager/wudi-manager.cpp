// wudi-manager.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <chrono>
#include <vector>
#include <signal.h>
#include <boost/process.hpp>

namespace bp = boost::process;

using signal_handler = void( * )( int );
bp::group group{};


bp::child get_process( bp::group & group, char const *process_name, char const * path = nullptr )
{
	if( path ) return bp::child( bp::search_path( process_name, { boost::filesystem::path{ path } } ), group );
	return bp::child( bp::search_path( process_name ), group );
}

void check_processes( std::vector<bp::child>& child_processes, bp::group& group, char const *process_name,
	char const *path )
{
	auto iter = std::remove_if( std::begin( child_processes ), std::end( child_processes ),
		[]( bp::child& child_process ) { return !child_process.running(); } );
	std::cout << std::boolalpha << ( iter == child_processes.end() ) << std::noboolalpha << std::endl;
	if( auto dead_children = std::distance( iter, std::end( child_processes ) ); dead_children != 0 ) {
		std::cout << dead_children << std::endl;
		child_processes.erase( iter, child_processes.end() );
		for( int i = 0; i != dead_children; ++i ) {
			child_processes.emplace_back( get_process( group, process_name, path ) );
		}
	}
}

void die_you_idiot( int sig_num )
{

}

int main( int argc, char** argv )
{
	if( argc < 2 ) {
		std::cerr << "Usage: " << argv[0] << " process_name [path_to_process=PATH] [worker_count=4]\n";
		return -1;
	}
	signal_handler handler = signal( SIGINT, die_you_idiot );
	if( handler == SIG_ERR ) {

	}
	char const* process_name = argv[1];
	char const* path = argc > 2 ? argv[2] : nullptr;
	int const worker_count = argc > 3 ? std::stoi( argv[3] ) : 4;
	std::vector<bp::child> child_processes{};
	child_processes.reserve( worker_count );
	for( int i = 0; i != worker_count; ++i ) {
		child_processes.emplace_back( get_process( group, process_name, path ) );
	}
	std::cout << "Waiting..." << std::endl;
	while( true ) {
		check_processes( child_processes, group, process_name, path );
		std::this_thread::sleep_for( std::chrono::milliseconds( 1'000 ) );
	}
	group.terminate();
	return 0;
}
