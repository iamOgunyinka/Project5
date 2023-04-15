#pragma once

#include <boost/utility/string_view.hpp>
#include <string_view>
#include <vector>

namespace woody_server::utilities {
template <typename Container, typename... IterList>
bool anyOf(Container const &container, IterList &&...iter_list) {
  return (... || (std::cend(container) == iter_list));
}

void trimString(std::string &);
std::string trimCopy(std::string const &s);
bool unixTimeToString(std::string &, std::size_t,
                      char const * = "%Y-%m-%d %H:%M:%S");
bool isValidMobileNumber(std::string_view, std::string &);
std::string md5Hash(std::string const &);
std::string stringListToString(std::vector<boost::string_view> const &vec);
std::string decodeUrl(boost::string_view const &encoded_string);
std::string stringViewToString(boost::string_view const &str_view);
std::string integerListToString(std::vector<uint32_t> const &vec);
std::string_view boostViewToStdStringView(boost::string_view);
std::vector<boost::string_view> splitStringView(boost::string_view const &str,
                                                char const *delimeter);
void splitStringInto(std::vector<std::string> &, std::string const &text,
                     std::string const &delim);
} // namespace woody_server::utilities
