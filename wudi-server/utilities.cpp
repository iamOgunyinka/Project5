#include "utilities.hpp"
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

namespace wudi_server {
using namespace fmt::v6::literals;

namespace utilities {

bool operator<(atomic_task_result_t const &task_1,
               atomic_task_result_t const &task_2) {
  return std::tie(task_1.task_id, task_1.website_id) <
         std::tie(task_2.task_id, task_2.website_id);
}

void to_json(json &j, upload_result_t const &item) {
  j = json{{"id", item.upload_id},
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
           {"progress", item.progress},
           {"web", item.website_ids},
           {"numbers", item.data_ids},
           {"total", item.total_numbers},
           {"date", item.scheduled_date}};
}

void to_json(json &j, atomic_task_t const &task) {
  j = json{{"id", task.task_id}};
}

bool read_task_file(std::string_view filename) {
  std::filesystem::path const file_path{filename};
  std::error_code ec{};
  auto const file_size = std::filesystem::file_size(file_path, ec);
  if (file_size == 0 || ec)
    return false;
  std::ifstream in_file{file_path};
  if (!in_file)
    return false;
  std::vector<char> file_buffer(file_size);
  in_file.read(file_buffer.data(), file_size);

  auto &tasks = get_scheduled_tasks();
  try {
    json json_root =
        json::parse(std::string_view(file_buffer.data(), file_size));
    file_buffer = {};
    if (!json_root.is_array())
      return false;
    json::array_t task_list = json_root.get<json::array_t>();
    for (auto const &json_task : task_list) {
      utilities::scheduled_task_t task{};
      json::object_t task_object = json_task.get<json::object_t>();
      task.task_id =
          static_cast<int>(task_object["id"].get<json::number_integer_t>());
      task.progress = static_cast<int>(
          task_object["progress"].get<json::number_integer_t>());
      json::array_t websites = task_object["websites"].get<json::array_t>();
      for (auto const &website_id : websites) {
        task.website_ids.push_back(
            static_cast<int>(website_id.get<json::number_integer_t>()));
      }
      json::array_t numbers = task_object["numbers"].get<json::array_t>();
      for (auto const &number_id : numbers) {
        task.number_ids.push_back(
            static_cast<int>(number_id.get<json::number_integer_t>()));
      }
      task.last_processed_number = task_object["last"].get<json::string_t>();
      // tasks.push_back(std::move(task));
    }
    return true;
  } catch (std::exception const &e) {
    spdlog::error(e.what());
  }
  return false;
}

string_view_pair_list::const_iterator
find_query_key(string_view_pair_list const &query_pairs,
               boost::string_view const &key) {
  return std::find_if(
      query_pairs.cbegin(), query_pairs.cend(),
      [=](string_view_pair const &str) { return str.first == key; });
}

std::string decode_url(boost::string_view const &encoded_string) {
  std::string src{};
  for (size_t i = 0; i < encoded_string.size();) {
    char c = encoded_string[i];
    if (c != '%') {
      src.push_back(c);
      ++i;
    } else {
      char c1 = encoded_string[i + 1];
      unsigned int localui1 = 0L;
      if ('0' <= c1 && c1 <= '9') {
        localui1 = c1 - '0';
      } else if ('A' <= c1 && c1 <= 'F') {
        localui1 = c1 - 'A' + 10;
      } else if ('a' <= c1 && c1 <= 'f') {
        localui1 = c1 - 'a' + 10;
      }

      char c2 = encoded_string[i + 2];
      unsigned int localui2 = 0L;
      if ('0' <= c2 && c2 <= '9') {
        localui2 = c2 - '0';
      } else if ('A' <= c2 && c2 <= 'F') {
        localui2 = c2 - 'A' + 10;
      } else if ('a' <= c2 && c2 <= 'f') {
        localui2 = c2 - 'a' + 10;
      }

      unsigned int ui = localui1 * 16 + localui2;
      src.push_back(ui);

      i += 3;
    }
  }

  return src;
}

bool is_valid_number(std::string_view const number, std::string &buffer) {
  if (number.size() < 11 || number.size() > 14)
    return false;

  std::size_t from = 2;
  if (number[0] == '+') { // international format
    if (number.size() != 14)
      return false;
    if (number[1] != '8' && number[2] != '6' && number[3] != '1')
      return false;
    if (number[4] < '3' || number[4] > '9')
      return false;
    from = 5;
  } else if (number[0] == '1') { // local format
    if (number.size() != 11)
      return false;
    if (number[1] < '3' || number[1] > '9')
      return false;
  } else
    return false;
  for (std::size_t index = from; index < number.length(); ++index) {
    if (number[index] < '0' || number[index] > '9')
      return false;
  }
  buffer = number.substr(from - 2);
  return true;
}

std::string view_to_string(boost::string_view const &str_view) {
  std::string str{str_view.begin(), str_view.end()};
  boost::trim(str);
  return str;
}

std::string svector_to_string(std::vector<boost::string_view> const &vec) {
  if (vec.empty())
    return {};
  std::string str{};
  for (std::size_t index = 0; index < vec.size() - 1; ++index) {
    str.append(vec[index].to_string() + ", ");
  }
  str.append(vec.back().to_string());
  return str;
}

std::string_view bv2sv(boost::string_view view) {
  return std::string_view(view.data(), view.size());
}

std::string intlist_to_string(std::vector<atomic_task_t> const &vec) {
  std::ostringstream ss{};
  if (vec.empty())
    return {};
  for (std::size_t i = 0; i != vec.size() - 1; ++i) {
    ss << vec[i].task_id << ", ";
  }
  ss << vec.back().task_id;
  return ss.str();
}

std::string intlist_to_string(std::vector<uint32_t> const &vec) {
  std::ostringstream ss{};
  if (vec.empty())
    return {};
  for (std::size_t i = 0; i != vec.size() - 1; ++i) {
    ss << vec[i] << ", ";
  }
  ss << vec.back();
  return ss.str();
}

std::vector<boost::string_view> split_string_view(boost::string_view const &str,
                                                  char const *delim) {
  std::size_t const delim_length = std::strlen(delim);
  std::size_t from_pos{};
  std::size_t index{str.find(delim, from_pos)};
  if (index == std::string::npos)
    return {str};
  std::vector<boost::string_view> result{};
  while (index != std::string::npos) {
    result.emplace_back(str.data() + from_pos, index - from_pos);
    from_pos = index + delim_length;
    index = str.find(delim, from_pos);
  }
  if (from_pos < str.length())
    result.emplace_back(str.data() + from_pos, str.size() - from_pos);
  return result;
}

std::array<char const *, LenUserAgents> const request_handler::user_agents = {
    "Mozilla/5.0 (Windows NT 6.1) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/41.0.2228.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like "
    "Gecko) Chrome/41.0.2227.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 6.3; WOW64) AppleWebKit/537.36 (KHTML, like "
    "Gecko) Chrome/41.0.2226.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:40.0) Gecko/20100101 Firefox/40.1",
    "Mozilla/5.0 (Windows NT 6.3; rv:36.0) Gecko/20100101 Firefox/36.0",
    "Mozilla/5.0 (X11; Linux i586; rv:31.0) Gecko/20100101 Firefox/31.0",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:31.0) Gecko/20130401 Firefox/31.0",
    "Mozilla/5.0 (Windows NT 5.1; rv:31.0) Gecko/20100101 Firefox/31.0",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; Trident/7.0; AS; rv:11.0) like Gecko",
    "Mozilla/5.0 (Windows; U; MSIE 9.0; WIndows NT 9.0; en-US))",
    "Mozilla/5.0 (Windows; U; MSIE 9.0; Windows NT 9.0; en-US)",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:68.0) Gecko/20100101 "
    "Firefox/68.0",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:67.0) Gecko/20100101 Firefox/67.0",
    "Mozilla/5.0 (X11; Linux i686; rv:67.0) Gecko/20100101 Firefox/67.0",
    "Mozilla/5.0 (X11; Linux x86_64; rv:67.0) Gecko/20100101 Firefox/67.0",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/74.0.3729.28 Safari/537.36 OPR/61.0.3298.6 (Edition developer)",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like "
    "Gecko) Chrome/64.0.3282.140 Safari/537.36 Edge/17.17134",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like "
    "Gecko) Chrome/74.0.3729.134 Safari/537.36 Vivaldi/2.5.1525.40"};

