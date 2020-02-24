#pragma once

// need this include guide to remove msvc min/max macro during compilation
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "safe_proxy.hpp"
#include <boost/signals2.hpp>
#include <curl/curl.h>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

using tcp = boost::asio::ip::tcp;

namespace wudi_server {
namespace utilities {
class number_stream_t;
enum class search_result_type_e;
} // namespace utilities
} // namespace wudi_server

namespace custom_curl {
template <typename Key, typename Value> class custom_map {
  std::map<Key, Value> map_{};

public:
  custom_map() = default;

  bool contains(Key const &key) { return map_.find(key) != map_.cend(); }

  std::optional<Value> value(Key const &key) {
    if (auto iter = map_.find(key); iter == map_.cend())
      return std::nullopt;
    else
      return iter->second;
  }

  void insert(Key key, Value const &value) {
    map_.emplace(std::forward<Key>(key), value);
  }

  void clear() {
    for (auto &data : map_) {
      boost::beast::error_code ec{};
      if (data.second && data.second->is_open())
        data.second->close(ec);
    }
  }

  bool remove(Key const &key) {
    if (auto iter = map_.find(key); iter != map_.cend()) {
      iter->second.reset();
      map_.erase(iter);
      return true;
    }
    return false;
  }
};

template <typename T> struct custom_curl_list_t {
private:
  curl_slist *list;

public:
  custom_curl_list_t() : list{nullptr} {}
  void append(T const &value) { list = curl_slist_append(list, value); }

  operator curl_slist *() { return list; }
  ~custom_curl_list_t() {
    if (list)
      curl_slist_free_all(list);
  }
};

using custom_map_t = custom_map<curl_socket_t, std::shared_ptr<tcp::socket>>;

struct curl_wrapper_t {
  curl_wrapper_t() { curl_global_init(CURL_GLOBAL_ALL); }
  ~curl_wrapper_t() { curl_global_cleanup(); }
};

struct curl_thread_data_t {
  CURLM *multi_interface{nullptr};
  int still_running{};

  curl_thread_data_t() : multi_interface{curl_multi_init()} {}
  operator bool() { return multi_interface; }
  ~curl_thread_data_t() {
    if (multi_interface) {
      curl_multi_cleanup(multi_interface);
      multi_interface = nullptr;
    }
  }
};

struct connect_info_t {
  CURL *easy_interface{curl_easy_init()};
  boost::asio::io_context &context;
  custom_map_t &map_;
  std::array<char, CURL_ERROR_SIZE> error_buffer{};
  std::vector<char> header_buffer{};
  std::vector<char> body_buffer{};
  custom_curl_list_t<char const *> headers{};
  std::string url{};
  wudi_server::endpoint_ptr proxy{};
  std::string phone_number{};

  connect_info_t(boost::asio::io_context &c, custom_map_t &m)
      : context{c}, map_{m} {}
  ~connect_info_t() {
    if (easy_interface)
      curl_easy_cleanup(easy_interface);
    easy_interface = nullptr;
  }
};

[[nodiscard]] bool
create_jj_games_interface(connect_info_t *, curl_thread_data_t &glob_data,
                          wudi_server::endpoint_ptr proxy_server_ip,
                          std::string const &number);
std::string get_current_time();
std::map<boost::string_view, boost::string_view>
parse_headers(std::string_view const &);
std::string get_proxy_string(std::optional<wudi_server::endpoint_ptr> const &);
std::size_t data_writer(char *buffer, std::size_t, std::size_t nmemb,
                        void *user_data);
std::size_t header_writer(char *buffer, std::size_t, std::size_t nmemb,
                          void *user_data);
curl_socket_t open_tcp_socket(void *, curlsocktype, curl_sockaddr *);
int close_tcp_socket(void *, curl_socket_t);
int multi_socket_callback(CURL *, curl_socket_t, int, void *, void *);
int multi_socket_timer_callback(CURLM *, long timeout_ms, void *user_data);
} // namespace custom_curl

namespace wudi_server {
using custom_curl::connect_info_t;
using custom_curl::curl_thread_data_t;

class jj_games_socket_t {
  friend int custom_curl::close_tcp_socket(void *, curl_socket_t);
  friend int custom_curl::multi_socket_callback(CURL *, curl_socket_t, int,
                                                void *, void *);
  friend int custom_curl::multi_socket_timer_callback(CURLM *, long timeout_ms,
                                                      void *user_data);

public:
  jj_games_socket_t(bool &stopped, net::io_context &io,
                    safe_proxy &proxy_provider,
                    utilities::number_stream_t &numbers);
  ~jj_games_socket_t();
  CURLM *curlm_async_interface();
  void start_connect();
  boost::asio::deadline_timer &timer();
  auto &signal() { return signal_; }

private:
  void send_next(void *connect_info = nullptr);
  auto &context() { return context_; }
  bool is_stopped() const;
  void current_proxy_assign_prop(ProxyProperty, endpoint_ptr);
  void select_proxy(connect_info_t *);
  void try_different_proxy(custom_curl::connect_info_t *);
  void prepare_next_data(custom_curl::connect_info_t *, std::string const &);
  void initialize_async_sockets();
  void check_multi_info();
  void on_socket_event_occurred(beast::error_code const, curl_socket_t const,
                                int, int *);
  void set_tcp_socket(int *, curl_socket_t, CURL *, int const, int const);
  void simple_timer_callback(boost::system::error_code);
  void add_tcp_socket(curl_socket_t, CURL *, int);
  void remove_tcp_socket(int *);
  void process_result(CURLcode code, connect_info_t *);

private:
  boost::asio::io_context &context_;
  safe_proxy &proxy_provider_;
  boost::asio::deadline_timer timer_;
  utilities::number_stream_t &numbers_;
  bool &stopped_;

  custom_curl::curl_thread_data_t thread_data_{};
  std::vector<connect_info_t *> connections{};
  std::set<int *> fd_list{};
  custom_curl::custom_map_t socket_map{};
  boost::signals2::signal<void(utilities::search_result_type_e,
                               std::string_view)>
      signal_;
};
} // namespace wudi_server
