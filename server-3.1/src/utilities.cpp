#include "utilities.hpp"
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <fstream>
#include <openssl/md5.h>
#include <random>

#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif

namespace wudi_server {
namespace utilities {

bool operator<(internal_task_result_t const &task_1,
               internal_task_result_t const &task_2) {
  return std::tie(task_1.task_id, task_1.website_id) <
         std::tie(task_2.task_id, task_2.website_id);
}

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

std::string &HexToChar(std::string &s, std::vector<char> const &data) {
  s = "";
  for (unsigned int i = 0; i < data.size(); ++i) {
    char szBuff[3] = "";
    sprintf(szBuff, "%02x",
            *reinterpret_cast<const unsigned char *>(&data[i]) & 0xff);
    s += szBuff[0];
    s += szBuff[1];
  }
  return s;
}

std::string md5(std::string const &sInputData) {
  std::vector<char> vMd5;
  vMd5.resize(16);

  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, sInputData.c_str(), sInputData.size());
  MD5_Final((unsigned char *)&vMd5[0], &ctx);

  std::string sMd5;
  HexToChar(sMd5, vMd5);
  return sMd5;
}

bool create_file_directory(std::filesystem::path const &path) {
  std::error_code ec{};
  auto f = std::filesystem::absolute(path.parent_path(), ec);
  if (ec)
    return false;
  ec = {};
  std::filesystem::create_directories(f, ec);
  return !ec;
}

std::time_t &proxy_fetch_interval() {
  static std::time_t interval_between_fetches{};
  return interval_between_fetches;
}

time_data_t get_time_data() {
  static std::random_device rd{};
  static std::mt19937 gen(rd());
  static std::uniform_real_distribution<> dis(0.0, 1.0);
  uint64_t const current_time = std::time(nullptr) * 1'000;
  std::size_t const random_number =
      static_cast<std::size_t>(std::round(1e3 * dis(gen)));
  std::uint64_t const callback_number =
      static_cast<std::size_t>(current_time + random_number);
  return time_data_t{current_time, callback_number};
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

void normalize_paths(std::string &str) {
  for (std::string::size_type i = 0; i != str.size(); ++i) {
    if (str[i] == '#') {
#ifdef _WIN32
      str[i] = '\\';
#else
      str[i] = '/';
#endif // _WIN32
    }
  }
};

void replace_special_chars(std::string &str) {
  for (std::string::size_type i = 0; i != str.size(); ++i) {
#ifdef _WIN32
    if (str[i] == '\\')
#else
    if (str[i] == '/')
#endif
      str[i] = '#';
  }
}

void remove_file(std::string &filename) {
  std::error_code ec{};
  normalize_paths(filename);
  if (std::filesystem::exists(filename))
    std::filesystem::remove(filename, ec);
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
    "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:40.0) Gecko/20100101 Firefox/40.1",
    "Mozilla/5.0 (Windows NT 6.3; rv:36.0) Gecko/20100101 Firefox/36.0",
    "Mozilla/5.0 (X11; Linux i586; rv:31.0) Gecko/20100101 Firefox/31.0",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:31.0) Gecko/20130401 Firefox/31.0",
    "Mozilla/5.0 (Windows NT 5.1; rv:31.0) Gecko/20100101 Firefox/31.0",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; Trident/7.0; AS; rv:11.0) like Gecko",
    "Mozilla/5.0 (Windows; U; MSIE 9.0; Windows NT 9.0; en-US)",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:68.0) Gecko/20100101 "
    "Firefox/68.0",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:67.0) Gecko/20100101 Firefox/67.0",
    "Mozilla/5.0 (X11; Linux i686; rv:67.0) Gecko/20100101 Firefox/67.0",
    "Mozilla/5.0 (X11; Linux x86_64; rv:67.0) Gecko/20100101 Firefox/67.0",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/74.0.3729.28 Safari/537.36 OPR/61.0.3298.6 (Edition developer)",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like "
    "Gecko) Chrome/64.0.3282.140 Safari/537.36 Edge/17.17134"};

uri::uri(const std::string &url_s) { parse(url_s); }

std::string uri::target() const { return path_ + "?" + query_; }

std::string uri::protocol() const { return protocol_; }

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
  static std::uniform_int_distribution<> uid(0, LenUserAgents - 1);
  return request_handler::user_agents[uid(gen)];
}

std::size_t get_random_integer() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(1, 100);
  return uid(gen);
}

char get_random_char() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(0, 52);
  static char const *all_alphas =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
  return all_alphas[uid(gen)];
}

std::string get_random_string(std::size_t const length) {
  std::string result{};
  result.reserve(length);
  for (std::size_t i = 0; i != length; ++i) {
    result.push_back(get_random_char());
  }
  return result;
}

threadsafe_cv_container<atomic_task_t> &get_scheduled_tasks() {
  static threadsafe_cv_container<atomic_task_t> tasks{};
  return tasks;
}

std::map<uint32_t, std::shared_ptr<internal_task_result_t>> &
get_response_queue() {
  static std::map<uint32_t, std::shared_ptr<internal_task_result_t>>
      task_result;
  return task_result;
}

std::size_t timet_to_string(std::string &output, std::size_t t,
                            char const *format) {
  std::time_t current_time = t;
#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif
  auto const tm_t = std::localtime(&current_time);

  if (!tm_t)
    return std::string::npos;
  output.clear();
  output.resize(32);
  return std::strftime(output.data(), output.size(), format, tm_t);
}

number_stream_t::number_stream_t(std::ifstream &file_stream)
    : input_stream{file_stream}, mutex_{} {}

std::string number_stream_t::get() noexcept(false) {
  if (closed_)
    throw empty_container_exception_t{};
  std::string number, temp{};
  std::lock_guard<std::mutex> lock_g{mutex_};
  if (!temporaries_.empty()) {
    std::string temp = temporaries_.front();
    temporaries_.erase(temporaries_.begin());
    return temp;
  }
  while (std::getline(input_stream, temp)) {
    boost::trim(temp);
    if (temp.empty() || !is_valid_number(temp, number))
      continue;
    return number;
  }
  throw empty_container_exception_t{};
}

void number_stream_t::close() {
  input_stream.close();
  closed_ = true;
}

bool number_stream_t::is_open() { return input_stream.is_open(); }

bool number_stream_t::empty() {
  return closed_ || !input_stream || input_stream.eof();
}

decltype(std::declval<std::ifstream>().rdbuf()) number_stream_t::dump_s() {
  return input_stream.rdbuf();
}

std::vector<std::string> &number_stream_t::dump() { return temporaries_; }

void number_stream_t::push_back(std::string const &str) {
  temporaries_.push_back(str);
}

bool &internal_task_result_t::stopped() { return stopped_; }
bool &internal_task_result_t::saving_state() { return save_state_; }

void internal_task_result_t::stop() {
  stopped_ = true;
  operation_status = task_status_e::Stopped;
}

} // namespace utilities
} // namespace wudi_server
