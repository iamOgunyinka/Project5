#pragma once

#include <boost/utility/string_view.hpp>

namespace woody_server {
class http_uri_t {
public:
  explicit http_uri_t(std::string const &url_s);
  [[nodiscard]] inline std::string path() const { return m_path; }
  [[nodiscard]] inline std::string host() const { return m_host; }
  [[nodiscard]] inline std::string target() const { return m_path; }
  [[nodiscard]] inline std::string protocol() const { return m_protocol; }

private:
  void parse(std::string const &);

  std::string m_host;
  std::string m_path;
  std::string m_protocol;
  std::string m_query;
};

namespace utilities {
std::string decodeUrl(boost::string_view const &encoded_string);
}
} // namespace woody_server
