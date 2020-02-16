#include "backgroundworker.hpp"
#include "auto_home_sock.hpp"
#include "jj_games_socket.hpp"
#include <filesystem>
#include <list>

namespace wudi_server {
std::string const BackgroundWorker::http_proxy_filename{
    "./http_proxy_servers.txt"};

BackgroundWorker::~BackgroundWorker() {
  task_result_ptr_->not_ok_file.close();
  task_result_ptr_->ok_file.close();
  task_result_ptr_->unknown_file.close();
}

BackgroundWorker::BackgroundWorker(
    WebsiteResult &&website, std::vector<UploadResult> &&uploads,
    std::shared_ptr<utilities::AtomicTaskResult> task_result,
    net::io_context &context)
    : website_info_{std::move(website)}, uploads_info_{std::move(uploads)},
      context_{context}, safe_proxy_{context},
      task_result_ptr_{task_result}, type_{website_type::Unknown} {}

BackgroundWorker::BackgroundWorker(
    utilities::AtomicTask old_task,
    std::shared_ptr<utilities::AtomicTaskResult> task_result,
    net::io_context &context)
    : website_info_{}, uploads_info_{}, context_{context}, safe_proxy_{context},
      task_result_ptr_{task_result}, type_{website_type::Unknown},
      atomic_task_{std::move(old_task)} {}

void BackgroundWorker::on_data_result_obtained(utilities::SearchResultType type,
                                               std::string_view number) {
  using utilities::SearchResultType;
  auto &processed = task_result_ptr_->processed;
  auto &total = task_result_ptr_->total_numbers;

  ++processed;
  switch (type) {
  case SearchResultType::NotRegistered:
    task_result_ptr_->ok_file << number << "\n";
    task_result_ptr_->ok_file.flush();
    break;
  case SearchResultType::Registered:
    task_result_ptr_->not_ok_file << number << "\n";
    task_result_ptr_->not_ok_file.flush();
    break;
  case SearchResultType::Unknown:
    task_result_ptr_->unknown_file << number << "\n";
    task_result_ptr_->unknown_file.flush();
    break;
  }

  if (processed == total) {
    task_result_ptr_->operation_status = utilities::TaskStatus::Completed;
    std::filesystem::remove(input_filename);
    spdlog::info("Done processing task -> {}:{}", task_result_ptr_->task_id,
                 task_result_ptr_->website_id);
  }

  int const progress = (processed / total) * 100;
  auto &signal = task_result_ptr_->progress_signal();
  if (progress > current_progress_) {
    current_progress_ = progress;
    signal(task_result_ptr_->task_id, processed,
           task_result_ptr_->operation_status);
  }
  using utilities::TaskStatus;
  if (task_result_ptr_->stopped() &&
      task_result_ptr_->operation_status == TaskStatus::Ongoing) {
    task_result_ptr_->operation_status = TaskStatus::Stopped;
    if (!save_status_to_persistence()) {
      spdlog::error("unable to save task-> {}:{} to persistent storage",
                    task_result_ptr_->task_id, task_result_ptr_->website_id);
    } else {
      spdlog::info("saved task -> {}:{} to persistent storage",
                   task_result_ptr_->task_id, task_result_ptr_->website_id);
    }
  }
}

bool BackgroundWorker::save_status_to_persistence() {
  auto website_address = [](website_type const type) {
    switch (type) { // we can easily add more websites in the future
    case website_type::AutoHomeRegister:
      return "autohome";
    case website_type::JJGames:
      return "jjgames";
    }
    return "";
  };
  using utilities::AtomicTask;
  auto db_connector = wudi_server::DatabaseConnector::GetDBConnector();
  AtomicTask stopped_task{};
  stopped_task.type_ = AtomicTask::task_type::stopped;
  stopped_task.task.emplace<AtomicTask::stopped_task>();
  stopped_task.processed = task_result_ptr_->processed;
  stopped_task.total = task_result_ptr_->total_numbers;

  auto &task = std::get<utilities::AtomicTask::stopped_task>(stopped_task.task);
  task.input_filename = input_filename;
  task.not_ok_filename = task_result_ptr_->not_ok_filename.string();
  task.ok_filename = task_result_ptr_->ok_filename.string();
  task.task_id = task_result_ptr_->task_id;
  task.unknown_filename = task_result_ptr_->unknown_filename.string();
  task.website_address = website_address(type_);

  return db_connector->save_stopped_task(stopped_task);
}

bool BackgroundWorker::open_output_files() {
  auto create_file_directory = [](std::filesystem::path const &path) -> bool {
    std::error_code ec{};
    auto f = std::filesystem::absolute(path.parent_path(), ec);
    if (ec)
      return false;
    ec = {};
    std::filesystem::create_directories(f, ec);
    return !ec;
  };

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
  auto const suffix{std::filesystem::path{current_date} /
                    (current_time + ".txt")};
  if (task_result_ptr_->not_ok_filename.string().empty()) {
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

void BackgroundWorker::run_number_crawler() {
  auto callback = std::bind(&BackgroundWorker::on_data_result_obtained, this,
                            std::placeholders::_1, std::placeholders::_2);
  using utilities::TaskStatus;
  if (!open_output_files()) {
    task_result_ptr_->operation_status = TaskStatus::Erred;
    if (std::filesystem::exists(input_filename)) {
      if (input_file.is_open())
        input_file.close();
      std::filesystem::remove(input_filename);
    }
    return;
  }
  task_result_ptr_->operation_status = TaskStatus::Ongoing;
  bool &stopped = task_result_ptr_->stopped();
  if (type_ == website_type::Unknown) {
    if (website_info_.alias.find("jjgames") != std::string::npos ||
        website_info_.address.find("jjgames") != std::string::npos) {
      type_ = website_type::JJGames;
    } else if (website_info_.alias.find("autohome") != std::string::npos ||
               website_info_.address.find("autohome") != std::string::npos) {
      type_ = website_type::AutoHomeRegister;
    }
  }
  if (type_ == website_type::JJGames) {
    // we only make one socket of this type
    auto socket_ptr = std::make_shared<jj_games_socket>(
        stopped, context_, safe_proxy_, *number_stream_);
    sockets_.push_back(socket_ptr); // keep a type-erased copy
    (void)socket_ptr->signal().connect(callback);
    socket_ptr->start_connect();
  } else {
    for (int i = 0; i != utilities::MaxOpenSockets; ++i) {
      auto socket_ptr = std::make_shared<auto_home_socket>(
          stopped, context_, safe_proxy_, *number_stream_);
      sockets_.push_back(socket_ptr); // keep a type-erased copy
      (void)socket_ptr->signal().connect(callback);
      socket_ptr->start_connect();
    }
  }
  context_.run();
  if (std::filesystem::exists(input_filename)) {
    if (input_file.is_open())
      input_file.close();
    std::filesystem::remove(input_filename);
  }
}

void BackgroundWorker::continue_old_task() {
  using utilities::AtomicTask;
  using utilities::TaskStatus;

  auto &task = std::get<AtomicTask::stopped_task>(atomic_task_->task);
  if (task.website_address.find("autohome") != std::string::npos) {
    type_ = website_type::AutoHomeRegister;
  } else if (task.website_address.find("jjgames") != std::string::npos) {
    type_ = website_type::JJGames;
  }
  input_filename = task.input_filename;
  if (task_result_ptr_->total_numbers == 0) {
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
    if (!input_file) {
      if (std::filesystem::exists(input_filename)) {
        std::filesystem::remove(input_filename);
      }
      task_result_ptr_->operation_status = TaskStatus::Erred;
      return;
    }
    number_stream_ = std::make_unique<utilities::number_stream>(input_file);
  }
  run_number_crawler();
}

void BackgroundWorker::run_new_task() {
  using utilities::TaskStatus;
  {
    input_filename = "./{}.txt"_format(std::time(nullptr));
    std::ofstream out_file{input_filename};
    if (!out_file) {
      input_filename.clear();
      task_result_ptr_->operation_status = TaskStatus::Erred;
      return;
    }
    for (std::size_t index = 0; index != uploads_info_.size(); ++index) {
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
      task_result_ptr_->operation_status = TaskStatus::Erred;
      return;
    }
    number_stream_ = std::make_unique<utilities::number_stream>(input_file);
  }
  run_number_crawler();
}

void BackgroundWorker::run() {
  if (website_info_.id != 0)
    run_new_task();
  else
    continue_old_task();
}

namespace utilities {
void background_task_executor(
    std::atomic_bool &stopped, std::mutex &mutex,
    std::shared_ptr<DatabaseConnector> &db_connector) {
  auto &scheduled_tasks = get_scheduled_tasks();
  while (!stopped) {
    auto scheduled_task = std::move(scheduled_tasks.get());
    auto &response_queue = get_response_queue();
    auto &task_counter = get_task_counter();
    if (scheduled_task.type_ == AtomicTask::task_type::fresh) {
      auto &task = std::get<AtomicTask::fresh_task>(scheduled_task.task);
      std::optional<WebsiteResult> website =
          db_connector->get_website(task.website_id);
      std::vector<UploadResult> numbers =
          db_connector->get_uploads(task.number_ids);
      if (!website || numbers.empty()) {
        spdlog::error("No such website or numbers is empty");
        continue;
      }

      auto task_result = std::make_shared<AtomicTaskResult>();
      task_result->task_id = scheduled_task.task_id;
      task_result->website_id = task.website_id;
      response_queue.emplace(scheduled_task.task_id, task_result);
      task_counter.insert(
          scheduled_task.task_id, [db_connector](uint32_t task_id) {
            db_connector->change_task_status(task_id, TaskStatus::Ongoing);
          });
      BackgroundWorker background_worker{std::move(*website),
                                         std::move(numbers), task_result,
                                         get_network_context()};
      background_worker.run();
      task_counter.remove(scheduled_task.task_id, [=](uint32_t const task_id) {
        db_connector->change_task_status(
            task_id, task_result->operation_status == TaskStatus::Stopped
                         ? TaskStatus::Stopped
                         : TaskStatus::Completed);
      });
    } else { // to-do
      auto &task = std::get<AtomicTask::stopped_task>(scheduled_task.task);
      if (task.website_address.empty() ||
          !std::filesystem::exists(task.input_filename)) {
        spdlog::error("No such website or numbers is empty");
        continue;
      }
      std::shared_ptr<AtomicTaskResult> task_result{};
      auto iter = response_queue.equal_range(scheduled_task.task_id);
      if (iter.first == response_queue.cend()) {
        task_result = std::make_shared<AtomicTaskResult>();
        task_result->task_id = scheduled_task.task_id;
        task_result->website_id = scheduled_task.website_id;
        response_queue.emplace(scheduled_task.task_id, task_result);
      } else {
        for (auto first = iter.first; first != iter.second; ++first) {
          if (first->second->website_id == scheduled_task.website_id) {
            task_result = first->second;
            break;
          }
        }
      }
    }

    // if we are here, we are done.
  }
}
} // namespace utilities
} // namespace wudi_server
