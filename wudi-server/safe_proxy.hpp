#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <ctime>
#include <memory>

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

class proxy_base {
protected:
  net::io_context &context_;
  std::string filename_;
  std::string host_;
  std::string target_;
  std::string count_path_;

  extraction_data current_extracted_data_;
  std::mutex mutex_{};
  std::size_t count_{};
  std::vector<endpoint_ptr> endpoints_;
  std::atomic_bool has_error = false;

protected:
  void load_proxy_file();
  void save_proxies_to_file();
  void clear();
  void push_back(custom_endpoint ep);
  virtual extraction_data
  get_remain_count(net::ip::basic_resolver_results<net::ip::tcp> &);
  virtual void get_more_proxies();

public:
  proxy_base(net::io_context &, std::string const &filename);
  virtual ~proxy_base() {}
  endpoint_ptr next_endpoint();
};

class generic_proxy final : public proxy_base {
  static std::string const proxy_filename;

public:
  generic_proxy(net::io_context &context);
  ~generic_proxy() {}
};

class jjgames_proxy final : public proxy_base {
  static std::string const proxy_filename;

public:
  jjgames_proxy(net::io_context &context);
  ~jjgames_proxy() {}
};

class other_proxies_t final : public proxy_base {
  static std::string const proxy_filename;

public:
  other_proxies_t(net::io_context &context);
  ~other_proxies_t() {}
  virtual extraction_data
  get_remain_count(net::ip::basic_resolver_results<net::ip::tcp> &) override;
};

using proxy_provider_t = proxy_base;
} // namespace wudi_server
