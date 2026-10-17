#include "core/Logger.h"
#include "testing.h"

using namespace wa;

TEST(scrub_replaces_registered_secrets) {
    ExecutionLogger log;
    log.addSecret("hunter2");
    CHECK_EQ(log.scrub("password is hunter2, again hunter2"), "password is ***, again ***");
    CHECK_EQ(log.scrub("nothing here"), "nothing here");
}

TEST(longer_secrets_are_replaced_first) {
    ExecutionLogger log;
    log.addSecret("abc");
    log.addSecret("abcdef");
    CHECK_EQ(log.scrub("x abcdef y"), "x *** y");  // 不会留下 "def"
}

TEST(empty_secret_is_ignored_and_duplicates_are_harmless) {
    ExecutionLogger log;
    log.addSecret("");
    log.addSecret("s3cret");
    log.addSecret("s3cret");
    CHECK_EQ(log.scrub("a s3cret b"), "a *** b");
    CHECK_EQ(log.scrub("plain"), "plain");
}

TEST(log_entries_are_scrubbed_and_sent_to_the_sink) {
    ExecutionLogger log;
    int sunk = 0;
    std::string lastSunk;
    log.setSink([&](const LogEntry& e) {
        ++sunk;
        lastSunk = e.message;
    });
    log.addSecret("tok-123");
    log.log(LogLevel::Info, 5, "S1", "sending tok-123 now");
    CHECK_EQ(log.entries().size(), size_t(1));
    CHECK_EQ(log.entries()[0].message, "sending *** now");
    CHECK_EQ(log.entries()[0].tMs, 5);
    CHECK_EQ(sunk, 1);
    CHECK_EQ(lastSunk, "sending *** now");  // 接收端也拿不到明文
}

TEST(secrets_added_later_only_affect_later_messages) {
    ExecutionLogger log;
    log.log(LogLevel::Info, 0, "", "before");
    log.addSecret("before");
    log.log(LogLevel::Info, 1, "", "before again");
    CHECK_EQ(log.entries()[0].message, "before");
    CHECK_EQ(log.entries()[1].message, "*** again");
}

TEST(level_names) {
    CHECK_EQ(std::string(toString(LogLevel::Warn)), "WARN");
    CHECK_EQ(std::string(toString(LogLevel::Error)), "ERROR");
}
