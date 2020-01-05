#include "utilities.hpp"
#include <vector>
#include <fstream>
#include <algorithm>
#include <random>
#include <filesystem>
#include <charconv>
#include <boost/algorithm/string.hpp>

namespace wudi_server
{
	using namespace fmt::v6::literals;

	namespace utilities
	{
		otl_stream& operator>>( otl_stream& os, UploadResult& item )
		{
			return os >> item.upload_id >> item.filename >> item.total_numbers >> item.upload_date;
		}

		otl_stream& operator>>( otl_stream& os, TaskResult& item )
		{
			return os >> item.id >> item.scheduler_username >> item.scheduled_date
				>> item.website_ids >> item.numbers >> item.progress;
		}

		otl_stream& operator>>( otl_stream& os, WebsiteResult& web )
		{
			return os >> web.id >> web.address >> web.alias;
		}

		void to_json( json& j, UploadResult const& item )
		{
			j = json{ { "id", item.upload_id }, { "date", item.upload_date },
			{ "filename", item.filename }, { "total", item.total_numbers } };
		}

		void to_json( json& j, TaskResult const& item )
		{
			j = json{ { "id", item.id }, { "progress", item.progress },
			{ "web", item.website_ids }, { "numbers", item.numbers },
			{ "username", item.scheduler_username }, { "date", item.scheduled_date } };
		}

		void to_json( json& j, WebsiteResult const&result )
		{
			j = json{ { "id", result.id }, { "alias", result.alias },
			{ "address", result.address } };
		}

		void display_sql_error( otl_exception const& exception )
		{
			spdlog::error( "SQLError code: {}", exception.code );
			spdlog::error( "SQLError stmt: {}", exception.stm_text );
			spdlog::error( "SQLError state: {}", exception.sqlstate );
			spdlog::error( "SQLError VarInfo: {}", exception.var_info );
		}

		bool read_task_file( std::string_view filename )
		{
			std::filesystem::path const file_path{ filename };
			std::error_code ec{};
			auto const file_size = std::filesystem::file_size( file_path, ec );
			if( file_size == 0 || ec ) return false;
			std::ifstream in_file{ file_path };
			if( !in_file ) return false;
			std::vector<char> file_buffer( file_size );
			in_file.read( file_buffer.data(), file_size );

			auto& tasks = get_scheduled_tasks();
			try {
				json json_root = json::parse( std::string_view( file_buffer.data(), file_size ) );
				file_buffer = {};
				if( !json_root.is_array() )  return false;
				json::array_t task_list = json_root.get<json::array_t>();
				for( auto const& json_task : task_list ) {
					utilities::ScheduledTask task{};
					json::object_t task_object = json_task.get<json::object_t>();
					task.task_id = task_object["id"].get<json::number_integer_t>();
					task.progress = task_object["progress"].get<json::number_integer_t>();
					json::array_t websites = task_object["websites"].get<json::array_t>();
					for( auto const& website_id : websites ) {
						task.website_ids.push_back( website_id.get<json::number_integer_t>() );
					}
					json::array_t numbers = task_object["numbers"].get<json::array_t>();
					for( auto const& number_id : numbers ) {
						task.number_ids.push_back( number_id.get<json::number_integer_t>() );
					}
					task.last_processed_number = task_object["last"].get<json::string_t>();
					tasks.push_back( std::move( task ) );
				}
				return true;
			}
			catch( std::exception const& e ) {
				spdlog::error( e.what() );
			}
			return false;
		}

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

		std::string view_to_string( boost::string_view const& str_view )
		{
			std::string str{ str_view.begin(), str_view.end() };
			boost::trim( str );
			return str;
		}

		DbConfig parse_database_file( std::string const& filename, std::string const& config_name )
		{
			std::ifstream in_file{ filename };
			if( !in_file ) return {};
			std::string line{};
			bool found = false;
			DbConfig db_config{};
			while( std::getline( in_file, line ) ) {
				if( line.size() < 1 ) continue;
				if( line[0] == '#' && line[1] == '~' ) { // new config
					if( found ) return db_config;
					found = config_name == line.c_str() + 2;
					continue;
				} else if( found ) {
					auto name_pair = split_string( line, ":" );
					if( name_pair.size() != 2 ) continue;

					std::string value = view_to_string( name_pair[1] );

					if( name_pair[0] == "username" ) db_config.username = std::move( value );
					else  if( name_pair[0] == "password" ) db_config.password = std::move( value );
					else if( name_pair[0] == "db_dns" ) db_config.db_dns = std::move( value );
				}
			}
			return db_config;
		}

