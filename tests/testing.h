#pragma once
// 极简测试框架（头文件 + testing_main.cpp），不依赖 QtTest，便于 core 在任何环境测试。
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    std::function<void()> fn;
};
std::vector<Case>& registry();
void reportFailure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

template <class T>
std::string show(const T& v) {
    if constexpr (std::is_enum_v<T>) {
        return std::to_string(static_cast<long long>(v));
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else {
        std::ostringstream os;
        os << v;
        return os.str();
    }
}

}  // namespace testing

#define TEST(name)                                       \
    static void test_##name();                           \
    static testing::Registrar reg_##name(#name, test_##name); \
    static void test_##name()

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) testing::reportFailure(__FILE__, __LINE__, "CHECK failed: " #cond); \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto va_ = (a);                                                          \
        const auto vb_ = (b);                                                          \
        if (!(va_ == vb_)) {                                                            \
            testing::reportFailure(__FILE__, __LINE__,                                  \
                                   std::string("CHECK_EQ failed: " #a " == " #b "\n    left:  ") + \
                                       testing::show(va_) + "\n    right: " + testing::show(vb_)); \
        }                                                                               \
    } while (0)

#define CHECK_CONTAINS(haystack, needle)                                                  \
    do {                                                                                  \
        const std::string h_ = (haystack);                                                \
        const std::string n_ = (needle);                                                  \
        if (h_.find(n_) == std::string::npos) {                                           \
            testing::reportFailure(__FILE__, __LINE__,                                    \
                                   "CHECK_CONTAINS failed: \"" + h_ + "\" lacks \"" + n_ + "\""); \
        }                                                                                 \
    } while (0)

#define CHECK_NOT_CONTAINS(haystack, needle)                                              \
    do {                                                                                  \
        const std::string h_ = (haystack);                                                \
        const std::string n_ = (needle);                                                  \
        if (h_.find(n_) != std::string::npos) {                                           \
            testing::reportFailure(__FILE__, __LINE__,                                    \
                                   "CHECK_NOT_CONTAINS failed: \"" + h_ + "\" contains \"" + n_ + "\""); \
        }                                                                                 \
    } while (0)
