#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/signals2/signal.hpp>
#include <ctime>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "container.hpp"
#include "enumerations.hpp"

namespace net = boost::asio;
namespace ip = net::ip;
namespace signals2 = boost::signals2;

namespace woody_server {
using tcp = ip::tcp;

net::io_context &getNetworkContext();

struct extraction_data_t {
  std::time_t expireTime{};
  int productRemain{};
  int connectRemain{};
  int extractRemain{};
  bool isAvailable{false};
};

struct proxy_configuration_t {
  std::string proxyUsername{};
  std::string proxyPassword{};
  std::string proxyTarget{};
  std::string countTarget{};
  proxy_type_e proxyProtocol = proxy_type_e::http_https_proxy;
  int fetchInterval{};
  int shareProxy{};
  int maxSocket{};
  int fetchOnce{};
  int softwareVersion{};
};

std::optional<proxy_configuration_t> readProxyConfiguration();

struct custom_endpoint_t {
  tcp::endpoint endpoint{};
  std::string username{};
  std::string password{};
  int numberScanned{};
  std::time_t timeLastUsed{};

  proxy_property_e property{proxy_property_e::ProxyActive};

  custom_endpoint_t(tcp::endpoint &&ep, std::string username_,
                    std::string password_)
      : endpoint(std::move(ep)), username{std::move(username_)},
        password{std::move(password_)} {}

  operator tcp::endpoint() const { return endpoint; }
  std::string &getUsername() { return username; }
  std::string &getPassword() { return password; }
  void swap(custom_endpoint_t &);
};

template <typename T> class vector_wrapper_t {
  mutable std::mutex m_mutex{};
  std::vector<T> m_container{};
  using underlying_type = typename T::element_type;

public:
  using value_type = T;

  vector_wrapper_t() = default;
  bool empty() {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_container.empty();
  }
  T back() {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_container.back();
  }

  template <
      typename Container,
      typename = std::enable_if_t<std::is_convertible_v<
          typename decltype(std::declval<Container>().begin())::value_type,
          underlying_type>>>
  void append(Container const &eps) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    for (auto const &elem : eps) {
      m_container.push_back(std::make_shared<underlying_type>(elem));
    }
  }

  void append(underlying_type &&ep) {
    m_container.push_back(std::make_shared<underlying_type>(std::move(ep)));
  }

  typename std::vector<T>::size_type size() { return m_container.size(); }

  T &operator[](typename std::vector<T>::size_type const index) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_container[index];
  }

  T const &operator[](typename std::vector<T>::size_type const index) const {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    return m_container[index];
  }

  void removeFirstN(std::size_t const count) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    if (m_container.size() < count) {
      return m_container.clear();
    }
    m_container.erase(m_container.begin(), m_container.begin() + count);
  }
  template <typename Func> void removeIf(Func &&func) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    m_container.erase(
        std::remove_if(m_container.begin(), m_container.end(), func),
        m_container.end());
  }
  template <typename Func> void forEach(Func &&func) {
    std::lock_guard<std::mutex> lock_g{m_mutex};
    for (auto &elem : m_container) {
      std::forward<Func>(func)(elem);
    }
  }
};

using endpoint_ptr_t = std::shared_ptr<custom_endpoint_t>;
using endpoint_ptr_list_t = vector_wrapper_t<endpoint_ptr_t>;

struct shared_data_t {
  std::thread::id threadID{};
  std::uint32_t webID{};
  proxy_type_e proxyType;
  mutable std::set<uint32_t> sharedWebIDs{};
  std::vector<custom_endpoint_t> endpoints{};
};

struct http_result_t {
  std::string responseBody{};
  std::size_t statusCode{};
};

struct posted_data_t {
  std::string url{};
  std::promise<http_result_t> promise;
};

using new_proxy_signal_t = signals2::signal<void(shared_data_t const &)>;
using promise_container_t = threadsafe_list_t<posted_data_t>;

void swap(custom_endpoint_t &a, custom_endpoint_t &b);

class global_proxy_repo_t {
  struct proxy_unique_info_t {
    std::size_t webID{};
    std::size_t proxyCount{};
  };

public:
  using proxy_info_map_t = std::map<std::thread::id, proxy_unique_info_t>;

  new_proxy_signal_t *newEndpointSignal() { return &m_newEndpointsSignal; }
  proxy_info_map_t *getThreadProxyInfo() { return &m_proxyInfoSet; }

  static promise_container_t &getPromiseContainer();
  [[noreturn]] static void backgroundProxyFetcher(net::io_context &);

private:
  new_proxy_signal_t m_newEndpointsSignal;
  proxy_info_map_t m_proxyInfoSet{};
};

using proxy_info_map_t = global_proxy_repo_t::proxy_info_map_t;

struct proxy_base_params_t {
  net::io_context &m_ioContext;
  new_proxy_signal_t &m_signal;
  proxy_configuration_t &m_config;
  proxy_info_map_t &proxyInfoMap;
  std::thread::id threadID;
  std::uint32_t webID;
  std::string filename{};
};

class proxy_base_t {
protected:
  proxy_base_params_t &m_proxyParam;
  std::size_t m_proxiesUsed{};

  extraction_data_t m_currentExtractedData{};
  std::mutex m_mutex{};
  std::size_t m_count{};
  endpoint_ptr_list_t m_endpoints{};
  std::atomic_bool m_hasError = false;
  std::atomic_bool m_confirmCount = !m_proxyParam.m_config.countTarget.empty();

protected:
  void loadProxyFile();
  void saveProxiesToFile();
  virtual extraction_data_t getRemainCount();
  virtual void getMoreProxies();

public:
  using value_type = endpoint_ptr_t;
  proxy_base_t(proxy_base_params_t &, std::string const &);
  virtual ~proxy_base_t();
  endpoint_ptr_t nextEndpoint();
  proxy_type_e type() const;
  void addMore(shared_data_t const &);
  auto totalUsed() const { return m_proxiesUsed; }
  void totalUsed(int val) { m_proxiesUsed += val; }
};

struct http_proxy_t final : proxy_base_t {
  explicit http_proxy_t(proxy_base_params_t &);
  ~http_proxy_t() override = default;
};

struct socks5_proxy_t final : proxy_base_t {
  explicit socks5_proxy_t(proxy_base_params_t &);
  ~socks5_proxy_t() override = default;
};
} // namespace woody_server
