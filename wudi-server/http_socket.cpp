/*
#include <vector>
#include <map>
#include <string>
#include <spdlog/spdlog.h>
#include "http_socket.hpp"
enum Constants
{
	PROXY_REQUIRES_AUTHENTICATION = 407
};

namespace wudi_server
{
	using utilities::request_handler;
	using namespace fmt::v6::literals;

	std::string const http_socket::password_base64_hash{
		"bGFueHVhbjM2OUBnbWFpbC5jb206TGFueHVhbjk2Mw=="
	};

	void http_socket::resend_http_request()
	{
		if( ++send_count_ >= utilities::MaxRetries ) {
			current_proxy_assign_prop( ProxyUnresponsive );
			choose_next_proxy();
			connect();
		} else {
			send_http_data();
		}
	}

	void http_socket::send_http_data()
	{
		tcp_stream_.expires_after( std::chrono::milliseconds( utilities::Timeout ) );
		http::async_write( tcp_stream_, post_request_, beast::bind_front_handler(
			&http_socket::on_data_sent, this ) );
	}

	void http_socket::on_data_sent( beast::error_code ec, std::size_t const )
	{
		if( ec ) resend_http_request();
		else receive_data();
	}

	void http_socket::receive_data()
	{
		tcp_stream_.expires_after( std::chrono::milliseconds( utilities::Timeout * 3 ) ); //3*3secs
		http::async_read( tcp_stream_, buffer_, response_, beast::bind_front_handler(
			&http_socket::on_data_received, this ) );
	}

	void http_socket::start_connect()
	{
		choose_next_proxy();
		if( current_endpoint_ ) send_next();
	}

	void http_socket::send_next()
	{
		try {
			current_number_ = numbers_.get();
			prepare_request_data();
			connect();
		}
		catch( utilities::empty_container_exception& ) {
			return;
		}
	}

	void http_socket::reconnect()
	{
		++connect_count_;
		if( connect_count_ >= utilities::MaxRetries ) {
			current_proxy_assign_prop( ProxyProperty::ProxyUnresponsive );
			choose_next_proxy();
		}
		connect();
	}

	void http_socket::connect()
	{
		if( temp_list_.empty() ) return;
		tcp_stream_.expires_after( std::chrono::milliseconds( utilities::Timeout ) );
		tcp_stream_.async_connect( temp_list_, beast::bind_front_handler(
			&http_socket::on_connected, this ) );
	}

	void http_socket::on_connected( beast::error_code ec, tcp::resolver::results_type::endpoint_type )
	{
		if( ec ) reconnect();
		else send_http_data();
	}

	void http_socket::choose_next_proxy()
	{
		send_count_ = 0;
		connect_count_ = 0;
		temp_list_.clear();
		if( auto proxy = proxy_provider_.next_endpoint(); proxy.has_value() ) {
			current_endpoint_ = proxy.value();
			temp_list_.push_back( *current_endpoint_ );
		} else {
			current_endpoint_ = nullptr;
			emit result_available( SearchResultType::Unknown, current_number_ );
		}
	}

	void http_socket::prepare_request_data( bool use_authentication_header )
	{
		std::string const payload{ "phone={}&isOverSea=0&validcodetype=1"_format( current_number_ ) };
		post_request_.clear();
		post_request_.method( beast::http::verb::post );
		post_request_.version( 11 );
		post_request_.target( address_ );
		post_request_.set( beast::http::field::connection, "keep-alive" );
		if( use_authentication_header ) {
			post_request_.set( beast::http::field::proxy_authorization, "Basic " +
				password_base64_hash );
		}
		post_request_.set( beast::http::field::host, utilities::uri{ address_ }.host() + ":443" );
		post_request_.set( beast::http::field::cache_control, "no-cache" );
		post_request_.set( beast::http::field::user_agent, utilities::get_random_agent() );
		post_request_.set( beast::http::field::content_type, "application/x-www-form-urlencoded; "
			"charset=UTF-8" );
		post_request_.body() = payload;
		post_request_.prepare_payload();
	}

	void http_socket::current_proxy_assign_prop( int properties )
	{
		if( current_endpoint_ ) {
			current_endpoint_->property = properties;
		}
	}

	http_socket::http_socket( net::io_context& io_context, safe_proxy& proxy_provider,
		utilities::threadsafe_vector<std::string>& numbers,
		std::string const& address ) : io_{ io_context },
		tcp_stream_{ net::make_strand( io_ ) }, address_{ address },
		numbers_{ numbers }, proxy_provider_{ proxy_provider }
	{
	}

	void http_socket::set_authentication_header()
	{
		prepare_request_data( true );
	}

	void http_socket::on_data_received( beast::error_code ec, std::size_t const )
	{
		static std::array<std::size_t, 10> redirect_codes{ 300, 301, 302, 303, 304, 305, 306, 307,
			308, 400 };
		if( ec ) {
			current_proxy_assign_prop( ProxyProperty::ProxyUnresponsive );
			choose_next_proxy();
			connect();
			return;
		}

		std::size_t const status_code = response_.result_int();
		//check if we've been redirected, most likely due to IP ban
		if( utilities::status_in_codes( status_code, redirect_codes ) ) {
			current_proxy_assign_prop( ProxyProperty::ProxyBlocked );
			choose_next_proxy();
			connect();
			return;
		}

		if( status_code == PROXY_REQUIRES_AUTHENTICATION ) {
			set_authentication_header();
			connect();
			return;
		}

		auto& body{ response_.body() };
		json document;
		try {
			document = json::parse( body );
		}
		catch( std::exception const& exception ) {

			std::size_t const opening_brace_index = body.find( '{' );
			std::size_t const closing_brace_index = body.find( '}' );

			if( status_code != 200 || opening_brace_index == std::string::npos ) {
				emit result_available( SearchResultType::Unknown, current_number_ );
				send_next();
				return;
			} else {
				if( closing_brace_index == std::string::npos ) {
					emit result_available( SearchResultType::Unknown, current_number_ );
					send_next();
					return;
				} else {
					body = std::string( body.begin() + opening_brace_index,
						body.begin() + closing_brace_index + 1 );
					try {

						document = json::parse( body );
					}
					catch( std::exception const& e ) {
						emit result_available( SearchResultType::Unknown, current_number_ );
						send_next();
						return;
					}
				}
			}
		}

		try {
			json::object_t object = document.get<json::object_t>();
			auto result{ object["success"].get<json::number_integer_t>() };
			emit result_available( result == 0 ? SearchResultType::Registered : SearchResultType::NotRegistered,
				current_number_ );
		}
		catch( ... ) {
			emit result_available( SearchResultType::Unknown, current_number_ );
		}
		send_next();
	}

	net::io_context& get_network_context()
	{
		static net::io_context context{};
		return context;
	}
}
#ifdef emit
#undef emit
#endif // emit
*/