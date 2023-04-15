#include "file_utils.hpp"
#include <filesystem>

namespace woody_server::utilities {
void normalizePaths(std::string &str) {
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

void replaceSpecialChars(std::string &str) {
  for (std::string::size_type i = 0; i != str.size(); ++i) {
#ifdef _WIN32
    if (str[i] == '\\')
#else
    if (str[i] == '/')
#endif
      str[i] = '#';
  }
}

void removeFile(std::string &filename) {
  std::error_code ec{};
  normalizePaths(filename);
  if (std::filesystem::exists(filename))
    std::filesystem::remove(filename, ec);
}

bool createFileDirectory(std::filesystem::path const &path) {
  std::error_code ec{};
  auto f = std::filesystem::absolute(path.parent_path(), ec);
  if (ec)
    return false;
  ec = {};
  std::filesystem::create_directories(f, ec);
  return !ec;
}
} // namespace woody_server::utilities