uri::uri(const std::string &url_s) { parse(url_s); }

std::string uri::path() const { return path_; }

std::string uri::host() const { return host_; }

void uri::parse(const std::string &url_s) {
  std::string const prot_end{"://"};
  std::string::const_iterator prot_i =
      std::search(url_s.begin(), url_s.end(), prot_end.begin(), prot_end.end());
  protocol_.reserve(
      static_cast<std::size_t>(std::distance(url_s.cbegin(), prot_i)));
  std::transform(url_s.begin(), prot_i, std::back_inserter(protocol_),
                 [](int c) { return std::tolower(c); });
  if (prot_i == url_s.end()) {
    prot_i = url_s.begin();
  } else {
    std::advance(prot_i, prot_end.length());
  }
  std::string::const_iterator path_i = std::find(prot_i, url_s.end(), '/');
  host_.reserve(static_cast<std::size_t>(std::distance(prot_i, path_i)));
  std::transform(prot_i, path_i, std::back_inserter(host_),
                 [](int c) { return std::tolower(c); });
  std::string::const_iterator query_i = std::find(path_i, url_s.end(), '?');
  path_.assign(path_i, query_i);
  if (query_i != url_s.end())
    ++query_i;
  query_.assign(query_i, url_s.end());
}

std::string get_random_agent() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(0, 17);
  return request_handler::user_agents[uid(gen)];
}

