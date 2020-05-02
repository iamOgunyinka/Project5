#include "backgroundworker.hpp"
#include "database_connector.hpp"
#include <filesystem>

namespace wudi_server {
background_worker_t::~background_worker_t() {
  signal_connector_.disconnect();
  sockets_.clear();
  io_context_.reset();
  spdlog::info("Closing all files");
}

background_worker_t::background_worker_t(
    website_result_t &&website, std::vector<upload_result_t> &&uploads,
    std::shared_ptr<utilities::internal_task_result_t> task_result,
    net::ssl::context &ssl_context)
    : ssl_context_{ssl_context}, website_info_{std::move(website)},
      uploads_info_{std::move(uploads)}, task_result_ptr_{task_result},
      website_type_{website_type_e::Unknown} {}

background_worker_t::background_worker_t(
    utilities::atomic_task_t old_task,
    std::shared_ptr<utilities::internal_task_result_t> task_result,
    net::ssl::context &ssl_context)
    : ssl_context_{ssl_context}, website_info_{}, uploads_info_{},
      task_result_ptr_{task_result}, website_type_{website_type_e::Unknown},
      atomic_task_{std::move(old_task)} {}

void background_worker_t::on_data_result_obtained(
    utilities::search_result_type_e type, std::string_view number) {
  using utilities::search_result_type_e;
  auto &processed = task_result_ptr_->processed;
  auto &total = task_result_ptr_->total_numbers;
  ++processed;
  switch (type) {
  case search_result_type_e::NotRegistered:
    ++task_result_ptr_->ok_count;
    task_result_ptr_->ok_file << number << "\n";
    task_result_ptr_->ok_file.flush();
    break;
  case search_result_type_e::Registered2:
    ++task_result_ptr_->ok_count;
    task_result_ptr_->ok2_file << number << "\n";
    task_result_ptr_->ok2_file.flush();
    break;
  case search_result_type_e::Registered:
    ++task_result_ptr_->not_ok_count;
    task_result_ptr_->not_ok_file << number << "\n";
    task_result_ptr_->not_ok_file.flush();
    break;
  case search_result_type_e::Unknown:
    ++task_result_ptr_->unknown_count;
    task_result_ptr_->unknown_file << number << "\n";
    task_result_ptr_->unknown_file.flush();
    break;
  case search_result_type_e::RequestStop:
    --processed; // `processed` will *always* be greater than 0
    task_result_ptr_->stop();
    task_result_ptr_->operation_status = task_status_e::AutoStopped;
    return;
  }

  bool const signallable = (processed % proxy_config_->max_socket) == 0;
  if (signallable) {
    db_connector->update_task_progress(*task_result_ptr_);
  }
  if (processed > 10 && ((processed - 10) > task_result_ptr_->total_numbers)) {
    // if we get here, there's a problem
    task_result_ptr_->stop();
    task_result_ptr_->operation_status = task_status_e::AutoStopped;
  }
}

bool background_worker_t::read_proxy_configuration() {
  proxy_config_.emplace();
  std::ifstream in_file{"./proxy_config.json"};
  if (!in_file)
    return false;
  try {
    json json_file;
    in_file >> json_file;
    json::object_t root_object = json_file.get<json::object_t>();
    auto proxy_field = root_object["proxy"].get<json::object_t>();
    auto available_protocols =
        proxy_field["#available_protocols"].get<json::array_t>();
    std::size_t const highest_index = available_protocols.size();
    std::size_t const protocol_index =
        proxy_field["protocol"].get<json::number_integer_t>();
    proxy_config_->hostname = proxy_field["host"].get<json::string_t>();
    proxy_config_->proxy_target = proxy_field["target"].get<json::string_t>();
    proxy_config_->count_target =
        proxy_field["count_target"].get<json::string_t>();
    proxy_config_->proxy_username =
        proxy_field["username"].get<json::string_t>();
    proxy_config_->proxy_password =
        proxy_field["password"].get<json::string_t>();
    proxy_config_->share_proxy = proxy_field["share"].get<json::boolean_t>();
    proxy_config_->max_socket =
        proxy_field["socket_count"].get<json::number_integer_t>();
    proxy_config_->fetch_once =
        proxy_field["per_fetch"].get<json::number_integer_t>();
    if (protocol_index >= highest_index)
      return false;
    switch (protocol_index) {
    case 0:
      proxy_config_->proxy_protocol = proxy_type_e::socks5;
      break;
    case 1:
      proxy_config_->proxy_protocol = proxy_type_e::http_https_proxy;
      break;
    default:
      spdlog::error("unknown proxy procotol specified");
      return false;
    }
  } catch (std::exception const &e) {
    spdlog::error("[read_proxy_configuration] {}", e.what());
    return false;
  }
  return true;
}

bool background_worker_t::open_output_files() {
  using utilities::create_file_directory;

  std::filesystem::path parent_directory{std::filesystem::current_path()};
  auto const abs_path{std::filesystem::absolute(parent_directory) / "over" /
                      website_info_.alias};
  std::string current_date{}, current_time{};
  auto const current_time_t = std::time(nullptr);
  if (std::size_t const count =
          utilities::timet_to_string(current_date, current_time_t, "%Y_%m_%d");
      count != std::string::npos) {
    current_date.resize(count);
  } else {
    // this is called if and only if we could not do the proper formatting
    current_date = std::to_string(current_time_t);
  }
  if (std::size_t const count =
          utilities::timet_to_string(current_time, current_time_t, "%H_%M_%S");
      count != std::string::npos) {
    current_time.resize(count);
  } else {
    current_time = std::to_string(current_time_t);
  }

  if (task_result_ptr_->not_ok_filename.string().empty()) {
    auto const suffix{std::filesystem::path{current_date} /
                      (current_time + ".txt")};
    task_result_ptr_->not_ok_filename = abs_path / "not_ok" / suffix;
    task_result_ptr_->ok_filename = abs_path / "ok" / suffix;
    task_result_ptr_->ok2_filename = abs_path / "ok2" / suffix;
    task_result_ptr_->unknown_filename = abs_path / "unknown" / suffix;
  }

  if (!(create_file_directory(task_result_ptr_->not_ok_filename) &&
        create_file_directory(task_result_ptr_->ok_filename) &&
        create_file_directory(task_result_ptr_->ok2_filename) &&
        create_file_directory(task_result_ptr_->unknown_filename))) {
    // create some error messages and fire out
    return false;
  }
  task_result_ptr_->not_ok_file.open(task_result_ptr_->not_ok_filename,
                                     std::ios::out | std::ios::app);
  task_result_ptr_->ok_file.open(task_result_ptr_->ok_filename,
                                 std::ios::out | std::ios::app);
  task_result_ptr_->ok2_file.open(task_result_ptr_->ok2_filename,
                                  std::ios::out | std::ios::app);
  task_result_ptr_->unknown_file.open(task_result_ptr_->unknown_filename,
                                      std::ios::out | std::ios::app);

  return task_result_ptr_->not_ok_file.is_open() &&
         task_result_ptr_->ok_file.is_open() &&
         task_result_ptr_->ok2_file.is_open() &&
         task_result_ptr_->unknown_file.is_open();
}

utilities::task_status_e background_worker_t::run_number_crawler() {
  using utilities::task_status_e;
  if (!open_output_files()) {
    spdlog::error("OpenOutputFiles failed");
    if (std::filesystem::exists(input_filename)) {
      if (input_file.is_open())
        input_file.close();
      std::filesystem::remove(input_filename);
    }
    spdlog::error("unable to open output files");
    return task_status_e::Erred;
  }

  db_connector = database_connector_t::s_get_db_connector();
  task_result_ptr_->operation_status = task_status_e::Ongoing;
  bool &stopped = task_result_ptr_->stopped();
  stopped = false;
  if (website_type_ == website_type_e::Unknown) {
    if (website_info_.address.find("jjgames") != std::string::npos) {
      website_type_ = website_type_e::JJGames;
    } else if (website_info_.address.find("autohome") != std::string::npos) {
      website_type_ = website_type_e::AutoHomeRegister;
    } else if (website_info_.address.find("ppsports") != std::string::npos) {
      website_type_ = website_type_e::PPSports;
    } else if (website_info_.address.find("watch") != std::string::npos) {
      website_type_ = website_type_e::WatchHome;
    } else {
      spdlog::error("Type not found");
      return task_status_e::Erred;
    }
  }

  auto callback = std::bind(&background_worker_t::on_data_result_obtained, this,
                            std::placeholders::_1, std::placeholders::_2);
  if (!db_connector->set_input_files(
          input_filename, task_result_ptr_->ok_filename.string(),
          task_result_ptr_->ok2_filename.string(),
          task_result_ptr_->not_ok_filename.string(),
          task_result_ptr_->unknown_filename.string(),
          task_result_ptr_->task_id)) {
    spdlog::error("Could not set input files");
    return task_status_e::Erred;
  }
  // we delayed construction of safe_proxy/io_context until now
  if (!read_proxy_configuration()) {
    return task_status_e::Erred;
  }

  auto const thread_id = std::this_thread::get_id();
  auto const web_id = task_result_ptr_->website_id;
  io_context_.emplace();
  switch (proxy_config_->proxy_protocol) {
  case proxy_type_e::http_https_proxy:
    proxy_provider_.reset(new http_proxy(*io_context_, *new_proxy_signal_,
                                         *proxy_config_, thread_id, web_id));
    break;
  case proxy_type_e::socks5:
    proxy_provider_.reset(new socks5_proxy(*io_context_, *new_proxy_signal_,
                                           *proxy_config_, thread_id, web_id));
    break;
  default:
    return task_status_e::Erred;
  }

  // here, when this signal is emitted, all workers subscribe to it so they
  // can have copies of the new proxies obtained
  if (proxy_config_->share_proxy) {
    signal_connector_ = new_proxy_signal_->connect([this](auto &&... args) {
      proxy_provider_->add_more(std::forward<decltype(args)>(args)...);
    });
  }

  {
    sockets_.reserve(static_cast<std::size_t>(proxy_config_->max_socket));
    proxy_config_->max_socket = std::max<int>(1, proxy_config_->max_socket);
    for (int i = 0; i != proxy_config_->max_socket; ++i) {
      auto socket = get_socket(proxy_provider_->type(), stopped, *io_context_,
                               *proxy_provider_, *number_stream_);
      sockets_.push_back(std::move(socket));
    }
    for (auto &socket : sockets_) {
      std::visit(
          [=](auto &&sock) {
            sock.signal().connect(callback);
            sock.start_connect();
          },
          *socket);
    }

    io_context_->run();
  }

  if (task_result_ptr_->operation_status == task_status_e::Ongoing) {
    if (number_stream_->empty()) {
      task_result_ptr_->operation_status = task_status_e::Completed;
    } else {
      // this will hardly ever happen, if it does, put it in a stop state
      task_result_ptr_->stop();
      task_result_ptr_->operation_status = task_status_e::Stopped;
    }
  }
  return task_result_ptr_->operation_status;
}

utilities::task_status_e background_worker_t::continue_old_task() {
  using utilities::atomic_task_t;
  using utilities::task_status_e;

  auto &task = atomic_task_.value();
  if (task.website_address.find("autohome") != std::string::npos) {
    website_type_ = website_type_e::AutoHomeRegister;
  } else if (task.website_address.find("jjgames") != std::string::npos) {
    website_type_ = website_type_e::JJGames;
  } else if (task.website_address.find("ppsports") != std::string::npos) {
    website_type_ = website_type_e::PPSports;
  } else if (task.website_address.find("watch") != std::string::npos) {
    website_type_ = website_type_e::WatchHome;
  }

  input_filename = task.input_filename;
  if (task_result_ptr_->total_numbers == 0) {
    if (atomic_task_->total == 0) {
      using utilities::get_file_content;
      using utilities::is_valid_number;

      task_result_ptr_->total_numbers = 0;
      get_file_content<std::string>(input_filename, is_valid_number,
                                    [this](std::string_view) mutable {
                                      ++task_result_ptr_->total_numbers;
                                    });
    } else {
      task_result_ptr_->total_numbers = atomic_task_->total;
    }
  }
  if (task_result_ptr_->total_numbers == 0) {
    spdlog::error("Total numbers = 0");
    return task_status_e::Erred;
  }
  input_file.open(input_filename, std::ios::in);
  if (!input_file) {
    if (std::filesystem::exists(input_filename)) {
      std::filesystem::remove(input_filename);
    }
    task_result_ptr_->operation_status = task_status_e::Erred;
    spdlog::error("Could not open input file: {}", input_filename);
    return task_status_e::Erred;
  }
  number_stream_ = std::make_unique<utilities::number_stream_t>(input_file);
  return run_number_crawler();
}

utilities::task_status_e background_worker_t::run_new_task() {
  using utilities::task_status_e;
  {
    input_filename = "./{}.txt"_format(
        utilities::get_random_string(utilities::get_random_integer()));
    std::ofstream out_file{input_filename};
    if (!out_file) {
      spdlog::error("Could not open out_file");
      input_filename.clear();
      task_result_ptr_->operation_status = task_status_e::Erred;
      return task_result_ptr_->operation_status;
    }
    for (std::size_t index = 0; index != uploads_info_.size(); ++index) {
      spdlog::info("name on disk: {}", uploads_info_[index].name_on_disk);
      std::ifstream in_file{uploads_info_[index].name_on_disk};
      if (!in_file)
        continue;
      out_file << in_file.rdbuf();
    }
    out_file.close();
    {
      using utilities::get_file_content;
      using utilities::is_valid_number;

      task_result_ptr_->total_numbers = 0;
      get_file_content<std::string>(input_filename, is_valid_number,
                                    [this](std::string_view) mutable {
                                      ++task_result_ptr_->total_numbers;
                                    });
      if (task_result_ptr_->total_numbers == 0)
        return task_status_e::Erred;
    }
    input_file.open(input_filename, std::ios::in);
    if (!input_file) {
      if (std::filesystem::exists(input_filename)) {
        std::filesystem::remove(input_filename);
      }
      spdlog::error("Could not open input_file");
      return task_status_e::Erred;
    }
    number_stream_ = std::make_unique<utilities::number_stream_t>(input_file);
  }
  return run_number_crawler();
}

utilities::task_status_e background_worker_t::run() {
  if (website_info_.id != 0) {
    return run_new_task();
  }
  return continue_old_task();
}

void background_worker_t::proxy_callback_signal(NewProxySignal *signal) {
  new_proxy_signal_ = signal;
}
} // namespace wudi_server
