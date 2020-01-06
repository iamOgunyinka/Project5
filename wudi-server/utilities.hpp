#pragma once
#include <vector>
#include <deque>
#include <string>
#include <array>
#include <optional>
#include <sstream>
#include <boost/utility/string_view.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#define OTL_BIG_INT long long
#define OTL_ODBC_MYSQL
#define OTL_STL
#define OTL_ODBC_WINDOWS
#define OTL_SAFE_EXCEPTION_ON
#include <otlv4/otlv4.h>

namespace fmt
{
	template<>
	struct formatter<boost::string_view>
	{
		template<typename ParseContext>
		constexpr auto parse( ParseContext& ctxt )
		{
			return ctxt.begin();
		}

		template<typename FormatContext>
		auto format( boost::string_view const& view, FormatContext& ctxt )
		{
			return format_to( ctxt.out(), "{}", std::string_view{ view.data(), view.size() } );
		}
	};

	template<>
	struct formatter<std::vector<std::size_t>>
	{
		template<typename ParseContext>
		constexpr auto parse( ParseContext& ctxt )
		{
			return ctxt.begin();
		}

		template<typename FormatContext>
		auto format( std::vector<std::size_t> const& integer_list, FormatContext& ctx )
		{
			if( integer_list.empty() ) {
				return format_to( ctx.out(), "" );
			}
			std::ostringstream stream{};
			for( std::size_t i = 0; i < integer_list.size() - 1; ++i ) {
				stream << integer_list[i] << ", ";
			}
			stream << integer_list.back();
			return format_to( ctx.out(), "{}", stream.str() );
		}
	};
}

namespace wudi_server
{
	namespace http = boost::beast::http;
	namespace net = boost::asio;

	using nlohmann::json;
	using namespace fmt::v6::literals;
	struct DatabaseConnector;

	namespace utilities
	{
		enum class SearchResultType
		{
			Registered = 0xA,
			NotRegistered = 0xB,
			Unknown = 0XF
		};

		enum class ErrorType
		{
			NoError = 0,
			ResourceNotFound = 1,
			RequiresUpdate = 2,
			BadRequest,
			ServerError,
			MethodNotAllowed,
			Unauthorized
		};

		enum Constants
		{
			SleepTimeoutSec = 5,
			WorkerThreadCount = 10,
			FiftyMegabytes = 1024 * 1024 * 50
		};

		enum Anonymous
		{
			LEN_USER_AGENTS = 18,
			MAX_OPEN_SOCKET = 0x100,
			MaxRetries = 3,
			Timeout = 3'000
		};

		struct ScheduledTask
		{
			std::size_t task_id{};
			std::size_t progress{};
			std::size_t scheduler_id{};
			std::size_t scheduled_dt{};
			std::vector<std::size_t> website_ids{};
			std::vector<std::size_t> number_ids{};
			std::string last_processed_number{};
		};

		struct command_line_interface
		{
			std::size_t thread_count{};
			uint16_t port{ 3456 };
			uint16_t timeout_mins{ 15 };
			std::string ip_address{ "127.0.0.1" };
			std::string scheduled_snapshot;
			std::string database_config_filename{ "../scripts/config/database.ini" };
		};

		struct DbConfig
		{
			std::string username;
			std::string password;
			std::string db_dns;

			operator bool()
			{
				return !( username.empty() && password.empty() && db_dns.empty() );
			}
		};

		struct UploadResult
		{
			std::size_t upload_id;
			std::size_t total_numbers;
			std::string upload_date;
			std::string filename;
		};

		struct UploadRequest
		{
			boost::string_view const upload_filename;
			boost::string_view const name_on_disk;
			boost::string_view const uploader_id;
			boost::string_view const upload_date;
			std::size_t const total_numbers;
		};

		struct TaskResult
		{
			std::size_t id;
			std::size_t progress;
			std::string website_ids;
			std::string numbers;
			std::string scheduler_username;
			std::string scheduled_date;
		};
		
		struct WebsiteResult
		{
			std::size_t id{};
			std::string address{};
			std::string alias;
		};

		struct ProxyAddress
		{
			std::string ip{};
			std::string port{};
		};

		struct request_handler
		{
			static std::array<char const*, LEN_USER_AGENTS> const user_agents;
		};

		struct uri
		{
			uri( std::string const& url_s );
			std::string path() const;
			std::string host() const;
		private:
			void parse( std::string const& );
			std::string host_;
			std::string path_;
			std::string protocol_;
			std::string query_;
		};

