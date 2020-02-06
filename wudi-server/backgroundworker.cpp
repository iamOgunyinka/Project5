#include "backgroundworker.hpp"
#include "auto_home_sock.hpp"
#include "jj_games_socket.hpp"
#include <filesystem>
#include <list>

namespace wudi_server {

std::map<std::string, website_type> website_map{
    {"autohome", website_type::AutoHomeRegister},
    {"jjgames", website_type::JJGames}};

std::string const BackgroundWorker::http_proxy_filename{
    "./http_proxy_servers.txt"};

BackgroundWorker::BackgroundWorker(
    WebsiteResult &&website, std::vector<UploadResult> &&uploads,
    std::shared_ptr<utilities::AtomicTaskResult> task_result,
    net::io_context &context)
    : website_info_{std::move(website)}, uploads_info_{std::move(uploads)},

      context_{context}, safe_proxy_{context}, task_result_ptr_{task_result} {}

void BackgroundWorker::on_data_result_obtained(utilities::SearchResultType type,
                                               std::string_view number) {
  using utilities::SearchResultType;
  ++task_result_ptr_->processed;
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
  if (task_result_ptr_->not_ok_file.is_open() &&
      task_result_ptr_->ok_file.is_open() &&
      task_result_ptr_->unknown_file.is_open())
    return false;

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
  task_result_ptr_->not_ok_filename = abs_path / "not_ok" / suffix;
  task_result_ptr_->ok_filename = abs_path / "ok" / suffix;
  task_result_ptr_->unknown_filename = abs_path / "unknown" / suffix;

  std::cout << task_result_ptr_->not_ok_filename << std::endl;
  std::cout << task_result_ptr_->ok_filename << std::endl;
  std::cout << task_result_ptr_->unknown_filename << std::endl;

  if (!(create_file_directory(task_result_ptr_->not_ok_filename) &&
        create_file_directory(task_result_ptr_->ok_filename) &&
        create_file_directory(task_result_ptr_->unknown_filename))) {
    // create some error messages and fire out
    return false;
  }
  task_result_ptr_->not_ok_file.open(task_result_ptr_->not_ok_filename,
                                     std::ios::out | std::ios::trunc);
  task_result_ptr_->ok_file.open(task_result_ptr_->ok_filename,
                                 std::ios::out | std::ios::trunc);
  task_result_ptr_->unknown_file.open(task_result_ptr_->unknown_filename,
                                      std::ios::out | std::ios::trunc);
  return task_result_ptr_->not_ok_file.is_open() &&
         task_result_ptr_->ok_file.is_open() &&
         task_result_ptr_->unknown_file.is_open();
}

void BackgroundWorker::run_number_crawler() {
  auto callback = std::bind(&BackgroundWorker::on_data_result_obtained, this,
                            std::placeholders::_1, std::placeholders::_2);

  std::list<std::shared_ptr<void>> sockets{};
  if (!open_output_files())
    return;
  if (auto iter = website_map.find("autohome"); iter != website_map.cend()) {
    if (iter->second == website_type::JJGames) {
      // we only make one socket of this type
      auto socket_ptr = std::make_shared<jj_games_socket>(context_, safe_proxy_,
                                                          *number_stream_);
      sockets.push_back(socket_ptr); // keep a type-erased copy
      (void)socket_ptr->signal().connect(callback);
      socket_ptr->start_connect();
    } else if (iter->second == website_type::AutoHomeRegister) {
      for (int i = 0; i != utilities::MaxOpenSockets; ++i) {
        auto socket_ptr = std::make_shared<auto_home_socket>(
            context_, safe_proxy_, *number_stream_, website_info_.address);
        sockets.push_back(socket_ptr); // keep a type-erased copy
        (void)socket_ptr->signal().connect(callback);
        socket_ptr->start_connect();
      }
    }
    context_.run();
  }
}

void BackgroundWorker::make_mapper() {
  //(void)process_done.connect([this] { return run_number_crawler(); });
  {
    input_filename = "./{}.txt"_format(std::time(nullptr));
    std::ofstream out_file{input_filename};
    if (!out_file) {
      input_filename = "";
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
    if (!input_file)
      return;
    number_stream_ = std::make_unique<utilities::number_stream>(input_file);
  }
  run_number_crawler();
}

void BackgroundWorker::run() { make_mapper(); }

namespace utilities {
void background_task_executor(
    std::atomic_bool &stopped, std::mutex &mutex,
    std::shared_ptr<DatabaseConnector> &db_connector) {
  auto &scheduled_tasks = get_scheduled_tasks();
  while (!stopped) {
    auto task = std::move(scheduled_tasks.get());
    std::optional<WebsiteResult> website =
        db_connector->get_website(task.website_id);
    std::vector<UploadResult> numbers =
        db_connector->get_uploads(task.number_ids);
    if (!website || numbers.empty()) {
      spdlog::error("No such website or numbers is empty");
      continue;
    }
    auto &task_results = get_tasks_results();
    auto task_result = std::make_shared<AtomicTaskResult>();
    task_result->task_id = task.task_id;
    task_result->website_id = task.website_id;
    task_results.emplace(task.task_id, task_result);
    BackgroundWorker background_worker{std::move(*website), std::move(numbers),
                                       task_result, get_network_context()};
    background_worker.run();
    // if we are here, we are done.
  }
}
} // namespace utilities
} // namespace wudi_server