threadsafe_cv_container<atomic_task_t> &get_scheduled_tasks() {
  static threadsafe_cv_container<atomic_task_t> tasks{};
  return tasks;
}

std::multimap<uint32_t, std::shared_ptr<atomic_task_result_t>> &
get_response_queue() {
  static std::multimap<uint32_t, std::shared_ptr<atomic_task_result_t>>
      task_result;
  return task_result;
}

sharedtask_ptr<uint32_t> &get_task_counter() {
  static sharedtask_ptr<uint32_t> task_counter{};
  return task_counter;
}

std::size_t timet_to_string(std::string &output, std::size_t t,
                            char const *format) {
  std::time_t current_time = t;
#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif
  auto tm_t = std::localtime(&current_time);

  if (!tm_t)
    return std::string::npos;
  output.clear();
  output.resize(32);
  return std::strftime(output.data(), output.size(), format, tm_t);
}

number_stream_t::number_stream_t(std::ifstream &file_stream)
    : input_stream{file_stream} {}

std::string number_stream_t::get() noexcept(false) {
  std::string number{};
  while (std::getline(input_stream, number)) {
    boost::trim(number);
    if (number.empty() || !is_valid_number(number, number))
      continue;
    return number;
  }
  throw empty_container_exception_t{};
}

bool number_stream_t::empty() { return !(input_stream && !input_stream.eof()); }

decltype(std::declval<std::ifstream>().rdbuf()) number_stream_t::dump() {
  return input_stream.rdbuf();
}

boost::signals2::signal<void(uint32_t, uint32_t, task_status_e)> &
atomic_task_result_t::progress_signal() {
  return progress_signal_;
}

bool &atomic_task_result_t::stopped() { return stopped_; }

void atomic_task_result_t::stop() { stopped_ = true; }

} // namespace utilities

rule_t::rule_t(std::initializer_list<http::verb> &&verbs, callback_t callback)
    : num_verbs_{verbs.size()}, route_callback_{std::move(callback)} {
  if (verbs.size() > 5)
    throw std::runtime_error{"maximum number of verbs is 5"};
  for (int i = 0; i != verbs.size(); ++i) {
    verbs_[i] = *(verbs.begin() + i);
  }
}

void endpoint_t::add_endpoint(std::string const &route,
                              std::initializer_list<http::verb> verbs,
                              callback_t &&callback) {
  if (route.empty() || route[0] != '/')
    throw std::runtime_error{"A valid route starts with a /"};
  endpoints.emplace(route, rule_t{std::move(verbs), std::move(callback)});
}

std::optional<endpoint_t::iterator>
endpoint_t::get_rules(std::string const &target) {
  auto iter = endpoints.find(target);
  if (iter == endpoints.end())
    return std::nullopt;
  return iter;
}

std::optional<endpoint_t::iterator>
endpoint_t::get_rules(boost::string_view const &target) {
  return get_rules(std::string(target.data(), target.size()));
}

} // namespace wudi_server
