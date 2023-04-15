#pragma once

#include "nlohmann/json.hpp"
#include "task_data.hpp"
#include "upload_data.hpp"

using nlohmann::json;

// the freestanding `to_json` functions are used by the JSON library and need
// to be in the same namespace as the classes

namespace woody_server {
void to_json(json &j, task_result_t const &);
void to_json(json &j, atomic_task_t const &);
void to_json(json &j, website_result_t const &);
void to_json(json &j, upload_result_t const &item);
} // namespace woody_server
