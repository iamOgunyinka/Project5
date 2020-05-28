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

std::string &hex_to_char(std::string &s, std::vector<char> const &data) {
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

std::string md5(std::string const &input_data) {
  std::vector<char> v_md5;
  v_md5.resize(16);

  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, input_data.c_str(), input_data.size());
  MD5_Final((unsigned char *)&v_md5[0], &ctx);

  std::string s_md5;
  hex_to_char(s_md5, v_md5);
  return s_md5;
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

std::string_view bv2sv(boost::string_view view) {
  return std::string_view(view.data(), view.size());
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

std::array<char const *, 14> const request_handler::user_agents = {
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

std::string get_random_agent() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(0, 13);
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

} // namespace utilities
} // namespace wudi_server
