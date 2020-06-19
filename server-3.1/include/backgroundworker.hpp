#pragma once

#include "number_stream.hpp"
#include "safe_proxy.hpp"
#include "sockets_interface.hpp"
#include "utilities.hpp"
#include <boost/asio/ssl/context.hpp>
#include <fstream>
#include <optional>

namespace wudi_server {

class background_worker_t;
using utilities::task_status_e;
using utilities::upload_result_t;
using utilities::website_result_t;

namespace asio = boost::asio;

enum class website_type_e {
  Unknown,
  AutoHomeRegister,
  JJGames,
  PPSports,
  Qunar,
  WatchHome,
  Wines,
  Xpuji,
  PcAuto
};

class background_worker_t {
public:
  background_worker_t(
      website_result_t &&, std::vector<upload_result_t> &&,
      std::shared_ptr<utilities::internal_task_result_t> task_result,
      asio::ssl::context &);
  background_worker_t(
      utilities::atomic_task_t old_task,
      std::shared_ptr<utilities::internal_task_result_t> task_result,
      asio::ssl::context &);
  ~background_worker_t();

  void proxy_callback_signal(NewProxySignal *);
  void proxy_info_map(proxy_info_map_t *);
  task_status_e run();
  website_type_e type() const { return website_type_; }
  auto &number_stream() { return number_stream_; }
  auto task_result() { return task_result_ptr_; }
  std::string filename() { return input_filename; }

private:
  bool open_output_files();
  task_status_e set_website_type();
  task_status_e setup_proxy_provider();
  task_status_e start_operations();
  void on_data_result_obtained(search_result_type_e, std::string_view);
  utilities::task_status_e run_new_task();
  utilities::task_status_e run_number_crawler();
  utilities::task_status_e continue_old_task();

private:
  asio::ssl::context &ssl_context_;
  std::unique_ptr<proxy_provider_t> proxy_provider_;
  website_type_e website_type_;
  std::shared_ptr<number_stream_t> number_stream_;
  website_result_t website_info_;
  std::vector<upload_result_t> uploads_info_;
  std::shared_ptr<utilities::internal_task_result_t> task_result_ptr_;
  std::shared_ptr<database_connector_t> db_connector;
  std::string input_filename{};
  std::ifstream input_file;
  std::optional<utilities::atomic_task_t> atomic_task_;
  std::optional<asio::io_context> io_context_;
  std::vector<std::unique_ptr<sockets_interface>> sockets_;
  NewProxySignal *new_proxy_signal_{nullptr};
  proxy_info_map_t *proxy_info_map_{nullptr};
  proxy_base_params_t *proxy_parameters_{nullptr};
  boost::signals2::connection signal_connector_;
  std::optional<proxy_configuration_t> proxy_config_;
};
} // namespace wudi_server
