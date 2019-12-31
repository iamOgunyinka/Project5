#include "session.hpp"
#include <nlohmann_json.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <fstream>
#include <gzip/decompress.hpp>

namespace wudi_server
{
	using nlohmann::json;

	void session::http_read_data()
	{
		buffer_.clear();
		empty_body_parser_ = std::make_unique<http::request_parser<http::empty_body>>();
		empty_body_parser_->body_limit( utilities::FiftyMegabytes );
		beast::get_lowest_layer( tcp_stream_ ).expires_after( std::chrono::minutes( args_.timeout_mins ) );
		http::async_read_header( tcp_stream_, buffer_, *empty_body_parser_, beast::bind_front_handler( &session::on_header_read, shared_from_this() ) );
	}

	void session::on_header_read( beast::error_code ec, std::size_t const )
	{
		if( ec ) {
			fputs( ec.message().c_str(), stderr );
			fputc( '\n', stderr );
			return error_handler( server_error( ec.message(), ErrorType::ServerError, string_request{} ), true );
		} else {
			auto const content_type = empty_body_parser_->get()[http::field::content_type];
			if( content_type == "application/json" ) {
				client_request_ = std::make_unique<http::request_parser<http::string_body>>( std::move( *empty_body_parser_ ) );
				http::async_read( tcp_stream_, buffer_, *client_request_, beast::bind_front_handler( &session::on_data_read, shared_from_this() ) );
			} else if( content_type == "application/gzip" ) {
				dynamic_body_parser = std::make_unique<dynamic_request>( std::move( *empty_body_parser_ ) );
				dynamic_body_parser->body_limit( utilities::FiftyMegabytes );
				http::async_read( tcp_stream_, buffer_, *dynamic_body_parser, beast::bind_front_handler( &session::binary_data_read, shared_from_this() ) );
			} else {
				return shutdown_socket();
			}
		}
	}

	void session::binary_data_read( beast::error_code ec, std::size_t bytes_transferred )
	{
		if( ec ) {
			fprintf( stderr, "%s\n", ec.message().c_str() );
			return error_handler( bad_request( "invalid content-type", string_request{} ) );
		}
		auto& request = dynamic_body_parser->get();
		handle_requests( request );
	}

	void session::on_data_read( beast::error_code ec, std::size_t const )
	{
		if( ec == http::error::end_of_stream ) { // end of connection
			return shutdown_socket();
		} else if( ec == http::error::body_limit ) {
			return error_handler( server_error( ec.message(), ErrorType::ServerError, string_request{} ), true );
		} else if( ec ) {
			fputs( ec.message().c_str(), stderr );
			return error_handler( server_error( ec.message(), ErrorType::ServerError, string_request{} ), true );
		} else {
			handle_requests( client_request_->get() );
		}
	}

	void session::shutdown_socket()
	{
		beast::error_code ec{};
		beast::get_lowest_layer( tcp_stream_ ).socket().shutdown( asio::socket_base::shutdown_send, ec );
		beast::get_lowest_layer( tcp_stream_ ).close();
	}

	void session::error_handler( string_response&& response, bool close_socket )
	{
		auto resp = std::make_shared<string_response>( std::move( response ) );
		resp_ = resp;
		if( !close_socket ) {
			http::async_write( tcp_stream_, *resp,
				beast::bind_front_handler( &session::on_data_written, shared_from_this() ) );
		} else {
			http::async_write( tcp_stream_, *resp, [self = shared_from_this()]( auto const err_c, std::size_t const )
			{
				self->shutdown_socket();
			} );
		}
	}

	void session::on_data_written( beast::error_code ec, std::size_t const bytes_written )
	{
		if( ec ) {
			fputs( ec.message().c_str(), stderr );
			fputc( '\n', stderr );
			return;
		}
		resp_ = nullptr;
		http_read_data();
	}

	void session::login_handler( string_request const& request, std::string_view const& optional_query )
	{
		try {
			json json_body{ json::parse( request.body() ) };
			json::object_t login_info{ json_body.get<json::object_t>() };
			return error_handler( get_error( login_info["username"],
				ErrorType::NoError, http::status::ok, request ) );
		}
		catch( std::exception const& exception ) {
			fputs( exception.what(), stderr );
			return error_handler( bad_request( "json object not valid", request ) );
		}
	}

	void session::index_page_handler( string_request const& request, std::string_view const& optional_query )
	{
		return error_handler( get_error( "login",
			ErrorType::NoError, http::status::ok, request ) );
	}

