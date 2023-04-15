#pragma once

#include "enumerations.hpp"
#include <array>
#include <string>

namespace woody_server {
struct request_handler {
  static std::array<char const *, constants_e::MaxLenUserAgents> const
      user_agents;
};
namespace utilities {
std::string getRandomUserAgent();
}
} // namespace woody_server
