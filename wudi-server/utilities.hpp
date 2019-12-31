#pragma once
#include <vector>
#include <string>
#include <array>
#include <optional>
#include <boost/utility/string_view.hpp>
#include <boost/beast.hpp>

namespace wudi_server
{
	namespace http = boost::beast::http;
	namespace utilities
	{
		std::string decode_url( boost::string_view const& encoded_string );
		std::vector<boost::string_view> split_string( boost::string_view const& str, char const* delimeter );
		struct command_line_interface
		{
			std::size_t thread_count{};
			uint16_t port{ 3456 };
			uint16_t timeout_mins{ 15 };
			std::string ip_address{ "127.0.0.1" };
		};

		enum class ErrorType
		{
			NoError = 0,
			ResourceNotFound = 1,
			RequiresUpdate = 2,
			BadRequest = 3,
			ServerError = 4,
			MethodNotAllowed
		};

		enum Constants
		{
			FiftyMegabytes = 1024 * 1024 * 50
		};
	}

	using Callback = std::function<void( http::request<http::string_body> const&, std::string_view const & )>;
	
	struct Rule
	{
		std::size_t num_verbs_{};
		std::array<http::verb, 5> verbs_{};
		Callback route_callback_;

		Rule( std::initializer_list<http::verb>&& verbs, Callback callback );
	};

	class Endpoint
	{
		std::map<std::string, Rule> endpoints;
		using iterator = std::map<std::string, Rule>::iterator;
	public:
		void add_endpoint( std::string const&, std::initializer_list<http::verb>, Callback&&  );
		std::optional<Endpoint::iterator> get_rules( std::string const& target );
		std::optional<iterator> get_rules( boost::string_view const& target );
	};

}