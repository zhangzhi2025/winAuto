#pragma once
// 领域模型：流程、步骤、定位器。纯数据，不含任何平台类型。
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace wa {

struct Point {
    int x = 0;
    int y = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    Point center() const { return Point{x + w / 2, y + h / 2}; }
};

// ---------------------------------------------------------------- 定位器
// 每种策略只携带自己的字段，不再用一个扁平结构塞所有字段。

enum class Match { Exact, Contains };
enum class Ambiguity { Error, First };  // 多个候选时：报错（默认）或取第一个

struct UiaLocator {
    std::string automationId;
    std::string name;
    std::string controlType;
    Match match = Match::Exact;
    Ambiguity ambiguity = Ambiguity::Error;
};

// 窗口相对坐标，基准是窗口 client 区域（不含标题栏和不可见边框），单位为物理像素。
enum class Anchor {
    TopLeft,      // x,y 为距 client 左上角的偏移
    BottomRight,  // x,y 为距 client 右下角向内的偏移
    Ratio         // x,y 为 0..1 的比例，窗口缩放后仍落在相同相对位置
};

struct WindowPointLocator {
    double x = 0;
    double y = 0;
    Anchor anchor = Anchor::TopLeft;
};

// 虚拟桌面物理像素坐标（多显示器下可能为负）
struct ScreenPointLocator {
    int x = 0;
    int y = 0;
};

struct ImageLocator {
    std::string templatePath;  // 相对路径，不得越出工作目录
    double minConfidence = 0.9;
    // 搜索范围，相对目标窗口 client 区域左上角，物理像素；不设置表示整个 client 区域。
    // 范围越小越快，也越不容易匹配到别处的相同图案。
    std::optional<Rect> searchRegion;
    // 多处都匹配时：报错（默认，绝不猜）或取最靠上（再靠左）的一处
    Ambiguity ambiguity = Ambiguity::Error;
};

using LeafLocator = std::variant<UiaLocator, WindowPointLocator, ScreenPointLocator, ImageLocator>;

// 回退链：按顺序尝试。仅在 ElementNotFound 时进入下一个，歧义直接失败。
// items 是 LeafLocator，因此"fallback 嵌套 fallback"在类型上就不可表达。
struct FallbackLocator {
    std::vector<LeafLocator> items;
};

using Locator =
    std::variant<UiaLocator, WindowPointLocator, ScreenPointLocator, ImageLocator, FallbackLocator>;

const char* strategyName(const Locator& l);      // "uia" / "window_point" / ...
const char* strategyName(const LeafLocator& l);

// ---------------------------------------------------------------- 步骤

enum class StepType {
    Unknown,  // 读入了未知类型，保留原文由校验器拒绝
    ActivateWindow,
    Click,
    SetValue,
    InputText,
    Hotkey,
    Wait,
    WaitForElement,
    AssertElement
};

enum class FailurePolicy { Stop, Continue };
enum class InputMethod { Unicode, Clipboard };
enum class ClickMode { Auto, Point };  // Auto：控件支持 Invoke 则直接调用，否则点击中心点

struct RetryPolicy {
    int times = 0;          // 失败后额外重试次数
    int intervalMs = 500;
};

struct Expect {
    std::string type = "element_exists";
    Locator locator;
};

struct WindowSpec {
    std::string processName;
    std::string titleContains;
    int timeoutMs = 5000;
};

struct Step {
    std::string id;
    StepType type = StepType::Unknown;
    std::string rawType;  // JSON 中的原始 type 文本
    std::string window;   // windows 别名，wait 步骤可为空

    std::optional<Locator> locator;

    // input_text / set_value
    std::optional<std::string> text;
    bool textFromPrompt = false;  // textFrom: "prompt"，运行时询问，不落盘
    bool sensitive = false;
    InputMethod method = InputMethod::Unicode;

    // click
    ClickMode clickMode = ClickMode::Auto;

    // hotkey
    std::vector<std::string> keys;

    // wait
    std::optional<int> durationMs;

    std::optional<int> timeoutMs;
    std::optional<Expect> expect;
    std::optional<RetryPolicy> retry;
    std::optional<FailurePolicy> onFailure;
};

struct ExecutionOptions {
    int stepTimeoutMs = 5000;
    FailurePolicy onFailure = FailurePolicy::Stop;
    bool screenshotOnFailure = false;
    RetryPolicy retry;
};

struct Workflow {
    std::string schemaVersion;
    std::string id;
    std::string name;
    std::map<std::string, WindowSpec> windows;
    ExecutionOptions execution;
    std::vector<Step> steps;

    // 读入时发现的未知字段（路径形式，如 "steps[1].locatr"），由校验器报错，防止拼写错误被静默忽略
    std::vector<std::string> unknownFields;
};

// ---------------------------------------------------------------- 字符串互转
const char* toString(StepType t);
StepType parseStepType(const std::string& s);  // 未知返回 StepType::Unknown
bool requiresWindow(StepType t);
bool requiresLocator(StepType t);

inline constexpr const char* kSchemaVersion = "1.0";
constexpr int kMaxTimeoutMs = 60000;

}  // namespace wa
