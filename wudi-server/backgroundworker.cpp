#include "backgroundworker.hpp"
#include "auto_home_sock.hpp"
#include "database_connector.hpp"
#include "jj_games_socket.hpp"
#include <filesystem>
#include <list>

namespace wudi_server {

auto create_file_directory(std::filesystem::path const &path) -> bool {
  std::error_code ec{};
  auto f = std::filesystem::absolute(path.parent_path(), ec);
  if (ec)
    return false;
  ec = {};
  std::filesystem::create_directories(f, ec);
  return !ec;
};

std::string const background_worker_t::http_proxy_filename{
    "./http_proxy_servers.txt"};

background_worker_t::~background_worker_t() {
  task_result_ptr_->not_ok_file.close();
  task_result_ptr_->ok_file.close();
  task_result_ptr_->unknown_file.close();
  spdlog::info("Closing all files");
}

background_worker_t::background_worker_t(
    website_result_t &&website, std::vector<upload_result_t> &&uploads,
    std::shared_ptr<utilities::atomic_task_result_t> task_result,
    net::io_context &context)
    : website_info_{std::move(website)}, uploads_info_{std::move(uploads)},
      context_{context}, safe_proxy_{context},
      task_result_ptr_{task_result}, type_{website_type::Unknown} {}

background_worker_t::background_worker_t(
    utilities::atomic_task_t old_task,
    std::shared_ptr<utilities::atomic_task_result_t> task_result,
    net::io_context &context)
    : website_info_{}, uploads_info_{}, context_{context}, safe_proxy_{context},
      task_result_ptr_{task_result}, type_{website_type::Unknown},
      atomic_task_{std::move(old_task)} {}

void background_worker_t::on_data_result_obtained(
    utilities::search_result_type_e type, std::string_view number) {
  using utilities::search_result_type_e;
  auto &processed = task_result_ptr_->processed;
  auto &total = task_result_ptr_->total_numbers;
  ++processed;
  switch (type) {
  case search_result_type_e::NotRegistered:
    task_result_ptr_->ok_file << number << "\n";
    task_result_ptr_->ok_file.flush();
    break;
  case search_result_type_e::Registered:
    task_result_ptr_->not_ok_file << number << "\n";
    task_result_ptr_->not_ok_file.flush();
    break;
  case search_result_type_e::Unknown:
    task_result_ptr_->unknown_file << number << "\n";
    task_result_ptr_->unknown_file.flush();
    break;
  }
  spdlog::info("Processed: {} of {}", processed, total);
  if (processed == total) {
    if (std::filesystem::exists(input_filename)) {
      if (input_file.is_open())
        input_file.close();
      std::filesystem::remove(input_filename);
    }
    task_result_ptr_->operation_status = utilities::task_status_e::Completed;
    spdlog::info("Done processing task -> {}:{}", task_result_ptr_->task_id,
                 task_result_ptr_->website_id);
  }

  int const progress = (processed * 100) / total;
  auto &signal = task_result_ptr_->progress_signal();
  if (progress > current_progress_) {
    current_progress_ = progress;
    signal(task_result_ptr_->task_id, processed,
           task_result_ptr_->operation_status);
  }
}

bool background_worker_t::save_status_to_persistence(
    std::string const &filename) {
  auto website_address = [](website_type const type) {
    switch (type) { // we can easily add more websites in the future
    case website_type::AutoHomeRegister:
      return "autohome";
    case website_type::JJGames:
      return "jjgames";
    }
    return "";
  };
  auto replace_special_chars = [](std::string &str) {
    for (std::string::size_type i = 0; i != str.size(); ++i) {
      if (str[i] == '\\')
        str[i] = '#';
    }
  };
  using utilities::atomic_task_t;
  auto db_connector = wudi_server::database_connector_t::s_get_db_connector();
  atomic_task_t stopped_task{};
  stopped_task.type_ = atomic_task_t::task_type::stopped;
  stopped_task.task.emplace<atomic_task_t::stopped_task>();
  stopped_task.processed = task_result_ptr_->processed;
  stopped_task.total = task_result_ptr_->total_numbers;
  stopped_task.task_id = task_result_ptr_->task_id;
  stopped_task.website_id = task_result_ptr_->website_id;

  auto &task =
      std::get<utilities::atomic_task_t::stopped_task>(stopped_task.task);
  task.input_filename = filename;

  task.not_ok_filename = task_result_ptr_->not_ok_filename.string();
  task.ok_filename = task_result_ptr_->ok_filename.string();
  task.unknown_filename = task_result_ptr_->unknown_filename.string();

  replace_special_chars(task.not_ok_filename);
  replace_special_chars(task.ok_filename);
  replace_special_chars(task.unknown_filename);
  replace_special_chars(task.input_filename);

  task.website_address = website_address(type_);

  std::ofstream out_file(filename);
  if (!out_file) {
    spdlog::error("Unable to save file to hard disk");
    return false;
  }
  out_file << number_stream_->dump();
  out_file.close();
  if (input_file.is_open()) {
    input_file.close();
  }
  std::error_code ec{};
  std::filesystem::remove(input_filename, ec);
  if (ec) {
    spdlog::error("Unable to remove file because: {}", ec.message());
  }
  return db_connector->save_stopped_task(stopped_task);
}

bool background_worker_t::open_output_files() {

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
    task_result_ptr_->unknown_filename = abs_path / "unknown" / suffix;
  }