		std::vector<boost::string_view> split_string( boost::string_view const& str, char const* delimeter )
		{
			std::string::size_type from_pos{};
			std::string::size_type index{ str.find( delimeter, from_pos ) };
			if( index == std::string::npos ) return { str };
			std::vector<boost::string_view> result{};
			while( index != std::string::npos ) {
				result.emplace_back( str.data() + from_pos, index - from_pos );
				from_pos = index + 1;
				index = str.find( delimeter, from_pos );
			}
			if( from_pos < str.length() )
				result.push_back( boost::string_view{ str.cbegin() + from_pos, str.size() - from_pos } );
			return result;
		}

		std::array<char const*, LEN_USER_AGENTS> const request_handler::user_agents = {
			"Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2228.0 Safari/537.36",
			"Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2227.0 Safari/537.36",
			"Mozilla/5.0 (Windows NT 6.3; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/41.0.2226.0 Safari/537.36",
			"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:40.0) Gecko/20100101 Firefox/40.1",
			"Mozilla/5.0 (Windows NT 6.3; rv:36.0) Gecko/20100101 Firefox/36.0",
			"Mozilla/5.0 (X11; Linux i586; rv:31.0) Gecko/20100101 Firefox/31.0",
			"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:31.0) Gecko/20130401 Firefox/31.0",
			"Mozilla/5.0 (Windows NT 5.1; rv:31.0) Gecko/20100101 Firefox/31.0",
			"Mozilla/5.0 (Windows NT 6.1; WOW64; Trident/7.0; AS; rv:11.0) like Gecko",
			"Mozilla/5.0 (Windows; U; MSIE 9.0; WIndows NT 9.0; en-US))",
			"Mozilla/5.0 (Windows; U; MSIE 9.0; Windows NT 9.0; en-US)",
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:68.0) Gecko/20100101 Firefox/68.0",
			"Mozilla/5.0 (Windows NT 6.1; WOW64; rv:67.0) Gecko/20100101 Firefox/67.0",
			"Mozilla/5.0 (X11; Linux i686; rv:67.0) Gecko/20100101 Firefox/67.0",
			"Mozilla/5.0 (X11; Linux x86_64; rv:67.0) Gecko/20100101 Firefox/67.0",
			"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/74.0.3729.28 Safari/537.36 OPR/61.0.3298.6 (Edition developer)",
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/64.0.3282.140 Safari/537.36 Edge/17.17134",
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/74.0.3729.134 Safari/537.36 Vivaldi/2.5.1525.40"
		};

		uri::uri( const std::string& url_s )
		{
			parse( url_s );
		}

		std::string uri::path() const
		{
			return path_;
		}

		std::string uri::host() const
		{
			return host_;
		}

		void uri::parse( const std::string& url_s )
		{
			std::string const prot_end{ "://" };
			std::string::const_iterator prot_i = std::search( url_s.begin(), url_s.end(),
				prot_end.begin(), prot_end.end() );
			protocol_.reserve( static_cast< std::size_t >( std::distance( url_s.cbegin(), prot_i ) ) );
			std::transform( url_s.begin(), prot_i,
				std::back_inserter( protocol_ ), []( int c ) { return std::tolower( c ); } );
			if( prot_i == url_s.end() ) {
				prot_i = url_s.begin();
			} else {
				std::advance( prot_i, prot_end.length() );
			}
			std::string::const_iterator path_i = std::find( prot_i, url_s.end(), '/' );
			host_.reserve( static_cast< std::size_t >( std::distance( prot_i, path_i ) ) );
			std::transform( prot_i, path_i,
				std::back_inserter( host_ ), []( int c ) { return std::tolower( c ); } );
			std::string::const_iterator query_i = std::find( path_i, url_s.end(), '?' );
			path_.assign( path_i, query_i );
			if( query_i != url_s.end() ) ++query_i;
			query_.assign( query_i, url_s.end() );
		}

