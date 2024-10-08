#pragma once

#include "utilities.hpp"
#include <boost/asio.hpp>
#include <boost/signals2/signal.hpp>
#include <ctime>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace wudi_server {
namespace net = boost::asio;
namespace ip = net::ip;
namespace signals2 = boost::signals2;

using tcp = ip::tcp;

net::io_context &get_network_context();

enum class proxy_property_e {
  ProxyActive,
  ProxyBlocked,
  ProxyMaxedOut,
  ProxyToldToWait,
  ProxyUnresponsive
};

enum class proxy_type_e : int { socks5 = 0, http_https_proxy = 1 };

struct extraction_data_t {
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
  proxy_type_e proxy_protocol = proxy_type_e::http_https_proxy;
  int fetch_interval{};
  int share_proxy{};
  int max_socket{};
  int fetch_once{};
  int software_version{};
};

std::optional<proxy_configuration_t> read_proxy_configuration();

struct custom_endpoint_t {
  tcp::endpoint endpoint_{};
  std::string user_name_{};
  std::string password_{};
  int number_scanned{};
  std::time_t time_last_used{};

  proxy_property_e property{proxy_property_e::ProxyActive};

  custom_endpoint_t(tcp::endpoint &&ep, std::string const &username,
                    std::string const &password)
      : endpoint_(std::move(ep)), user_name_{username}, password_{password} {}
  operator tcp::endpoint() const { return endpoint_; }

  std::string &username() { return user_name_; }
  std::string &password() { return password_; }
  void swap(custom_endpoint_t &);
};

template <typename T> class vector_wrapper_t {
  std::mutex mutex_{};
  std::vector<T> container_{};
  using underlying_type = typename T::element_type;

public:
  using value_type = T;

  vector_wrapper_t() {}
  bool empty() {
    std::lock_guard<std::mutex> lock_g{mutex_};
    return container_.empty();
  }
  T back() {
    std::lock_guard<std::mutex> lock_g{mutex_};
    return container_.back();
  }

  template <
      typename Container,
      typename = std::enable_if_t<std::is_convertible_v<
          typename decltype(std::declval<Container>().begin())::value_type,
          underlying_type>>>
  void push_back(Container const &eps) {
    std::lock_guard<std::mutex> lock_g{mutex_};
    for (auto const &elem : eps) {
      container_.push_back(std::make_shared<underlying_type>(elem));
    }
  }

  void push_back(underlying_type &&ep) {
    container_.push_back(std::make_shared<underlying_type>(std::move(ep)));
  }

  typename std::vector<T>::size_type size() { return container_.size(); }

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

using endpoint_ptr = std::shared_ptr<custom_endpoint_t>;
using endpoint_ptr_list = vector_wrapper_t<endpoint_ptr>;

struct shared_data_t {
  std::thread::id thread_id{};
  std::uint32_t web_id{};
  proxy_type_e proxy_type;
  mutable std::set<uint32_t> shared_web_ids{};
  std::vector<custom_endpoint_t> eps{};
};

struct http_result_t {
  std::string response_body{};
  std::size_t status_code{};
};

struct posted_data_t {
  std::string url{};
  std::promise<http_result_t> promise;
};

using NewProxySignal = signals2::signal<void(shared_data_t const &)>;
using promise_container = utilities::threadsafe_cv_container<posted_data_t>;

void swap(custom_endpoint_t &a, custom_endpoint_t &b);

class global_proxy_repo_t {
  struct proxy_unique_info {
    std::size_t web_id{};
    std::size_t proxy_count{};
  };

public:
  using proxy_info_map_t = std::map<std::thread::id, proxy_unique_info>;

  NewProxySignal *new_ep_signal() { return &new_endpoints_signal_; }
  proxy_info_map_t *get_thread_proxy_info() { return &proxy_info_set_; }

  static promise_container &get_promise_container();
  static void background_proxy_fetcher(net::io_context &);

private:
  NewProxySignal new_endpoints_signal_;
  proxy_info_map_t proxy_info_set_{};
};

using proxy_info_map_t = global_proxy_repo_t::proxy_info_map_t;

struct proxy_base_params_t {
  net::io_context &io_;
  NewProxySignal &signal_;
  proxy_configuration_t &config_;
  proxy_info_map_t &proxy_info_map;
  std::thread::id thread_id;
  std::uint32_t web_id;
  std::string filename{};
};

class proxy_base_t {
protected:
  proxy_base_params_t &param_;
  std::size_t proxies_used_{};

  extraction_data_t current_extracted_data_;
  std::mutex mutex_{};
  std::size_t count_{};
  endpoint_ptr_list endpoints_;
  std::atomic_bool has_error_ = false;
  std::atomic_bool confirm_count_ = !param_.config_.count_target.empty();

protected:
  void load_proxy_file();
  void save_proxies_to_file();
  virtual extraction_data_t get_remain_count();
  virtual void get_more_proxies();

public:
  using Property = proxy_property_e;
  using value_type = endpoint_ptr;

  proxy_base_t(proxy_base_params_t &, std::string const &);
  virtual ~proxy_base_t();

  endpoint_ptr next_endpoint();
  proxy_type_e type() const;
  void add_more(shared_data_t const &);
  auto total_used() const { return proxies_used_; }
  void total_used(int val) { proxies_used_ += val; }
};

struct http_proxy_t final : proxy_base_t {
  http_proxy_t(proxy_base_params_t &);
  ~http_proxy_t() {}
};

struct socks5_proxy_t final : proxy_base_t {
  socks5_proxy_t(proxy_base_params_t &);
  ~socks5_proxy_t() {}
};
} // namespace wudi_server
