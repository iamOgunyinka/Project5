#pragma once

#include <boost/utility/string_view.hpp>

namespace woody_server {
struct upload_request_t {
  boost::string_view upload_filename;
  boost::string_view name_on_disk;
  boost::string_view uploader_id;
  boost::string_view upload_date;
  size_t total_numbers;
};

struct upload_result_t {
  int32_t upload_id{};
  int32_t total_numbers{};
  int32_t status{};
  std::string upload_date;
  std::string filename;
  std::string name_on_disk;
};

struct website_result_t {
  int32_t id{};
  std::string address{};
  std::string alias{};
};
} // namespace woody_server
