#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/signals2/signal.hpp>
#include <ctime>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace wudi_server {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ip = net::ip;
namespace signals2 = boost::signals2;

using tcp = ip::tcp;

net::io_context &get_network_context();

enum class ProxyProperty {
  ProxyActive,
  ProxyBlocked,
  ProxyMaxedOut,
  ProxyUnresponsive
};

enum class proxy_type_e : int { socks5 = 0, http_https_proxy = 1 };

struct extraction_data {
  std::time_t expire_time{};
  int product_remain{};
  int connect_remain{};
  int extract_remain{};
  bool is_available{false};
};

struct proxy_configuration_t {
  std::string proxy_username{};
  std::string proxy_password{};
  std::string proxy_target{};
  std::string count_target{};
  proxy_type_e proxy_protocol;
  int fetch_interval{};
  int share_proxy{};
  int max_socket{};
  int fetch_once{};
  int software_version{};
};

std::optional<proxy_configuration_t> read_proxy_configuration();

struct custom_endpoint {
  tcp::endpoint endpoint_{};
  std::string user_name_{};
  std::string password_{};
  int number_scanned{};
  ProxyProperty property{ProxyProperty::ProxyActive};

  custom_endpoint(tcp::endpoint &&ep, std::string const &username,
                  std::string const &password)
      : endpoint_(std::move(ep)), user_name_{username}, password_{password} {}
  operator tcp::endpoint() const { return endpoint_; }

  std::string &username() { return user_name_; }
  std::string &password() { return password_; }
  void swap(custom_endpoint &);
};

template <typename T> class vector_wrapper {
  std::mutex mutex_{};
  std::vector<T> container_{};

public:
  using value_type = T;

  vector_wrapper() {}
  bool empty() {
    std::lock_guard<std::mutex> lock_g{mutex_};
    return container_.empty();
  }
  void clear() {
    std::lock_guard<std::mutex> lock_g{mutex_};
    container_.clear();
  }
  void push_back(T const &t) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    container_.push_back(t);
  }
  void push_back(T &&t) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    container_.push_back(std::move(t));
  }
  typename std::vector<T>::size_type size() {
    std::lock_guard<std::mutex> lock_g{mutex_};
    return container_.size();
  }

  T &operator[](typename std::vector<T>::size_type const index) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    return container_[index];
  }
  T const &operator[](typename std::vector<T>::size_type const index) const {
    std::lock_guard<std::mutex> lock_g{mutex_};
    return container_[index];
  }
  void remove_first_n(std::size_t const count) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    if (container_.size() < count) {
      return container_.clear();
    }
    container_.erase(container_.begin(), container_.begin() + count);
  }
  template <typename Func> void remove_if(Func &&func) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    container_.erase(std::remove_if(container_.begin(), container_.end(), func),
                     container_.end());
  }
  template <typename Func> void for_each(Func &&func) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    for (auto &elem : container_) {
      std::forward<Func>(func)(elem);
    }
  }
};

using endpoint_ptr = std::shared_ptr<custom_endpoint>;
using endpoint_ptr_list = vector_wrapper<endpoint_ptr>;

struct shared_data_t {
  std::thread::id thread_id{};
  std::uint32_t web_id{};
  proxy_type_e proxy_type;
  mutable std::set<uint32_t> shared_web_ids{};
  std::vector<custom_endpoint> eps{};
};

using NewProxySignal = signals2::signal<void(shared_data_t const &)>;
void swap(custom_endpoint &a, custom_endpoint &b);

class global_proxy_repo_t {
  NewProxySignal new_endpoints_signal_;

public:
  NewProxySignal *new_ep_signal() { return &new_endpoints_signal_; }
};

class proxy_base {
  static std::time_t last_fetch_time_;
  static std::mutex fetch_time_mutex_;

protected:
  net::io_context &context_;
  NewProxySignal &broadcast_proxy_signal_;
  proxy_configuration_t &proxy_config_;
  std::thread::id const this_thread_id_;
  std::uint32_t const website_id_;
  std::size_t proxies_used_{};

  std::string filename_;
  extraction_data current_extracted_data_;
  std::mutex mutex_{};
  std::size_t count_{};
  endpoint_ptr_list endpoints_;
  std::atomic_bool has_error_ = false;
  std::atomic_bool confirm_count_ = !proxy_config_.count_target.empty();

protected:
  void load_proxy_file();
  void save_proxies_to_file();
  virtual extraction_data get_remain_count();
  virtual void get_more_proxies();

public:
  using Property = ProxyProperty;
  using value_type = endpoint_ptr;
  proxy_base(net::io_context &, NewProxySignal &, proxy_configuration_t &,
             std::thread::id, std::uint32_t, std::string const &filename);
  virtual ~proxy_base() {}
  endpoint_ptr next_endpoint();
  proxy_type_e type() const;
  void add_more(shared_data_t const &);
  auto total_used() const { return proxies_used_; }
  void total_used(int val) { proxies_used_ += val; }
};

struct http_proxy final : proxy_base {
  http_proxy(net::io_context &, NewProxySignal &, proxy_configuration_t &,
             std::thread::id, std::uint32_t);
  ~http_proxy() {}
};

struct socks5_proxy final : proxy_base {
  socks5_proxy(net::io_context &, NewProxySignal &, proxy_configuration_t &,
               std::thread::id, std::uint32_t);
  ~socks5_proxy() {}
};

using proxy_provider_t = proxy_base;
} // namespace wudi_server