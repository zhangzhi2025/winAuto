#include <set>
#include <string>

#include "core/ErrCode.h"
#include "testing.h"

using namespace wa;

TEST(every_error_code_has_name_and_text) {
    const ErrCode all[] = {ErrCode::Ok,           ErrCode::InvalidWorkflow,  ErrCode::ConfirmationRequired,
                           ErrCode::WindowNotFound, ErrCode::WindowElevated, ErrCode::ElementNotFound,
                           ErrCode::ElementAmbiguous, ErrCode::ExpectationFailed, ErrCode::Timeout,
                           ErrCode::Cancelled,    ErrCode::InputBlocked,     ErrCode::Unsupported,
                           ErrCode::LimitExceeded, ErrCode::Internal};
    std::set<std::string> names;
    for (ErrCode c : all) {
        CHECK(std::string(errName(c)) != "Unknown");
        CHECK(std::string(errText(c)) != "未知错误");
        CHECK(std::string(errText(c)).size() > 0);
        names.insert(errName(c));
    }
    CHECK_EQ(names.size(), sizeof(all) / sizeof(all[0]));  // 名称互不重复
    CHECK_EQ(std::string(errName(ErrCode::ElementNotFound)), "ElementNotFound");
}

TEST(result_success_and_failure_branches) {
    auto ok = Result<int>::success(42);
    CHECK(ok.ok());
    CHECK(static_cast<bool>(ok));
    CHECK_EQ(ok.value, 42);

    auto bad = Result<int>::failure(ErrCode::Timeout, "slow");
    CHECK(!bad.ok());
    CHECK(bad.code == ErrCode::Timeout);
    CHECK_EQ(bad.detail, "slow");

    Status s = okStatus();
    CHECK(s.ok());
}

TEST(failAs_keeps_code_and_detail) {
    auto bad = Result<int>::failure(ErrCode::ElementAmbiguous, "two matches");
    Result<std::string> other = failAs<std::string>(bad);
    CHECK(other.code == ErrCode::ElementAmbiguous);
    CHECK_EQ(other.detail, "two matches");
}
