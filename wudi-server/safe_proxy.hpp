#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/signals2/signal.hpp>
#include <ctime>
#include <memory>

namespace wudi_server {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ip = net::ip;
namespace signals2 = boost::signals2;

using tcp = ip::tcp;

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
  ProxyProperty property{ProxyProperty::ProxyActive};

  custom_endpoint(tcp::endpoint &&ep) : endpoint(std::move(ep)) {}
  operator tcp::endpoint() const { return endpoint; }
  void swap(custom_endpoint &);
};

using EndpointList = std::vector<custom_endpoint>;
using endpoint_ptr = std::shared_ptr<custom_endpoint>;

using NewProxySignal = signals2::signal<void(
    std::thread::id, std::uint32_t, std::vector<endpoint_ptr> const &)>;
void swap(custom_endpoint &a, custom_endpoint &b);

class global_proxy_repo_t {
  NewProxySignal new_endpoints_signal_;

public:
  NewProxySignal *new_ep_signal() { return &new_endpoints_signal_; }
};

class proxy_base {
protected:
  net::io_context &context_;
  NewProxySignal &broadcast_proxy_signal_;
  std::thread::id const this_thread_id_;
  std::uint32_t const website_id_;

  std::string host_;
  std::string target_;
  std::string count_path_;
  std::string filename_;

  extraction_data current_extracted_data_;
  std::mutex mutex_{};
  std::size_t count_{};
  std::vector<endpoint_ptr> endpoints_;
  std::atomic_bool verify_extract_ = false;
  std::atomic_bool has_error_ = false;
  std::atomic_bool first_pass_ = true;
  std::atomic_bool is_free_ = true;

protected:
  void load_proxy_file();
  void save_proxies_to_file();
  void clear();
  void push_back(custom_endpoint ep);
  virtual extraction_data
  get_remain_count(ip::basic_resolver_results<ip::tcp> &);
  virtual void get_more_proxies();

public:
  proxy_base(net::io_context &, NewProxySignal &, std::thread::id,
             std::uint32_t, std::string const &filename);
  virtual ~proxy_base() {}
  endpoint_ptr next_endpoint();
  void add_more(std::thread::id const, std::uint32_t const,
                std::vector<endpoint_ptr> const &);
};

class http_proxy final : public proxy_base {
  static std::string const proxy_filename;

public:
  http_proxy(net::io_context &, NewProxySignal &, std::thread::id,
             std::uint32_t);
  ~http_proxy() {}
};

class socks5_proxy final : public proxy_base {
  static std::string const proxy_filename;

public:
  socks5_proxy(net::io_context &, NewProxySignal &, std::thread::id,
               std::uint32_t);
  ~socks5_proxy() {}
};

using proxy_provider_t = proxy_base;
} // namespace wudi_server
