#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <optional>

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

net::io_context &get_network_context();

enum class ProxyProperty { ProxyUnresponsive, ProxyBlocked, ProxyActive };

struct custom_endpoint {
  tcp::endpoint endpoint{};
  ProxyProperty property{};

  custom_endpoint(tcp::endpoint &&ep, ProxyProperty prop)
      : endpoint(std::move(ep)), property{prop} {}
  operator net::ip::tcp::endpoint() const { return endpoint; }
  void swap(custom_endpoint &);
};

using EndpointList = std::vector<custom_endpoint>;
using endpoint_ptr = std::shared_ptr<custom_endpoint>;
void swap(custom_endpoint &a, custom_endpoint &b);

class safe_proxy {
private:
  beast::tcp_stream http_tcp_stream_;
  http::request<http::empty_body> http_request_;
  net::io_context &context_;

  static std::string const proxy_generator_address;
  static std::string const https_proxy_filename;
  static std::string const http_proxy_filename;
  std::mutex mutex_{};
  std::size_t count_{};
  std::vector<endpoint_ptr> endpoints_;
  std::atomic_bool is_free = true;
  std::atomic_bool has_error = false;

private:
  void load_proxy_file();
  void get_more_proxies();
  void save_proxies_to_file();

public:
  safe_proxy(net::io_context &context);
  void clear();
  void push_back(custom_endpoint ep);
  std::optional<endpoint_ptr> next_endpoint();
};
} // namespace wudi_server