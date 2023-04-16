#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace woody_server::utilities {
void trimString(std::string &);

template <typename T> using filter_t = bool (*)(std::string_view const, T &);

template <typename T, typename Func>
void getFileContent(std::string const &filename, filter_t<T> filter,
                    Func post_op) {
  std::ifstream in_file{filename};
  if (!in_file)
    return;
  std::string line{};
  T output{};
  while (std::getline(in_file, line)) {
    trimString(line);
    if (line.empty())
      continue;
    if (filter(line, output))
      post_op(output);
  }
}

bool createFileDirectory(std::filesystem::path const &path);
void normalizePaths(std::string &str);
void replaceSpecialChars(std::string &str);
void removeFile(std::string &filename);
} // namespace woody_server::utilities
