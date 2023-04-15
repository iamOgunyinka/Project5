#pragma once
#include <atomic>
#include <memory>
#include <string>

namespace boost::asio::ssl {
class context;
}

namespace woody_server {

enum class task_status_e;
struct atomic_task_t;
struct database_connector_t;
class background_worker_t;
class global_proxy_repo_t;

void on_task_ran(task_status_e, atomic_task_t &,
                 std::shared_ptr<database_connector_t> &,
                 background_worker_t *);

std::unique_ptr<background_worker_t>
start_new_task(atomic_task_t &scheduled_task, boost::asio::ssl::context &);

std::unique_ptr<background_worker_t>
continue_recent_task(atomic_task_t &scheduled_task,
                     boost::asio::ssl::context &);

std::unique_ptr<background_worker_t>
resume_unstarted_task(atomic_task_t &scheduled_task,
                      boost::asio::ssl::context &);

void run_completion_op(std::shared_ptr<database_connector_t> &,
                       background_worker_t &);

void run_stopped_op(std::shared_ptr<database_connector_t> &,
                    background_worker_t &);

void run_error_occurred_op(std::shared_ptr<database_connector_t> &,
                           background_worker_t &);
bool save_status_to_persistent_storage(
    std::string const &filename, background_worker_t &bg_worker,
    std::shared_ptr<database_connector_t> db_connector);

void background_task_executor(std::atomic_bool &stopped,
                              boost::asio::ssl::context &,
                              global_proxy_repo_t &);
} // namespace woody_server