		struct empty_container_exception : public std::runtime_error
		{
			empty_container_exception() : std::runtime_error( "" ) {}
		};

		template<typename T>
		struct threadsafe_vector
		{
		private:
			std::mutex mutex_{};
			std::deque<T> container_{};
			std::size_t total_{};
		public:
			threadsafe_vector( std::vector<T>&& container ) : container_{ std::move( container ) },
				total_{ container_.size() }{
			}
			threadsafe_vector() = default;
			threadsafe_vector& operator=( threadsafe_vector const& ) = delete;
			threadsafe_vector& operator=( threadsafe_vector&& ) = delete;
			threadsafe_vector( threadsafe_vector const& ) = delete;
			threadsafe_vector( threadsafe_vector&& ) = delete;

			T get()
			{
				std::lock_guard<std::mutex> lock{ mutex_ };
				if( container_.empty() ) throw empty_container_exception{};
				T value = container_.front();
				container_.pop_front();
				--total_;
				return value;
			}

			void push_back( T&& data )
			{
				std::lock_guard<std::mutex> lock_{ mutex_ };
				container_.push_back( std::forward<T>( data ) );
				total_ = container_.size();
			}

			std::size_t get_total() const
			{
				return total_;
			}
		};

		template<std::size_t N>
		bool status_in_codes( std::size_t const code, std::array<std::size_t, N> const& codes )
		{
			for( auto const& stat_code : codes ) if( code == stat_code ) return true;
			return false;
		}

		template<typename Container, typename ...IterList>
		bool any_of( Container const& container, IterList&& ...iter_list )
		{
			return ( ... || ( std::cend( container ) == iter_list ) );
		}

		otl_stream& operator>>( otl_stream&, UploadResult& );
		otl_stream& operator>>( otl_stream&, TaskResult& );
		otl_stream& operator>>( otl_stream&, WebsiteResult& );
		std::string decode_url( boost::string_view const& encoded_string );
		std::vector<boost::string_view> split_string( boost::string_view const& str, char const* delimeter );
		void to_json( json& j, UploadResult const& item );
		void to_json( json& j, TaskResult const& );
		void to_json( json& j, WebsiteResult const& );
		void log_sql_error( otl_exception const& exception );
		[[nodiscard]] std::string view_to_string( boost::string_view const& str_view );
		[[nodiscard]] DbConfig parse_database_file( std::string const& filename, std::string const& config_name );
		std::string get_random_agent();
		void background_task_executor( std::atomic_bool& stopped, std::mutex&, std::shared_ptr<DatabaseConnector>& );
		std::deque<ScheduledTask>& get_scheduled_tasks();
		int timet_to_string( std::string&, std::size_t, char const* = "%Y-%m-%d %H:%M:%S" );
		bool read_task_file( std::string_view );
	}

	using Callback = std::function<void( http::request<http::string_body> const&, std::string_view const& )>;

	struct Rule
	{
		std::size_t num_verbs_{};
		std::array<http::verb, 3> verbs_{};
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
		std::vector<utilities::WebsiteResult> get_websites( std::vector<std::size_t> const& ids );
		bool add_website( std::string_view const address, std::string_view const alias );
		bool add_task( utilities::ScheduledTask& task );
		std::vector<utilities::TaskResult> get_all_tasks();
		std::pair<int, int> get_login_role( std::string_view const, std::string_view const );
		bool add_upload( utilities::UploadRequest const& upload_request );

		template<typename T>
		std::vector<utilities::UploadResult> get_uploads( std::vector<T> const& ids )
		{
			std::string sql_statement{};
			if( ids.empty() ) {
				sql_statement = "SELECT id, filename, total_numbers, upload_date FROM tb_uploads";
			} else {
				if constexpr( std::is_same_v<T, boost::string_view> ) {
					sql_statement = "SELECT id, filename, total_numbers, upload_date FROM tb_uploads WHERE id IN "
						"({})"_format( svector_to_string( ids ) );
				} else {
					sql_statement = "SELECT id, filename, total_numbers, upload_date FROM tb_uploads WHERE id IN "
						"({})"_format( ids );
				}
			}
			std::vector<utilities::UploadResult> result{};
			try {
				otl_stream db_stream( 1'000'000, sql_statement.c_str(), otl_connector_ );
				utilities::UploadResult item{};
				while( db_stream >> item ) {
					result.push_back( std::move( item ) );
				}
			}
			catch( otl_exception const& e ) {
				utilities::log_sql_error( e );
			}
			return result;
		}
	};
}