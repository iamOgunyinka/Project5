#include "json_utils.hpp"

namespace woody_server {
void to_json(json &j, upload_result_t const &item) {
  j = json{{"id", item.upload_id},
           {"status", item.status},
           {"date", item.upload_date},
           {"filename", item.filename},
           {"total", item.total_numbers}};
}

void to_json(json &j, website_result_t const &result) {
  j = json{
      {"id", result.id}, {"alias", result.alias}, {"address", result.address}};
}

void to_json(json &j, task_result_t const &item) {
  j = json{{"id", item.id},
           {"status", item.task_status},
           {"processed", item.processed},
           {"per_ip", item.scans_per_ip},
           {"ip_used", item.ip_used},
           {"web", item.website_id},
           {"numbers", item.data_ids},
           {"total", item.total},
           {"unknown", item.unknown},
           {"not_ok", item.not_ok},
           {"date", item.scheduled_date}};
}

void to_json(json &j, atomic_task_t const &task) {
  j = json{{"id", task.task_id}};
}
} // namespace woody_server
