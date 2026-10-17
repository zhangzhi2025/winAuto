#include <cstring>

#include "core/Error.h"
#include "tests/TestFramework.h"

using namespace winauto;

TEST(Error, everyCodeHasNameAndDescription) {
    const ErrCode all[] = {ErrCode::Ok,           ErrCode::WindowNotFound,   ErrCode::WindowAmbiguous,
                           ErrCode::WindowElevated, ErrCode::ActivationFailed, ErrCode::ElementNotFound,
                           ErrCode::ElementAmbiguous, ErrCode::Timeout,       ErrCode::Cancelled,
                           ErrCode::InputBlocked, ErrCode::ExpectationFailed, ErrCode::Unsupported,
                           ErrCode::PromptUnavailable, ErrCode::InvalidWorkflow, ErrCode::Internal};
    for (ErrCode c : all) {
        CHECK(std::strlen(toString(c)) > 0);
        CHECK(std::strcmp(toString(c), "Unknown") != 0);
        CHECK(std::strlen(describe(c)) > 0);
        CHECK(std::strcmp(describe(c), "Unknown error") != 0);
    }
}

TEST(Error, namesAreStable) {
    CHECK_EQ(std::string(toString(ErrCode::ElementNotFound)), std::string("ElementNotFound"));
    CHECK_EQ(std::string(toString(ErrCode::WindowElevated)), std::string("WindowElevated"));
}

TEST(Error, retryableSet) {
    CHECK(isRetryable(ErrCode::ElementNotFound));
    CHECK(isRetryable(ErrCode::Timeout));
    CHECK(isRetryable(ErrCode::ExpectationFailed));
    CHECK(isRetryable(ErrCode::ActivationFailed));
    CHECK(!isRetryable(ErrCode::ElementAmbiguous));
    CHECK(!isRetryable(ErrCode::WindowElevated));
    CHECK(!isRetryable(ErrCode::Cancelled));
    CHECK(!isRetryable(ErrCode::InvalidWorkflow));
    CHECK(!isRetryable(ErrCode::Unsupported));
}

TEST(Error, statusAndResult) {
    Status ok = Status::success();
    CHECK(ok.ok());
    Status bad = Status::fail(ErrCode::Timeout, "slow");
    CHECK(!bad.ok());
    CHECK_EQ(bad.detail, std::string("slow"));

    Result<int> r = Result<int>::success(42);
    CHECK(r.ok());
    CHECK_EQ(r.value, 42);
    Result<int> f = Result<int>::fail(ErrCode::Cancelled, "x");
    CHECK(!f.ok());
    CHECK(f.code() == ErrCode::Cancelled);
}
