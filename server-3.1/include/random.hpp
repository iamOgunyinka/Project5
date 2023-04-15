#pragma once

#include <ctime>
#include <string>

namespace woody_server::utilities {
char getRandomChar();
std::string getRandomString(std::size_t);
std::size_t getRandomInteger();
std::time_t &proxyFetchInterval();
} // namespace woody_server::utilities
