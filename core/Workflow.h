// 流程领域模型。定位策略用 std::variant 表示，字段与策略强绑定。
// 带默认值的字段用 std::optional，这样 JSON 读写可以精确往返。
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace winauto {

// ----------------------------------------------------------------- 默认值
constexpr int kDefaultStepTimeoutMs = 5000;
constexpr int kDefaultWindowTimeoutMs = 5000;
constexpr int kDefaultRetryIntervalMs = 500;
constexpr int kDefaultExpectTimeoutMs = 2000;
constexpr int kPollIntervalMs = 100;
constexpr int kMaxTimeoutMs = 60000;
constexpr int kMaxRetryTimes = 10;

// ------------------------------------------------------------------ 定位器
enum class Match { Exact, Contains };
enum class Ambiguity { Error, First };  // First 会产生警告：目标不明确时盲点第一个
enum class PointAnchor { TopLeft, BottomRight, Ratio };

struct UiaLocator {
    std::string automationId;
    std::string name;
    std::string controlType;
    std::optional<Match> match;
    std::optional<Ambiguity> ambiguity;
};

// 窗口 client 区域内的物理像素；anchor=Ratio 时 x、y 为 [0,1] 比例
struct WindowPointLocator {
    double x = 0;
    double y = 0;
    std::optional<PointAnchor> anchor;
};

// 虚拟桌面物理像素（可为负）
struct ScreenPointLocator {
    int x = 0;
    int y = 0;
};

struct SearchRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ImageLocator {
    std::string templatePath;  // 相对路径，JSON 字段名为 "template"
    std::optional<double> minConfidence;
    std::optional<SearchRegion> searchRegion;
};

struct Locator;

// 显式回退：只有前一个策略返回 ElementNotFound 才尝试下一个
struct FallbackLocator {
    std::vector<Locator> locators;
};

struct Locator {
    std::variant<UiaLocator, WindowPointLocator, ScreenPointLocator, ImageLocator, FallbackLocator> value;
};

const char* strategyName(const Locator& loc);  // "uia" / "window_point" / ...
std::string describe(const Locator& loc);      // 用于日志的简短描述
bool isFallback(const Locator& loc);

// -------------------------------------------------------------------- 步骤
enum class StepType {
    ActivateWindow,
    Click,
    SetValue,
    InputText,
    Hotkey,
    Wait,
    WaitForElement,
    AssertElement
};
enum class ClickMode { Auto, Invoke, Mouse };
enum class InputMethod { Unicode, Clipboard, ValuePattern };
enum class FailurePolicy { Stop, Continue };

struct RetryPolicy {
    int times = 0;
    std::optional<int> intervalMs;
};

struct Expect {
    // 目前只有 element_exists
    Locator locator;
    std::optional<int> timeoutMs;
};

struct Step {
    std::string id;
    StepType type = StepType::Wait;
    std::optional<std::string> description;
    std::optional<std::string> window;  // windows 别名；wait 以外都必须提供
    std::optional<Locator> locator;
    std::optional<std::string> text;
    bool textFromPrompt = false;  // JSON: "textFrom": "prompt"，运行时询问，不落盘
    std::optional<bool> sensitive;
    std::optional<ClickMode> clickMode;
    std::optional<InputMethod> method;
    std::vector<std::string> keys;
    std::optional<int> durationMs;
    std::optional<int> timeoutMs;
    std::optional<Expect> expect;
    std::optional<RetryPolicy> retry;
    std::optional<FailurePolicy> onFailure;
};

// ---------------------------------------------------------------- 窗口/流程
struct WindowSpec {
    std::string processName;
    std::optional<std::string> titleContains;
    std::optional<int> timeoutMs;
};

struct NamedWindow {
    std::string alias;
    WindowSpec spec;
};

struct ExecutionOptions {
    std::optional<int> stepTimeoutMs;
    std::optional<FailurePolicy> onFailure;
    std::optional<bool> screenshotOnFailure;
};

struct Workflow {
    std::string schemaVersion = "1.0";
    std::string id;
    std::string name;
    std::vector<NamedWindow> windows;
    ExecutionOptions execution;
    std::vector<Step> steps;

    const WindowSpec* findWindow(const std::string& alias) const;
    int stepTimeoutMs(const Step& step) const;
    FailurePolicy failurePolicy(const Step& step) const;
};

// 步骤是否在运行时需要文本（literal 或 prompt）
bool stepUsesText(const Step& step);

// ------------------------------------------------------- 枚举 <-> 字符串
const char* toString(StepType v);
const char* toString(ClickMode v);
const char* toString(InputMethod v);
const char* toString(FailurePolicy v);
const char* toString(Match v);
const char* toString(Ambiguity v);
const char* toString(PointAnchor v);

bool parseStepType(std::string_view s, StepType* out);
bool parseClickMode(std::string_view s, ClickMode* out);
bool parseInputMethod(std::string_view s, InputMethod* out);
bool parseFailurePolicy(std::string_view s, FailurePolicy* out);
bool parseMatch(std::string_view s, Match* out);
bool parseAmbiguity(std::string_view s, Ambiguity* out);
bool parsePointAnchor(std::string_view s, PointAnchor* out);

}  // namespace winauto
