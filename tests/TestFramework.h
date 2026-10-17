// 极小的测试框架（无外部依赖，MSVC / GCC / Clang 通用）。
//   TEST(Suite, name) { CHECK(cond); CHECK_EQ(a, b); REQUIRE(cond); }
// 运行：winauto_tests [--list] [--filter Suite.]   （前缀匹配）
#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct RequireFailure : std::exception {};

inline int& failures() {
    static int n = 0;
    return n;
}

inline void report(const char* file, int line, const std::string& msg) {
    ++failures();
    std::cerr << "    FAIL " << file << ":" << line << ": " << msg << "\n";
}

template <class A, class B>
std::string describeEq(const char* ea, const char* eb, const A& a, const B& b) {
    std::ostringstream os;
    os << ea << " == " << eb << "  (left: " << a << ", right: " << b << ")";
    return os.str();
}

}  // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)

#define TEST(suite, name)                                                                   \
    static void TF_CAT(tf_fn_, __LINE__)();                                                 \
    static ::tf::Registrar TF_CAT(tf_reg_, __LINE__)(#suite "." #name, &TF_CAT(tf_fn_, __LINE__)); \
    static void TF_CAT(tf_fn_, __LINE__)()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) ::tf::report(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                      \
    do {                                                                                    \
        const auto& tf_a = (a);                                                             \
        const auto& tf_b = (b);                                                             \
        if (!(tf_a == tf_b)) ::tf::report(__FILE__, __LINE__, ::tf::describeEq(#a, #b, tf_a, tf_b)); \
    } while (0)

#define REQUIRE(cond)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::tf::report(__FILE__, __LINE__, "REQUIRE(" #cond ")");            \
            throw ::tf::RequireFailure();                                      \
        }                                                                      \
    } while (0)
