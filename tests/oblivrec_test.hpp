// Minimal test harness. No framework, because RULES.md A7 forbids new
// dependencies without asking and this is 40 lines.
#ifndef OBLIVREC_TEST_HPP
#define OBLIVREC_TEST_HPP
#include <cstdio>
#include <cstdlib>
#include <string>

namespace oblivrec_test {
inline int& Failures() { static int f = 0; return f; }

inline void Check(bool ok, const char* expr, const char* file, int line,
                  const std::string& detail = "") {
  if (!ok) {
    ++Failures();
    std::printf("  FAIL %s:%d  %s\n", file, line, expr);
    if (!detail.empty()) std::printf("       %s\n", detail.c_str());
  }
}
inline int Report(const char* name) {
  if (Failures() == 0) { std::printf("  ok: %s\n", name); return 0; }
  std::printf("  %d failure(s) in %s\n", Failures(), name);
  return 1;
}
}  // namespace oblivrec_test

#define CHECK(cond) \
  ::oblivrec_test::Check((cond), #cond, __FILE__, __LINE__)
#define CHECK_MSG(cond, detail) \
  ::oblivrec_test::Check((cond), #cond, __FILE__, __LINE__, (detail))
#endif
