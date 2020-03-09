#pragma once
#include <atomic>
#include <memory>
#include <mutex>

namespace wudi_server {
namespace utilities {
struct atomic_task_t;
}

struct database_connector_t;
class background_worker_t;

std::unique_ptr<background_worker_t>
start_new_task(utilities::atomic_task_t &scheduled_task);

std::unique_ptr<background_worker_t>
continue_recent_task(utilities::atomic_task_t &scheduled_task);

std::unique_ptr<background_worker_t>
resume_unstarted_task(utilities::atomic_task_t &scheduled_task);

void run_completion_op(std::shared_ptr<database_connector_t>&,
                       background_worker_t &);

void run_stopped_op(std::shared_ptr<database_connector_t> &,
                    background_worker_t &);

void run_error_occurred_op(std::shared_ptr<database_connector_t> &,
                           background_worker_t &);

void background_task_executor(std::atomic_bool &stopped, std::mutex &,
                              std::shared_ptr<database_connector_t> &);

bool save_status_to_persistence(
    std::string const &filename, background_worker_t &bg_worker,
    std::shared_ptr<database_connector_t> db_connector);

} // namespace wudi_server
