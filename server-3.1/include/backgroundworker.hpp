#pragma once
#include "all_sockets.hpp"
#include "database_connector.hpp"

#include <optional>
#include <variant>

namespace wudi_server {
class background_worker_t;
namespace asio = boost::asio;
namespace http = boost::beast::http;

enum class website_type_e {
  Unknown,
  AutoHomeRegister,
  JJGames,
  PPSports,
  Qunar,
  WatchHome
};

class background_worker_t {
public:
  background_worker_t(website_result_t &&, std::vector<upload_result_t> &&,
                      std::shared_ptr<internal_task_result_t> task_result,
                      net::ssl::context &);
  background_worker_t(atomic_task_t old_task,
                      std::shared_ptr<internal_task_result_t> task_result,
                      net::ssl::context &);
  ~background_worker_t();

  void proxy_callback_signal(NewProxySignal *signal);
  task_status_e run();
  website_type_e type() const { return website_type_; }
  auto &number_stream() { return number_stream_; }
  auto task_result() { return task_result_ptr_; }
  std::string filename() { return input_filename; }

private:
  bool open_output_files();
  void on_data_result_obtained(search_result_type_e, std::string_view);
  task_status_e run_new_task();
  task_status_e run_number_crawler();
  task_status_e continue_old_task();

  template <typename... Args>
  std::unique_ptr<vsocket_type> get_socket(proxy_type_e const proxy_type,
                                           Args &&... args) {
    if (proxy_type == proxy_type_e::http_https_proxy) {
      switch (website_type_) {
      case website_type_e::AutoHomeRegister:
        return std::make_unique<vsocket_type>(std::in_place_type<ah_https>,
                                              std::forward<Args>(args)...);
      case website_type_e::JJGames:
        return nullptr;
      case website_type_e::PPSports:
        return std::make_unique<vsocket_type>(std::in_place_type<pps_http>,
                                              std::forward<Args>(args)...);
      case website_type_e::Qunar:
        return std::make_unique<vsocket_type>(std::in_place_type<qn_http>,
                                              std::forward<Args>(args)...);
      case website_type_e::WatchHome:
        return std::make_unique<vsocket_type>(std::in_place_type<wh_http>,
                                              std::forward<Args>(args)...);
      }
    } else {
      switch (website_type_) {
      case website_type_e::AutoHomeRegister:
        return std::make_unique<vsocket_type>(std::in_place_type<ah_sk5>,
                                              ssl_context_,
                                              std::forward<Args>(args)...);
      case website_type_e::JJGames:
        return std::make_unique<vsocket_type>(std::in_place_type<jjgames_sk5>,
                                              ssl_context_,
                                              std::forward<Args>(args)...);
      case website_type_e::Qunar:
        return std::make_unique<vsocket_type>(std::in_place_type<qn_sk5>,
                                              ssl_context_,
                                              std::forward<Args>(args)...);
      case website_type_e::PPSports:
        return std::make_unique<vsocket_type>(std::in_place_type<pps_sk5>,
                                              std::forward<Args>(args)...);
      case website_type_e::WatchHome:
        return std::make_unique<vsocket_type>(std::in_place_type<wh_sk5>,
                                              std::forward<Args>(args)...);
      }
    }
    throw std::runtime_error("specified socket type unknown");
  }

private:
  net::ssl::context &ssl_context_;
  std::unique_ptr<proxy_provider_t> proxy_provider_;
  website_type_e website_type_;
  std::shared_ptr<number_stream_t> number_stream_;
  website_result_t website_info_;
  std::vector<upload_result_t> uploads_info_;
  std::shared_ptr<internal_task_result_t> task_result_ptr_;
  std::shared_ptr<database_connector_t> db_connector;
  std::string input_filename{};
  std::ifstream input_file;
  std::optional<atomic_task_t> atomic_task_;
  std::optional<net::io_context> io_context_;
  std::vector<std::unique_ptr<vsocket_type>> sockets_;
  NewProxySignal *new_proxy_signal_{nullptr};
  boost::signals2::connection signal_connector_;
  std::unique_ptr<proxy_configuration_t> proxy_config_;
};
} // namespace wudi_server
