#include "utilities.hpp"
#include <sstream>

#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif

namespace wudi_server {
namespace utilities {

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

std::string intlist_to_string(std::vector<int> const &vec) {
  std::ostringstream ss{};
  if (vec.empty())
    return {};
  for (std::size_t i = 0; i != vec.size() - 1; ++i) {
    ss << vec[i] << ", ";
  }
  ss << vec.back();
  return ss.str();
}

std::vector<std::string_view> split_string_view(std::string_view const &str,
                                                char const *delim) {
  std::size_t const delim_length = std::strlen(delim);
  std::size_t from_pos{};
  std::size_t index{str.find(delim, from_pos)};
  if (index == std::string::npos)
    return {str};
  std::vector<std::string_view> result{};
  while (index != std::string::npos) {
    result.emplace_back(str.data() + from_pos, index - from_pos);
    from_pos = index + delim_length;
    index = str.find(delim, from_pos);
  }
  if (from_pos < str.length())
    result.emplace_back(str.data() + from_pos, str.size() - from_pos);
  return result;
}

} // namespace utilities
} // namespace wudi_server
