#include <exception>
#include <iostream>

#include "testing.h"

namespace testing {
namespace {
int g_failuresInCurrent = 0;
}

std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_failuresInCurrent;
    std::cerr << "  " << file << ":" << line << ": " << message << "\n";
}
}  // namespace testing

int main() {
    int failedTests = 0;
    for (const auto& c : testing::registry()) {
        testing::g_failuresInCurrent = 0;
        try {
            c.fn();
        } catch (const std::exception& e) {
            testing::reportFailure(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
        } catch (...) {
            testing::reportFailure(__FILE__, __LINE__, "uncaught unknown exception");
        }
        if (testing::g_failuresInCurrent > 0) {
            ++failedTests;
            std::cerr << "[FAIL] " << c.name << "\n";
        } else {
            std::cout << "[ OK ] " << c.name << "\n";
        }
    }
    std::cout << testing::registry().size() - failedTests << "/" << testing::registry().size()
              << " passed\n";
    return failedTests == 0 ? 0 : 1;
}