	void session::upload_handler( string_request const& request, std::string_view const& optional_query )
	{
		boost::string_view const filename_view = dynamic_body_parser->get()["filename"];
		if( filename_view.empty() ) return error_handler( bad_request( "filename missing", string_request{} ) );
		auto& body = request.body();
		std::string filename{ std::string( filename_view.data(), filename_view.size() ) + ".json" };
		std::ofstream out_file{ filename };
		if( !out_file ) return error_handler( server_error( "unable to save file", ErrorType::ServerError, request ) );
		try {
			std::string decompressed_json = gzip::decompress( const_cast< char const* >( body.data() ), body.size() );
			out_file << decompressed_json;
			out_file.flush();
			return send_response( success( "ok", request ) );
		}
		catch( std::exception const& e ) {
			std::fprintf( stderr, "%s\n", e.what() );
			return error_handler( bad_request( "unable to decompress file", request ) );
		}
	}

	void session::handle_requests( string_request const& request )
	{
		boost::string_view const request_target{ request.target() };
		auto method = request.method();
		if( request_target.empty() ) return index_page_handler( request, "" );
		auto split = utilities::split_string( request_target, "?" );
		if( auto iter = endpoint_apis_.get_rules( split[0] ); iter.has_value() ) {
			auto iter_end = iter.value()->second.verbs_.cbegin() + iter.value()->second.num_verbs_;
			auto found_iter = std::find( iter.value()->second.verbs_.cbegin(), iter_end, method );
			if( found_iter == iter_end ) {
				return error_handler( method_not_allowed( request ) );
			}
			std::string_view query = split.size() > 1 ? std::string_view( split[1].data(), split[1].size() ) : "";
			return iter.value()->second.route_callback_( request, query );
		} else {
			return error_handler( not_found( request ) );
		}
	}

	session::session( asio::ip::tcp::socket&& socket, command_line_interface const& args ) :
		tcp_stream_{ std::move( socket ) }, args_{ args }
	{
		add_endpoint_interfaces();
	}

	void session::add_endpoint_interfaces()
	{
		using http::verb;
		endpoint_apis_.add_endpoint( "/", { verb::get },
			beast::bind_front_handler( &session::index_page_handler, shared_from_this() ) );
		endpoint_apis_.add_endpoint( "/login", { verb::get, verb::post },
			beast::bind_front_handler( &session::login_handler, shared_from_this() ) );
		endpoint_apis_.add_endpoint( "/upload", { verb::post }, beast::bind_front_handler(
			&session::upload_handler, shared_from_this() ) );
		endpoint_apis_.add_endpoint( "/website", { verb::post, verb::get },
			beast::bind_front_handler( &session::website_handler, shared_from_this() ) );
		endpoint_apis_.add_endpoint( "/schedule_task", { verb::post, verb::post },
			beast::bind_front_handler( &session::schedule_task_handler, shared_from_this() ) );
	}

	void session::schedule_task_handler( string_request const& request, std::string_view const& query )
	{
		using http::verb;
		if( request.method() == verb::get ) {

		}
	}

	void session::website_handler( string_request const& request, std::string_view const& query )
	{

	}

	void session::run()
	{
		http_read_data();
	}

	string_response session::not_found( string_request const& request )
	{
		return get_error( "url not found", ErrorType::ResourceNotFound,
			http::status::not_found, request );
	}

	string_response session::server_error( std::string const& message, ErrorType type,
		string_request const& request )
	{
		return get_error( message, type, http::status::internal_server_error, request );
	}

	string_response session::bad_request( std::string const& message, string_request const& request )
	{
		return get_error( message, ErrorType::BadRequest, http::status::bad_request, request );
	}

	string_response session::method_not_allowed( string_request const& req )
	{
		return get_error( "method not allowed", ErrorType::MethodNotAllowed,
			http::status::method_not_allowed, req );
	}

	string_response session::get_error( std::string const& error_message,
		utilities::ErrorType type, http::status status, string_request const& req )
	{
		json::object_t result_obj{};
		result_obj["status"] = type;
		result_obj["message"] = error_message;
		json result{ result_obj };

		string_response response{ status, req.version() };
		response.set( http::field::server, "wudi-custom-server" );
		response.set( http::field::content_type, "application/json" );
		response.keep_alive( req.keep_alive() );
		response.body() = result.dump();
		response.prepare_payload();
		return response;
	}

	string_response session::success( std::string const& error_message, string_request const& req )
	{
		json::object_t result_obj{};
		result_obj["status"] = ErrorType::NoError;
		result_obj["message"] = error_message;
		json result{ result_obj };

		string_response response{ http::status::ok, req.version() };
		response.set( http::field::server, "wudi-custom-server" );
		response.set( http::field::content_type, "application/json" );
		response.keep_alive( req.keep_alive() );
		response.body() = result.dump();
		response.prepare_payload();
		return response;
	}

	void session::send_response( string_response&& response )
	{
		auto resp = std::make_shared<string_response>( std::move( response ) );
		resp_ = resp;
		http::async_write( tcp_stream_, *resp,
			beast::bind_front_handler( &session::on_data_written, shared_from_this() ) );
	}
}