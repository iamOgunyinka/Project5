#pragma once

// need this include guide to remove msvc min/max macro during compilation
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "safe_proxy.hpp"
#include <boost/signals2.hpp>
#include <curl/curl.h>
#include <functional>
#include <set>
#include <vector>

namespace wudi_server {

namespace utilities {
class number_stream_t;
enum class search_result_type_e;
} // namespace utilities

struct curl_wrapper_t {
  curl_wrapper_t() { curl_global_init(CURL_GLOBAL_ALL); }
  ~curl_wrapper_t() { curl_global_cleanup(); }
};
template <typename T> struct custom_curl_list_t {
private:
  curl_slist *list;

public:
  custom_curl_list_t() : list{nullptr} {}
  void append(T const &value) { list = curl_slist_append(list, value); }

  operator curl_slist *() { return list; }
  ~custom_curl_list_t() {
    if (list) {
      curl_slist_free_all(list);
    }
  }
}; // namespace wudi_server

struct single_connect_info_t {
  CURL *easy_interface{curl_easy_init()};
  std::array<char, CURL_ERROR_SIZE> error_buffer{};
  std::vector<char> header_buffer{};
  std::vector<char> body_buffer{};
  custom_curl_list_t<char const *> headers{};

  single_connect_info_t() = default;
  ~single_connect_info_t() {
    if (easy_interface) {
      curl_easy_cleanup(easy_interface);
    }
    easy_interface = nullptr;
  }
};

std::string get_current_time();
std::map<boost::string_view, boost::string_view>
parse_headers(std::string_view const &);
std::size_t data_writer(char *buffer, std::size_t, std::size_t nmemb,
                        void *user_data);
std::size_t header_writer(char *buffer, std::size_t, std::size_t nmemb,
                          void *user_data);
std::string get_proxy_string(std::optional<wudi_server::endpoint_ptr> const &);

class jj_games_single_interface {
public:
  using my_signal = boost::signals2::signal<void(
      utilities::search_result_type_e, std::string_view)>;

public:
  jj_games_single_interface(bool &stopped, safe_proxy &proxy_provider,
                            utilities::number_stream_t &numbers);
  void start_connect();
  auto &signal() { return signal_; }

private:
  void create_jj_games_interface();
  void initialize_async_sockets();
  void process_result(CURLcode);
  void send_next();
  void current_proxy_assign_prop(ProxyProperty);
  void choose_next_proxy();
  void perform_action();

private:
  utilities::number_stream_t &numbers_;
  bool &stopped_;
  std::unique_ptr<single_connect_info_t> connection_;
  safe_proxy &proxy_provider_;
  wudi_server::endpoint_ptr current_proxy{};
  std::string phone_number{};
  my_signal signal_;
};

} // namespace wudi_server