		std::string get_random_agent()
		{
			static std::random_device rd{};
			static std::mt19937  gen{ rd() };
			static std::uniform_int_distribution<> uid( 0, 17 );
			return request_handler::user_agents[uid( gen )];
		}

		void background_task_executor( std::atomic_bool& stopped, std::mutex& mutex, std::shared_ptr<DatabaseConnector>& db_connector )
		{
			auto& scheduled_tasks = get_scheduled_tasks();
			while( !stopped ) {
				mutex.lock();
				if( scheduled_tasks.empty() ) {
					mutex.unlock();
					std::this_thread::sleep_for( std::chrono::seconds( SleepTimeoutSec ) );
					continue;
				}
				ScheduledTask task = std::move( scheduled_tasks.front() );
				scheduled_tasks.pop_front();
				mutex.unlock();
				if( task.progress >= 100 ) continue;
				mutex.lock();
				std::vector<WebsiteResult> websites = db_connector->get_websites( task.website_ids );
				std::vector<UploadResult> numbers = db_connector->get_uploads( task.number_ids );
				mutex.unlock();
			}
		}

		std::deque<ScheduledTask>& get_scheduled_tasks()
		{
			static std::deque<ScheduledTask> scheduled_tasks{};
			return scheduled_tasks;
		}

		bool timet_to_string( std::string& output, std::size_t t, char const* format )
		{
			std::time_t current_time{ t };
			auto tm_t = std::localtime( &current_time );
			if( !tm_t ) return false;
			output.clear();
			output.resize( 32 );
			std::size_t write_count{ std::strftime( output.data(), output.size(), format, tm_t ) };
			return write_count != 0;
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
	void Endpoint::add_endpoint( std::string const& route, std::initializer_list<http::verb> verbs,
		Callback&& callback )
	{
		if( route.empty() || route[0] != '/' )
			throw std::runtime_error{ "A valid route starts with a /" };
		endpoints.emplace( route, Rule{ std::move( verbs ), std::move( callback ) } );
	}

	std::optional<Endpoint::iterator> Endpoint::get_rules( std::string const& target )
	{
		auto iter = endpoints.find( target );
		if( iter == endpoints.end() ) return std::nullopt;
		return iter;
	}

	std::optional<Endpoint::iterator> Endpoint::get_rules( boost::string_view const& target )
	{
		return get_rules( std::string( target.data(), target.size() ) );
	}

	std::shared_ptr<DatabaseConnector> DatabaseConnector::GetDBConnector()
	{
		static std::shared_ptr<DatabaseConnector> db_connector{};
		if( !db_connector ) {
			otl_connect::otl_initialize( 1 );
			db_connector = std::make_unique<DatabaseConnector>();
		}
		return db_connector;
	}

	DatabaseConnector& DatabaseConnector::username( std::string const& username )
	{
		db_config.username = username;
		return *this;
	}

	DatabaseConnector& DatabaseConnector::password( std::string const& password )
	{
		db_config.password = password;
		return *this;
	}

	DatabaseConnector& DatabaseConnector::database_name( std::string const& db_name )
	{
		db_config.db_dns = db_name;
		return *this;
	}

	void DatabaseConnector::keep_sql_server_busy()
	{
		spdlog::info( "keeping SQL server busy" );
		std::thread sql_thread{
			[this] {
			try {
				auto dir = otl_cursor::direct_exec( otl_connector_, "select count(*) from mysql.user", true );
				spdlog::info( "OTL Busy server says: {}", dir );
			}
			catch( otl_exception const& exception ) {
				utilities::display_sql_error( exception );
				otl_connector_.logoff();
				otl_connector_.rlogon( "{}/{}@mysql8017"_format( db_config.username, db_config.password ).c_str() );
				std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
			}
			std::this_thread::sleep_for( std::chrono::minutes( 30 ) );
		}
		};
		sql_thread.detach();
	}

	bool DatabaseConnector::connect()
	{
		if( db_config.db_dns.empty() || db_config.password.empty() || db_config.username.empty() ) {
			throw std::runtime_error{ "configuration incomplete" };
		}
		if( is_running ) return is_running;

		std::string const login_str{ "{}/{}@mysql8017"_format( db_config.username, db_config.password ) };// db_config.database_name_ )
		try {
			this->otl_connector_.rlogon( login_str.c_str() );
			keep_sql_server_busy();
			is_running = true;
			return is_running;
		}
		catch( otl_exception const& exception ) {
			utilities::display_sql_error( exception );
			return is_running;
		}
	}

	std::pair<int, int> DatabaseConnector::get_login_role( std::string const& username, std::string const& password )
	{
		std::string const sql_statement{ "select id, role from tb_users where "
			"username = '{}' and password = '{}'"_format( username, password ) };
		std::pair<int, int> id_role_pair = { -1, -1 };
		try {
			otl_stream db_stream( 5, sql_statement.c_str(), otl_connector_ );
			db_stream >> id_role_pair.first >> id_role_pair.second;
		}
		catch( otl_exception const& e ) {
			utilities::display_sql_error( e );
		}
		return id_role_pair;
	}

	bool DatabaseConnector::add_upload( utilities::UploadRequest const& upload_request )
	{
		std::string const sql_statement{ "insert into tb_uploads (uploader_id, filename, upload_date, "
			"total_numbers, name_on_disk ) VALUES( {}, \"{}\", {}, {}, \"{}\" )"_format(
				upload_request.uploader_id, upload_request.upload_filename, upload_request.upload_date,
				upload_request.total_numbers, upload_request.name_on_disk ) };
		try {
			otl_cursor::direct_exec( otl_connector_, sql_statement.c_str(), otl_exception::enabled );
			return true;
		}
		catch( otl_exception const& e ) {
			utilities::display_sql_error( e );
			return false;
		}
	}

	bool DatabaseConnector::add_task( utilities::ScheduledTask& task )
	{
		std::string date_out{};
		if( !utilities::timet_to_string( date_out, task.scheduled_dt ) ) return false;

		std::string sql_statement{
			"INSERT INTO tb_tasks (scheduler_id, date_scheduled, websites, uploads, progress)"
			"VALUES( {}, \"{}\", \"{}\", \"{}\", 0 )"_format( task.scheduler_id, date_out, task.website_ids,
				task.number_ids )
		};
		try {
			otl_cursor::direct_exec( otl_connector_, sql_statement.c_str(), otl_exception::enabled );
			otl_stream stream( 1, "SELECT MAX(id) FROM tb_tasks", otl_connector_ );
			stream >> task.task_id;
			return true;
		}
		catch( otl_exception const& e ) {
			utilities::display_sql_error( e );
			return false;
		}
	}

	std::vector<utilities::TaskResult> DatabaseConnector::get_all_tasks()
	{
		char const* sql_statement = "SELECT tb_tasks.id, username, date_scheduled, websites, "
			"uploads, progress FROM tb_tasks INNER JOIN tb_users";
			std::vector<utilities::TaskResult> result{};
		try {
			otl_stream db_stream( 100'000, sql_statement, otl_connector_ );
			utilities::TaskResult item{};
			while( db_stream >> item ) {
				result.push_back( std::move( item ) );
			}
		}
		catch( otl_exception const& e ) {
			utilities::display_sql_error( e );
		}
		return result;
	}

	std::vector<utilities::WebsiteResult> DatabaseConnector::get_websites( std::vector<std::size_t> const& ids )
	{
		std::string sql_statement{};
		if( ids.empty() ) {
			sql_statement = "SELECT id, address, nickname FROM tb_websites";
		} else {
			sql_statement = "SELECT id, address, nickname FROM tb_websites WHERE ID in ({})"_format( ids );
		}
		std::vector<utilities::WebsiteResult> results{};
		try {
			otl_stream db_stream( 1'000, sql_statement.c_str(), otl_connector_ );
			utilities::WebsiteResult website_info{};
			while( db_stream >> website_info ) {
				results.push_back( std::move( website_info ) );
			}
		}
		catch( otl_exception const& e ) {
			utilities::display_sql_error( e );
		}
		return results;
	}

	std::string DatabaseConnector::svector_to_string( std::vector<boost::string_view> const& vec )
	{
		if( vec.empty() ) return {};
		std::string str{};
		for( std::size_t index = 0; index < vec.size() - 1; ++index ) {
			str.append( vec[index].to_string() + ", " );
		}
		str.append( vec.back().to_string() );
		return str;
	}
}