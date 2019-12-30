#include "utilities.hpp"
#include <vector>

namespace wudi_server
{
	namespace utilities
	{
		std::string decode_url( boost::string_view const& encoded_string )
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

		std::vector<std::string> split_string( boost::string_view const& str, char const* delimeter )
		{
			std::string::size_type from_pos{};
			std::string::size_type index{ str.find( delimeter, from_pos ) };
			if( index == std::string::npos ) return std::vector<std::string>{ std::string{ str.begin(), str.end() }};
			std::vector<std::string> result{};
			while( index != std::string::npos ) {
				result.push_back( std::string{ str.begin() + from_pos, str.begin() + ( index - from_pos ) } );
				from_pos = index + 1;
				index = str.find( delimeter, from_pos );
			}
			if( from_pos < str.length() ) result.push_back( std::string{ str.cbegin() + from_pos, str.cend() } );
			return result;
		}
	}

	Rule::Rule( std::initializer_list<http::verb>&& verbs, Callback callback ) : 
		num_verbs_{ verbs.size() }, route_callback_{ std::move( callback ) }
	{
		if( verbs.size() > 5 ) throw std::runtime_error{ "maximum number of verbs is 5" };
		for( int i = 0; i != verbs.size(); ++i ) {
			verbs_[i] = *( verbs.begin() + i );
		}
	}
	void Endpoint::add_endpoint( std::string const& route, std::initializer_list<http::verb> verbs, Callback&& callback )
	{
		if( route.empty() || route[0] != '/' ) throw std::runtime_error{ "A valid route starts with a /" };
		endpoints.emplace( route, Rule{ std::move( verbs ), std::move( callback ) } );
	}
	
	std::optional<Endpoint::iterator> Endpoint::get_rules( std::string const& target )
	{
		auto iter = endpoints.find( target );
		if( iter == endpoints.end() ) return std::nullopt;
		return iter;
	}
}