  if (!(create_file_directory(task_result_ptr_->not_ok_filename) &&
        create_file_directory(task_result_ptr_->ok_filename) &&
        create_file_directory(task_result_ptr_->unknown_filename))) {
    // create some error messages and fire out
    return false;
  }
  task_result_ptr_->not_ok_file.open(task_result_ptr_->not_ok_filename,
                                     std::ios::out | std::ios::app);
  task_result_ptr_->ok_file.open(task_result_ptr_->ok_filename,
                                 std::ios::out | std::ios::app);
  task_result_ptr_->unknown_file.open(task_result_ptr_->unknown_filename,
                                      std::ios::out | std::ios::app);
  return task_result_ptr_->not_ok_file.is_open() &&
         task_result_ptr_->ok_file.is_open() &&
         task_result_ptr_->unknown_file.is_open();
}

void background_worker_t::run_number_crawler() {
  auto callback = std::bind(&background_worker_t::on_data_result_obtained, this,
                            std::placeholders::_1, std::placeholders::_2);
  using utilities::task_status_e;
  if (!open_output_files()) {
    spdlog::error("OpenOutputFiles failed");
    task_result_ptr_->operation_status = task_status_e::Erred;
    if (std::filesystem::exists(input_filename)) {
      if (input_file.is_open())
        input_file.close();
      std::filesystem::remove(input_filename);
    }
    return;
  }
  task_result_ptr_->operation_status = task_status_e::Ongoing;
  bool &stopped = task_result_ptr_->stopped();
  stopped = false;
  if (type_ == website_type::Unknown) {
    if (website_info_.alias.find("jjgames") != std::string::npos ||
        website_info_.address.find("jjgames") != std::string::npos) {
      type_ = website_type::JJGames;
    } else if (website_info_.alias.find("autohome") != std::string::npos ||
               website_info_.address.find("autohome") != std::string::npos) {
      type_ = website_type::AutoHomeRegister;
    }
  }
  
  std::list<std::shared_ptr<void>> sockets{};
  if (context_.stopped())
    context_.restart();
  if (type_ == website_type::JJGames) {
    // we only need one socket of this type
    auto socket_ptr = std::make_shared<jj_games_socket_t>(
        stopped, context_, safe_proxy_, *number_stream_);
    sockets.push_back(socket_ptr); // keep a type-erased copy
    (void)socket_ptr->signal().connect(callback);
    socket_ptr->start_connect();
  } else if (type_ == website_type::AutoHomeRegister) {
    for (int i = 0; i != utilities::MaxOpenSockets; ++i) {
      auto socket_ptr = std::make_shared<auto_home_socket_t>(
          stopped, context_, safe_proxy_, *number_stream_);
      sockets.push_back(socket_ptr); // keep a type-erased copy
      (void)socket_ptr->signal().connect(callback);
      socket_ptr->start_connect();
    }
  }
  context_.run();

  using utilities::task_status_e;
  if (task_result_ptr_->stopped() &&
      task_result_ptr_->operation_status == task_status_e::Ongoing) {
    task_result_ptr_->operation_status = task_status_e::Stopped;
    task_result_ptr_->progress_signal().disconnect_all_slots();
    std::string const filename =
        "./stopped_files/{}.txt"_format(std::time(nullptr));
    if (create_file_directory(filename) &&
        save_status_to_persistence(filename)) {
      spdlog::info("saved task -> {}:{} to persistent storage",
                   task_result_ptr_->task_id, task_result_ptr_->website_id);
    } else {
      if (std::filesystem::exists(filename)) {
        std::filesystem::remove(filename);
      }
      spdlog::error("unable to save task-> {}:{} to persistent storage",
                    task_result_ptr_->task_id, task_result_ptr_->website_id);
    }
  }
  sockets.clear();
}

void background_worker_t::continue_old_task() {
  using utilities::atomic_task_t;
  using utilities::task_status_e;

  auto &task = std::get<atomic_task_t::stopped_task>(atomic_task_->task);
  if (task.website_address.find("autohome") != std::string::npos) {
    type_ = website_type::AutoHomeRegister;
  } else if (task.website_address.find("jjgames") != std::string::npos) {
    type_ = website_type::JJGames;
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
  input_file.open(input_filename, std::ios::in);
  if (!input_file) {
    if (std::filesystem::exists(input_filename)) {
      std::filesystem::remove(input_filename);
    }
    task_result_ptr_->operation_status = task_status_e::Erred;
    return;
  }
  number_stream_ = std::make_unique<utilities::number_stream_t>(input_file);
  run_number_crawler();
}

void background_worker_t::run_new_task() {
  using utilities::task_status_e;
  {
    input_filename = "./{}.txt"_format(std::time(nullptr));
    std::ofstream out_file{input_filename};
    if (!out_file) {
      spdlog::error("Could not open out_file");
      input_filename.clear();
      task_result_ptr_->operation_status = task_status_e::Erred;
      return;
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
    }
    input_file.open(input_filename, std::ios::in);
    if (!input_file) {
      if (std::filesystem::exists(input_filename)) {
        std::filesystem::remove(input_filename);
      }
      task_result_ptr_->operation_status = task_status_e::Erred;
      spdlog::error("Could not open input_file");
      return;
    }
    number_stream_ = std::make_unique<utilities::number_stream_t>(input_file);
  }
  run_number_crawler();
}

void background_worker_t::run() {
  if (website_info_.id != 0)
    run_new_task();
  else
    continue_old_task();
}
} // namespace wudi_server
