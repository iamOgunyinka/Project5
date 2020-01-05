#pragma once
/*
#include <memory>
#include <vector>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include "safe_proxy.hpp"
#include "utilities.hpp"

#ifndef emit
#define emit
#endif

namespace wudi_server
{
    namespace beast = boost::beast;
    namespace net = boost::asio;
    namespace http = beast::http;

    using utilities::ProxyAddress;
    using utilities::SearchResultType;
    using tcp = boost::asio::ip::tcp;
    using CustomStringList = std::vector<std::string>;
    using ProxyList = std::vector<ProxyAddress>;

    net::io_context& get_network_context();

    class http_socket
	{
        static std::string const password_base64_hash;
    private:
        net::io_context &io_;
        beast::tcp_stream tcp_stream_;
        beast::flat_buffer buffer_{};
        http::request<http::string_body> post_request_{};
        http::response<http::string_body> response_{};
        std::string current_number_{};
        std::string address_{};

        std::size_t connect_count_{};
        std::size_t send_count_{};
        bool tried_unresposiveness_{ false };
        utilities::threadsafe_vector<std::string>& numbers_;
        EndpointList temp_list_;
        safe_proxy& proxy_provider_;
        safe_proxy::endpoint_ptr current_endpoint_;
	private:
		void result_available( SearchResultType, std::string const& );

        void connect();
		void on_connected( beast::error_code, tcp::resolver::results_type::endpoint_type );
        void send_http_data();
		void on_data_sent( beast::error_code, std::size_t const );
        void receive_data();
        void on_data_received( beast::error_code, std::size_t const );
		void reconnect();
        void resend_http_request();

        void choose_next_proxy();
        void send_next();
        void prepare_request_data( bool use_auth = false );
        void set_authentication_header();
        void current_proxy_assign_prop( int properties );
    public:
        http_socket( net::io_context& io, safe_proxy& proxy_provider,
                     utilities::threadsafe_vector<std::string>& numbers,
                     std::string const &address );
        ~http_socket() = default;
        void start_connect();
    };
}
*/