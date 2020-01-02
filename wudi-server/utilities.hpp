#pragma once
#include <vector>
#include <string>
#include <array>
#include <optional>
#include <boost/utility/string_view.hpp>
#include <boost/beast.hpp>
#include <spdlog/fmt/ostr.h>

#define OTL_BIG_INT long long
#define OTL_ODBC_MYSQL
#define OTL_STL
#define OTL_ODBC_WINDOWS
#define OTL_SAFE_EXCEPTION_ON
#include <otlv4/otlv4.h>


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
			std::string database_config_filename;
		};

		struct DbConfig
		{
			std::string username_;
			std::string password_;
			std::string database_name_;

			operator bool()
			{
				return !( username_.empty() && password_.empty() && database_name_.empty() );
			}
		};
		
		struct UploadResult
		{
			std::size_t upload_id;
			std::string filename;
			std::string upload_alias;
		};

		otl_stream& operator>>( otl_stream& os, UploadResult& );
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

		void display_sql_error( otl_exception const& exception );
		std::string view_to_string( boost::string_view const& str_view );
		[[nodiscard]] DbConfig parse_database_file( std::string const& filename, std::string const& config_name );
	}

	using Callback = std::function<void( http::request<http::string_body> const&, std::string_view const& )>;

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
		void add_endpoint( std::string const&, std::initializer_list<http::verb>, Callback&& );
		std::optional<Endpoint::iterator> get_rules( std::string const& target );
		std::optional<iterator> get_rules( boost::string_view const& target );
	};

	struct DatabaseConnector
	{
		utilities::DbConfig db_config;
		otl_connect otl_connector_;
		bool is_running = false;
	private:
		std::string svector_to_string( std::vector<boost::string_view> const& vec );
		void keep_sql_server_busy();
	public:
		static std::shared_ptr<DatabaseConnector> GetDBConnector();
		DatabaseConnector& username( std::string const& username );
		DatabaseConnector& password( std::string const& password );
		DatabaseConnector& database_name( std::string const& db_name );
		bool connect();

	public:
		std::vector<utilities::UploadResult> get_uploads( std::vector<boost::string_view> const& );
		std::pair<int, int> get_login_role( std::string const& username, std::string const& password );
	};
}