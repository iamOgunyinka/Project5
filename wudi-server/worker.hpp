#pragma once
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <ctime>
#include <memory>
#include <mutex>

namespace wudi_server {
namespace utilities {
struct atomic_task_t;
enum class task_status_e;
} // namespace utilities

struct database_connector_t;
class background_worker_t;

void on_task_ran(utilities::task_status_e, utilities::atomic_task_t &,
                 std::shared_ptr<database_connector_t> &,
                 background_worker_t *);

std::unique_ptr<background_worker_t>
start_new_task(utilities::atomic_task_t &scheduled_task);

std::unique_ptr<background_worker_t>
continue_recent_task(utilities::atomic_task_t &scheduled_task);

std::unique_ptr<background_worker_t>
resume_unstarted_task(utilities::atomic_task_t &scheduled_task);

void run_completion_op(std::shared_ptr<database_connector_t> &,
                       background_worker_t &);

void run_stopped_op(std::shared_ptr<database_connector_t> &,
                    background_worker_t &);

void run_error_occurred_op(std::shared_ptr<database_connector_t> &,
                           background_worker_t &);
bool save_status_to_persistent_storage(
    std::string const &filename, background_worker_t &bg_worker,
    std::shared_ptr<database_connector_t> db_connector);

void background_task_executor(std::atomic_bool &stopped, std::mutex &,
                              std::shared_ptr<database_connector_t> &);
void auto_task_restarter(boost::asio::io_context &);
} // namespace wudi_server
