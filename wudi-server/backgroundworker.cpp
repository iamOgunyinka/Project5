#include "backgroundworker.hpp"
#include "auto_home_sock.hpp"
#include "jj_games_socket.hpp"
#include <list>

namespace wudi_server {

std::map<std::string, website_type> website_map{
    {"autohome", website_type::AutoHomeRegister},
    {"jjgames", website_type::JJGames}};

std::string const BackgroundWorker::http_proxy_filename{
    "./http_proxy_servers.txt"};

BackgroundWorker::BackgroundWorker(std::vector<WebsiteResult> &&websites,
                                   std::vector<UploadResult> &&uploads,
                                   net::io_context &context)
    : websites_info_{std::move(websites)},
      uploads_info_{std::move(uploads)}, context_{context} {
  make_proxy_providers();
}

void BackgroundWorker::make_proxy_providers() {
  using utilities::uri;
  for (auto const &website : websites_info_) {
    std::string const host_address = uri{website.address}.host();
    if (safe_proxies_.find(host_address) == safe_proxies_.cend()) {
      safe_proxies_[host_address] = std::make_shared<safe_proxy>(context_);
    }
  }
}

void BackgroundWorker::on_data_result_obtained(utilities::SearchResultType type,
                                               std::string_view number) {
  ++curr_website_number_counter_;
  ++current_count_;
  if (curr_website_number_counter_ == web_uploads_ptr_->get_total()) {
    curr_website_number_counter_ = 0;
    //return run_number_crawler(++counter_);
  }
}

void BackgroundWorker::run_number_crawler(std::size_t &index) {
  if (index >= websites_info_.size())
    return;
  using utilities::get_file_content;
  using utilities::is_valid_number;
  using utilities::threadsafe_container;

  auto callback = std::bind(&BackgroundWorker::on_data_result_obtained, this,
                            std::placeholders::_1, std::placeholders::_2);

  auto numbers = get_file_content<std::string>(
      uploads_info_[index].name_on_disk, is_valid_number);
  if (numbers.empty())
    return run_number_crawler(++index);

  web_uploads_ptr_ =
      std::make_unique<threadsafe_container<std::string>>(std::move(numbers));
  std::size_t const socket_count = // sockets to use per website
      std::max(static_cast<std::size_t>(2),
               utilities::MaxOpenSockets / websites_info_.size());

  std::list<std::shared_ptr<void>> sockets{};
  std::size_t website_counter = 0;

  for (auto const &website_info : websites_info_) {
    std::string const &address = website_info.address;
    if (auto iter = website_map.find(address); iter != website_map.cend()) {
      if (iter->second == website_type::JJGames) {
        // we only make one socket of this type
        auto socket_ptr = std::make_shared<jj_games_socket>(
            context_, *safe_proxies_[address], *web_uploads_ptr_, callback);
        sockets.push_back(socket_ptr); // keep a type-erased copy
        socket_ptr->start_connect();
      } else if (iter->second == website_type::AutoHomeRegister) {
        for (int i = 0; i != socket_count; ++i) {
          auto socket_ptr = std::make_shared<auto_home_socket>(
              context_, *safe_proxies_[address], *web_uploads_ptr_, address,
              callback);
          sockets.push_back(socket_ptr); // keep a type-erased copy
          socket_ptr->start_connect();
        }
        context_.run();
      }
    }
  }
}

void BackgroundWorker::make_mapper() {
  total_numbers_ =
      std::accumulate(uploads_info_.cbegin(), uploads_info_.cend(), 0ULL,
                      [](auto const &init, auto const &upload) {
                        return upload.total_numbers + init;
                      });
  if (websites_info_.empty() || uploads_info_.empty())
    return;
  counter_ = 0;
  run_number_crawler(counter_);
}

void BackgroundWorker::run() { make_mapper(); }

namespace utilities {
void background_task_executor(
    std::atomic_bool &stopped, std::mutex &mutex,
    std::shared_ptr<DatabaseConnector> &db_connector) {
  auto &scheduled_tasks = get_scheduled_tasks();
  while (!stopped) {
    if (scheduled_tasks.empty()) {
      std::this_thread::sleep_for(std::chrono::seconds(SleepTimeoutSec));
      continue;
    }
    ScheduledTask task = std::move(scheduled_tasks.get());
    if (task.progress >= 100)
      continue;
    mutex.lock();
    std::vector<WebsiteResult> websites =
        db_connector->get_websites(task.website_ids);
    std::vector<UploadResult> numbers =
        db_connector->get_uploads(task.number_ids);
    mutex.unlock();
    if (websites.empty() || numbers.empty()) {
      spdlog::error("");
    }
    BackgroundWorker background_worker{std::move(websites), std::move(numbers),
                                       get_network_context()};
    background_worker.run();
    // if we are here, we are done.
  }
}
} // namespace utilities
} // namespace wudi_server
