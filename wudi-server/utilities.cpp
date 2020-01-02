#include "utilities.hpp"
#include <vector>
#include <fstream>
#include <boost/algorithm/string.hpp>
#include <spdlog/spdlog.h>

namespace wudi_server
{
	using namespace fmt::v6::literals;

	namespace utilities
	{
		otl_stream& operator>>( otl_stream& os, UploadResult& item )
		{
			os >> item.upload_id >> item.filename >> item.upload_alias;
			return os;
		}

		void display_sql_error( otl_exception const& exception )
		{
			spdlog::error( "SQLError code: {}", exception.code );
			spdlog::error( "SQLError stmt: {}", exception.stm_text );
			spdlog::error( "SQLError state: {}", exception.sqlstate );
			spdlog::error( "SQLError VarInfo: {}", exception.var_info );
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

					std::string key = view_to_string( name_pair[0] );
					std::string value = view_to_string( name_pair[1] );

					if( key == "username" ) db_config.username_ = std::move( value );
					else  if( key == "password" ) db_config.password_ = std::move( value );
					else if( key == "db_name" ) db_config.database_name_ = std::move( value );
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
				result.push_back( boost::string_view{ str.begin() + from_pos, ( index - from_pos ) } );
				from_pos = index + 1;
				index = str.find( delimeter, from_pos );
			}
			if( from_pos < str.length() )
				result.push_back( boost::string_view{ str.cbegin() + from_pos, str.size() - from_pos } );
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
		db_config.username_ = username;
		return *this;
	}

	DatabaseConnector& DatabaseConnector::password( std::string const& password )
	{
		db_config.password_ = password;
		return *this;
	}

	DatabaseConnector& DatabaseConnector::database_name( std::string const& db_name )
	{
		db_config.database_name_ = db_name;
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
				otl_connector_.rlogon( "{}/{}@mysql8017"_format( db_config.username_, db_config.password_ ).c_str() );
				std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
			}
			std::this_thread::sleep_for( std::chrono::minutes( 30 ) );
		}
		};
		sql_thread.detach();
	}

	bool DatabaseConnector::connect()
	{
		if( db_config.database_name_.empty() || db_config.password_.empty() || db_config.username_.empty() ) {
			throw std::runtime_error{ "configuration incomplete" };
		}
		if( is_running ) return is_running;

		std::string const login_str{ "{}/{}@mysql8017"_format( db_config.username_, db_config.password_ ) };// db_config.database_name_ )
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

	std::vector<utilities::UploadResult> DatabaseConnector::get_uploads( std::vector<boost::string_view> const& ids )
	{
		std::string sql_statement{};
		if( ids.empty() ) {
			sql_statement = "select id, filename, upload_code from tb_uploads";
		} else {
			sql_statement = "select id, filename, upload_code from tb_uploads where id in ({})"_format( svector_to_string( ids) );
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
			utilities::display_sql_error( e );
		}
		return result;
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