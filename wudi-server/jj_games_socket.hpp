#pragma once

// need this include guide to remove msvc min/max macro during compilation
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "safe_proxy.hpp"
#include "utilities.hpp"
#include <boost/signals2.hpp>
#include <curl/curl.h>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#ifndef emit
#define emit
#endif

using tcp = boost::asio::ip::tcp;

namespace custom_curl {
template <typename T> struct custom_curl_list {
private:
  curl_slist *list;

public:
  custom_curl_list() : list{nullptr} {}
  void append(T const &value) { list = curl_slist_append(list, value); }

  operator curl_slist *() { return list; }
  ~custom_curl_list() {
    if (list)
      curl_slist_free_all(list);
  }
};

using CustomMap =
    wudi_server::utilities::custom_map<curl_socket_t,
                                       std::shared_ptr<tcp::socket>>;

struct LibCurlRAII {
  LibCurlRAII() { curl_global_init(CURL_GLOBAL_ALL); }
  ~LibCurlRAII() { curl_global_cleanup(); }
};

struct CurlThreadData {
  CURLM *multi_interface{nullptr};
  int still_running{};

  CurlThreadData() : multi_interface{curl_multi_init()} {}
  operator bool() { return multi_interface; }
  ~CurlThreadData() {
    if (multi_interface) {
      curl_multi_cleanup(multi_interface);
      multi_interface = nullptr;
    }
  }
};

struct ConnectInfo {
  CURL *easy_interface{curl_easy_init()};
  boost::asio::io_context &context;
  CustomMap &map_;
  std::array<char, CURL_ERROR_SIZE> error_buffer{};
  std::vector<char> header_buffer{};
  std::vector<char> body_buffer{};
  custom_curl_list<char const *> headers{};
  std::string url{};
  wudi_server::endpoint_ptr proxy{};
  std::string phone_number{};

  ConnectInfo(boost::asio::io_context &c, CustomMap &m) : context{c}, map_{m} {}
  ~ConnectInfo() {
    if (easy_interface)
      curl_easy_cleanup(easy_interface);
    easy_interface = nullptr;
  }
};

[[nodiscard]] bool
create_jj_games_interface(ConnectInfo *, CurlThreadData &glob_data,
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
using custom_curl::ConnectInfo;
using custom_curl::CurlThreadData;

class jj_games_socket {
  friend int custom_curl::close_tcp_socket(void *, curl_socket_t);
  friend int custom_curl::multi_socket_callback(CURL *, curl_socket_t, int,
                                                void *, void *);
  friend int custom_curl::multi_socket_timer_callback(CURLM *, long timeout_ms,
                                                      void *user_data);

public:
  jj_games_socket(bool &stopped, net::io_context &io,
                  safe_proxy &proxy_provider,
                  utilities::number_stream &numbers);
  ~jj_games_socket();
  CURLM *curlm_async_interface();
  void start_connect();
  boost::asio::deadline_timer &timer();
  auto &signal() { return signal_; }

private:
  void send_next(void *connect_info = nullptr);
  auto &context() { return context_; }
  bool is_stopped() const;
  void current_proxy_assign_prop(ProxyProperty, endpoint_ptr);
  void select_proxy(ConnectInfo *);
  void try_different_proxy(custom_curl::ConnectInfo *);
  void prepare_next_data(custom_curl::ConnectInfo *, std::string const &);
  void initialize_async_sockets();
  void check_multi_info();
  void on_socket_event_occurred(beast::error_code const, curl_socket_t const,
                                int, int *);
  void set_tcp_socket(int *, curl_socket_t, CURL *, int const, int const);
  void simple_timer_callback(boost::system::error_code);
  void add_tcp_socket(curl_socket_t, CURL *, int);
  void remove_tcp_socket(int *);
  void process_result(CURLcode code, ConnectInfo *);

private:
  boost::asio::io_context &context_;
  safe_proxy &proxy_provider_;
  boost::asio::deadline_timer timer_;
  utilities::number_stream &numbers_;
  bool &stopped_;

  custom_curl::CurlThreadData thread_data_{};
  std::vector<ConnectInfo *> connections{};
  std::set<int *> fd_list{};
  custom_curl::CustomMap socket_map{};
  boost::signals2::signal<void(utilities::SearchResultType, std::string_view)>
      signal_;
};
} // namespace wudi_server
