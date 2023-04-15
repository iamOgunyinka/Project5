#include "random.hpp"
#include <random>

#if _MSC_VER && !__INTEL_COMPILER
#pragma warning(disable : 4996)
#endif

namespace woody_server::utilities {
std::time_t &proxyFetchInterval() {
  static std::time_t interval_between_fetches{};
  return interval_between_fetches;
}

std::size_t getRandomInteger() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(1, 100);
  return uid(gen);
}

char getRandomChar() {
  static std::random_device rd{};
  static std::mt19937 gen{rd()};
  static std::uniform_int_distribution<> uid(0, 52);
  static char const *all_alphas =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
  return all_alphas[uid(gen)];
}

std::string getRandomString(std::size_t const length) {
  std::string result{};
  result.reserve(length);
  for (std::size_t i = 0; i != length; ++i)
    result.push_back(getRandomChar());
  return result;
}
} // namespace woody_server::utilities
