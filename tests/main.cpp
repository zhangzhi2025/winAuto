#include <cstring>

#include "tests/TestFramework.h"

int main(int argc, char** argv) {
    std::string filter;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) list = true;
        else if (std::strcmp(argv[i], "--filter") == 0 && i + 1 < argc) filter = argv[++i];
    }
    int ran = 0, failed = 0;
    for (const auto& c : tf::registry()) {
        if (!filter.empty() && c.name.compare(0, filter.size(), filter) != 0) continue;
        if (list) {
            std::cout << c.name << "\n";
            continue;
        }
        ++ran;
        const int before = tf::failures();
        try {
            c.fn();
        } catch (const tf::RequireFailure&) {
        } catch (const std::exception& e) {
            tf::report(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        const bool ok = tf::failures() == before;
        if (!ok) ++failed;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << c.name << "\n";
    }
    if (list) return 0;
    if (ran == 0) {
        std::cerr << "no tests matched filter '" << filter << "'\n";
        return 2;
    }
    std::cout << ran - failed << "/" << ran << " passed\n";
    return failed == 0 ? 0 : 1;
}
