#ifndef SAFE_PROXY_HPP
#define SAFE_PROXY_HPP
#include <optional>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <memory>
/*
namespace wudi_server
{
    namespace beast = boost::beast;
    namespace net = boost::asio;
    namespace http = beast::http;
    using tcp = boost::asio::ip::tcp;

    net::io_context& get_network_context();

    enum ProxyProperty
    {
        ProxyUnresponsive,
        ProxyBlocked,
        ProxyActive
    };

    struct custom_endpoint
    {
        tcp::endpoint endpoint{};
        int property{};

        operator net::ip::tcp::endpoint() const {
            return endpoint;
        }
        void swap( custom_endpoint & );
    };
    using EndpointList = std::vector<custom_endpoint>;
    void swap( custom_endpoint &a, custom_endpoint & b );

    struct safe_proxy
    {
        using endpoint_ptr = std::shared_ptr<custom_endpoint>;
    private:
        std::mutex mutex_{};
        std::size_t count_{};
        std::vector<endpoint_ptr> endpoints_;
        void GetMoreProxies();
        std::atomic_bool is_free = true;
        std::atomic_bool has_error = false;
    public:
        safe_proxy();
        void clear();
        void push_back( custom_endpoint ep );
        std::optional<endpoint_ptr> next_endpoint();
    };
}
*/
#endif // SAFE_PROXY_HPP
