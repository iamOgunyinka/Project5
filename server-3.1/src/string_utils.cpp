#include "string_utils.hpp"

#include <ctime>
#include <openssl/md5.h>
#include <sstream>

namespace woody_server::utilities {
void ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
          }));
}

void rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(),
                       [](unsigned char ch) { return !std::isspace(ch); })
              .base(),
          s.end());
}

void trim(std::string &s) {
  ltrim(s);
  rtrim(s);
}

std::string ltrimCopy(std::string s) {
  ltrim(s);
  return s;
}

std::string rtrimCopy(std::string s) {
  rtrim(s);
  return s;
}

std::string trimCopy(std::string const &s) {
  std::string temp = s;
  trim(temp);
  return s;
}

void trimString(std::string &str) { trim(str); }
bool unixTimeToString(std::string &output, std::size_t const t,
                      char const *format) {
  auto currentTime = static_cast<std::time_t>(t);
#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif
  auto const tmT = std::localtime(&currentTime);

  if (!tmT)
    return std::string::npos;
  output.clear();
  output.resize(32);
  return std::strftime(output.data(), output.size(), format, tmT) != 0;
}

std::string stringViewToString(boost::string_view const &str_view) {
  std::string str{str_view.begin(), str_view.end()};
  trimString(str);
  return str;
}

std::string stringListToString(std::vector<boost::string_view> const &vec) {
  if (vec.empty())
    return {};
  std::string str{};
  for (std::size_t index = 0; index < vec.size() - 1; ++index) {
    str.append(vec[index].to_string() + ", ");
  }
  str.append(vec.back().to_string());
  return str;
}

void hexToChar(std::string &s, std::vector<char> const &data) {
  s.clear();
  for (unsigned int i = 0; i < data.size(); ++i) {
    char szBuff[3] = "";
    sprintf(szBuff, "%02x",
            *reinterpret_cast<const unsigned char *>(&data[i]) & 0xff);
    s += szBuff[0];
    s += szBuff[1];
  }
}

std::string md5Hash(std::string const &input_data) {
  std::vector<char> vMd5;
  vMd5.resize(16);

  MD5_CTX ctx;
  MD5_Init(&ctx);
  MD5_Update(&ctx, input_data.c_str(), input_data.size());
  MD5_Final((unsigned char *)&vMd5[0], &ctx);

  std::string sMd5;
  hexToChar(sMd5, vMd5);
  return sMd5;
}

bool isValidMobileNumber(std::string_view const number, std::string &buffer) {
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

std::string_view boostViewToStdStringView(boost::string_view view) {
  return std::string_view(view.data(), view.size());
}

std::string integerListToString(std::vector<uint32_t> const &vec) {
  std::ostringstream ss{};
  if (vec.empty())
    return {};
  for (std::size_t i = 0; i != vec.size() - 1; ++i) {
    ss << vec[i] << ", ";
  }
  ss << vec.back();
  return ss.str();
}

std::vector<boost::string_view> splitStringView(boost::string_view const &str,
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

void splitStringInto(std::vector<std::string> &result, std::string const &str,
                     std::string const &delim) {
  std::size_t const delimLength = delim.length();
  std::size_t fromPos{};
  std::size_t index{str.find(delim, fromPos)};
  if (index == std::string::npos)
    return;

  while (index != std::string::npos) {
    result.emplace_back(str.data() + fromPos, index - fromPos);
    fromPos = index + delimLength;
    index = str.find(delim, fromPos);
  }

  if (fromPos < str.length())
    result.emplace_back(str.data() + fromPos, str.size() - fromPos);
}
} // namespace woody_server::utilities
