#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <ctime>
#include <memory>
#include <optional>

enum safe_count { max_ip_use = 150, max_endpoints_allowed = 5'000 };

namespace wudi_server {
namespace beast = boost::beast;
namespace net = boost::asio;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

net::io_context &get_network_context();

enum class ProxyProperty { ProxyUnresponsive, ProxyBlocked, ProxyActive };

struct extraction_data {
  std::time_t expire_time{};
  int product_remain{};
  int connect_remain{};
  int extract_remain{};
  bool is_available{false};
};

class global_proxy_provider {
private:
  net::io_context &context_;
  extraction_data current_extracted_data_;
  static std::string const proxy_generator_address;
  static std::string const http_proxy_filename;
  std::mutex mutex_{};
  std::vector<tcp::endpoint> endpoints_;

private:
  void load_proxy_file();
  void save_proxies_to_file();
  global_proxy_provider(net::io_context &context);

public:
  std::size_t get_more_proxies(std::size_t const current_size);
  tcp::endpoint &endpoint_at(std::size_t);
  std::size_t count() { return endpoints_.size(); }
  static global_proxy_provider &get_global_proxy_provider();
  static extraction_data
  remain_count(net::io_context &,
               net::ip::basic_resolver_results<net::ip::tcp> &);
};

class proxy_provider_t {
  struct endpoint_info {
    ProxyProperty property{ProxyProperty::ProxyActive};
    std::size_t use_count{};
    std::size_t index_{};
  };
  std::vector<endpoint_info> information_list_{};
  global_proxy_provider &proxy_provider_;
  std::mutex mutex_;
  bool exhausted_daily_dose{false};

public:
  proxy_provider_t(global_proxy_provider &gsp);
  std::size_t next_endpoint();
  tcp::endpoint &endpoint(std::size_t);
  void assign_property(std::size_t, ProxyProperty);
};
} // namespace wudi_server
