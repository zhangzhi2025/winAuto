#include "core/Json.h"

#include <limits>

#include "nlohmann/json.hpp"

namespace wa {

namespace {

using json = nlohmann::json;
using ojson = nlohmann::ordered_json;

// ================================================================ 读取

struct ReadError {
    std::string path;
    std::string message;
};

std::string childPath(const std::string& parent, const std::string& key) {
    return parent.empty() ? key : parent + "." + key;
}
std::string indexPath(const std::string& parent, size_t i) {
    return parent + "[" + std::to_string(i) + "]";
}

class Reader {
public:
    explicit Reader(Workflow& wf) : wf_(wf) {}

    Locator readLocatorPublic(const json& v, const std::string& path) { return readLocator(v, path); }

    void readRoot(const json& root) {
        if (!root.is_object()) throw ReadError{"", "root must be a JSON object"};
        noteUnknown(root, {"schemaVersion", "id", "name", "windows", "execution", "data", "steps"}, "");

        wf_.schemaVersion = str(root, "schemaVersion", "", "");
        wf_.id = str(root, "id", "", "");
        wf_.name = str(root, "name", "", "");

        if (const json* w = find(root, "windows")) {
            if (!w->is_object()) throw ReadError{"windows", "must be an object"};
            for (auto it = w->begin(); it != w->end(); ++it) {
                wf_.windows[it.key()] = readWindow(it.value(), childPath("windows", it.key()));
            }
        }
        if (const json* e = find(root, "execution")) {
            wf_.execution = readExecution(*e, "execution");
        }
        if (const json* data = find(root, "data")) {
            if (!data->is_object()) throw ReadError{"data", "must be an object"};
            wf_.initialDataJson = data->dump();
        }
        if (const json* s = find(root, "steps")) {
            if (!s->is_array()) throw ReadError{"steps", "must be an array"};
            wf_.steps = readSteps(*s, "steps", 0);
        }
    }

private:
    Workflow& wf_;

    static const json* find(const json& o, const char* key) {
        auto it = o.find(key);
        return it == o.end() ? nullptr : &*it;
    }

    void noteUnknown(const json& obj, std::initializer_list<const char*> known,
                     const std::string& path) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            bool ok = false;
            for (const char* k : known) {
                if (it.key() == k) {
                    ok = true;
                    break;
                }
            }
            if (!ok) wf_.unknownFields.push_back(childPath(path, it.key()));
        }
    }

    static void requireObject(const json& v, const std::string& path) {
        if (!v.is_object()) throw ReadError{path, "must be an object"};
    }

    std::string str(const json& o, const char* key, const std::string& def,
                    const std::string& path) {
        const json* v = find(o, key);
        if (!v) return def;
        if (!v->is_string()) throw ReadError{childPath(path, key), "must be a string"};
        return v->get<std::string>();
    }

    int integer(const json& o, const char* key, int def, const std::string& path) {
        const json* v = find(o, key);
        if (!v) return def;
        if (!v->is_number_integer()) throw ReadError{childPath(path, key), "must be an integer"};
        long long n = v->get<long long>();
        if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) {
            throw ReadError{childPath(path, key), "integer out of range"};
        }
        return static_cast<int>(n);
    }

    std::optional<int> optInteger(const json& o, const char* key, const std::string& path) {
        if (!find(o, key)) return std::nullopt;
        return integer(o, key, 0, path);
    }

    std::string numericText(const json& o, const char* key, const std::string& path) {
        const json* v = find(o, key);
        if (!v) return "";
        const std::string fieldPath = childPath(path, key);
        if (v->is_string()) return v->get<std::string>();
        if (v->is_number_unsigned()) return std::to_string(v->get<std::uint64_t>());
        if (v->is_number_integer()) return std::to_string(v->get<std::int64_t>());
        throw ReadError{fieldPath, "must be a string containing digits"};
    }

    double number(const json& o, const char* key, double def, const std::string& path) {
        const json* v = find(o, key);
        if (!v) return def;
        if (!v->is_number()) throw ReadError{childPath(path, key), "must be a number"};
        return v->get<double>();
    }

    bool boolean(const json& o, const char* key, bool def, const std::string& path) {
        const json* v = find(o, key);
        if (!v) return def;
        if (!v->is_boolean()) throw ReadError{childPath(path, key), "must be a boolean"};
        return v->get<bool>();
    }

    FailurePolicy failurePolicy(const std::string& s, const std::string& path) {
        if (s == "stop") return FailurePolicy::Stop;
        if (s == "continue") return FailurePolicy::Continue;
        throw ReadError{path, "must be \"stop\" or \"continue\""};
    }

    WindowSpec readWindow(const json& v, const std::string& path) {
        requireObject(v, path);
        noteUnknown(v, {"processName", "titleContains", "timeoutMs", "processId", "width", "height"}, path);
        WindowSpec w;
        w.processName = str(v, "processName", "", path);
        w.titleContains = str(v, "titleContains", "", path);
        w.timeoutMs = integer(v, "timeoutMs", w.timeoutMs, path);
        w.processId = numericText(v, "processId", path);
        w.width = numericText(v, "width", path);
        w.height = numericText(v, "height", path);
        return w;
    }

    RetryPolicy readRetry(const json& v, const std::string& path) {
        requireObject(v, path);
        noteUnknown(v, {"times", "intervalMs"}, path);
        RetryPolicy r;
        r.times = integer(v, "times", r.times, path);
        r.intervalMs = integer(v, "intervalMs", r.intervalMs, path);
        return r;
    }

    ExecutionOptions readExecution(const json& v, const std::string& path) {
        requireObject(v, path);
        noteUnknown(v, {"stepTimeoutMs", "onFailure", "screenshotOnFailure", "retry", "maxStepExecutions"}, path);
        ExecutionOptions e;
        e.stepTimeoutMs = integer(v, "stepTimeoutMs", e.stepTimeoutMs, path);
        if (find(v, "onFailure")) {
            e.onFailure = failurePolicy(str(v, "onFailure", "", path), childPath(path, "onFailure"));
        }
        e.screenshotOnFailure = boolean(v, "screenshotOnFailure", false, path);
        e.maxStepExecutions = integer(v, "maxStepExecutions", e.maxStepExecutions, path);
        if (const json* r = find(v, "retry")) e.retry = readRetry(*r, childPath(path, "retry"));
        return e;
    }

    LeafLocator readLeaf(const json& v, const std::string& path, const std::string& strategy) {
        if (strategy == "uia") {
            noteUnknown(v, {"strategy", "automationId", "name", "controlType", "match", "ambiguity"},
                        path);
            UiaLocator l;
            l.automationId = str(v, "automationId", "", path);
            l.name = str(v, "name", "", path);
            l.controlType = str(v, "controlType", "", path);
            if (find(v, "match")) {
                std::string m = str(v, "match", "", path);
                if (m == "exact") l.match = Match::Exact;
                else if (m == "contains") l.match = Match::Contains;
                else throw ReadError{childPath(path, "match"), "must be \"exact\" or \"contains\""};
            }
            if (find(v, "ambiguity")) {
                std::string a = str(v, "ambiguity", "", path);
                if (a == "error") l.ambiguity = Ambiguity::Error;
                else if (a == "first") l.ambiguity = Ambiguity::First;
                else throw ReadError{childPath(path, "ambiguity"), "must be \"error\" or \"first\""};
            }
            return l;
        }
        if (strategy == "window_point") {
            noteUnknown(v, {"strategy", "x", "y", "anchor"}, path);
            WindowPointLocator l;
            l.x = number(v, "x", 0, path);
            l.y = number(v, "y", 0, path);
            if (find(v, "anchor")) {
                std::string a = str(v, "anchor", "", path);
                if (a == "top_left") l.anchor = Anchor::TopLeft;
                else if (a == "bottom_right") l.anchor = Anchor::BottomRight;
                else if (a == "ratio") l.anchor = Anchor::Ratio;
                else {
                    throw ReadError{childPath(path, "anchor"),
                                    "must be \"top_left\", \"bottom_right\" or \"ratio\""};
                }
            }
            return l;
        }
        if (strategy == "screen_point") {
            noteUnknown(v, {"strategy", "x", "y"}, path);
            ScreenPointLocator l;
            l.x = integer(v, "x", 0, path);
            l.y = integer(v, "y", 0, path);
            return l;
        }
        if (strategy == "image") {
            noteUnknown(v, {"strategy", "template", "minConfidence", "searchRegion", "ambiguity"}, path);
            ImageLocator l;
            l.templatePath = str(v, "template", "", path);
            l.minConfidence = number(v, "minConfidence", l.minConfidence, path);
            if (find(v, "ambiguity")) {
                std::string a = str(v, "ambiguity", "", path);
                if (a == "error") l.ambiguity = Ambiguity::Error;
                else if (a == "first") l.ambiguity = Ambiguity::First;
                else throw ReadError{childPath(path, "ambiguity"), "must be \"error\" or \"first\""};
            }
            if (const json* r = find(v, "searchRegion")) {
                const std::string rp = childPath(path, "searchRegion");
                requireObject(*r, rp);
                noteUnknown(*r, {"x", "y", "width", "height"}, rp);
                Rect rect;
                rect.x = integer(*r, "x", 0, rp);
                rect.y = integer(*r, "y", 0, rp);
                rect.w = integer(*r, "width", 0, rp);
                rect.h = integer(*r, "height", 0, rp);
                l.searchRegion = rect;
            }
            return l;
        }
        throw ReadError{childPath(path, "strategy"), "unknown strategy \"" + strategy + "\""};
    }

    Locator readLocator(const json& v, const std::string& path) {
        requireObject(v, path);
        if (!find(v, "strategy")) throw ReadError{childPath(path, "strategy"), "is required"};
        const std::string strategy = str(v, "strategy", "", path);
        if (strategy == "fallback") {
            noteUnknown(v, {"strategy", "locators"}, path);
            FallbackLocator f;
            if (const json* arr = find(v, "locators")) {
                const std::string ap = childPath(path, "locators");
                if (!arr->is_array()) throw ReadError{ap, "must be an array"};
                for (size_t i = 0; i < arr->size(); ++i) {
                    const json& item = (*arr)[i];
                    const std::string ip = indexPath(ap, i);
                    requireObject(item, ip);
                    if (!find(item, "strategy")) {
                        throw ReadError{childPath(ip, "strategy"), "is required"};
                    }
                    const std::string s = str(item, "strategy", "", ip);
                    if (s == "fallback") {
                        throw ReadError{ip, "nested fallback is not allowed"};
                    }
                    f.items.push_back(readLeaf(item, ip, s));
                }
            }
            return f;
        }
        return std::visit([](auto&& leaf) -> Locator { return leaf; }, readLeaf(v, path, strategy));
    }

    Expect readExpect(const json& v, const std::string& path) {
        requireObject(v, path);
        noteUnknown(v, {"type", "locator"}, path);
        Expect e;
        e.type = str(v, "type", e.type, path);
        if (const json* l = find(v, "locator")) {
            e.locator = readLocator(*l, childPath(path, "locator"));
        } else {
            throw ReadError{childPath(path, "locator"), "is required"};
        }
        return e;
    }

    std::vector<Step> readSteps(const json& arr, const std::string& listPath, int depth) {
        if (!arr.is_array()) throw ReadError{listPath, "must be an array"};
        std::vector<Step> out;
        out.reserve(arr.size());
        for (size_t i = 0; i < arr.size(); ++i) {
            out.push_back(readStep(arr[i], indexPath(listPath, i), depth));
        }
        return out;
    }

    Step readStep(const json& v, const std::string& path, int depth) {
        requireObject(v, path);
        // 嵌套过深的文件在这里就拒绝：既防止递归爆栈，也和校验器的 kMaxNesting 保持一致
        if (depth > kMaxNesting) throw ReadError{path, "blocks are nested too deeply (max " + std::to_string(kMaxNesting) + ")"};
        noteUnknown(v,
                    {"id", "type", "window", "locator", "text", "textFrom", "method", "sensitive",
                     "mode", "keys", "durationMs", "timeoutMs", "expect", "retry", "onFailure",
                     "condition", "times", "maxIterations", "intervalMs", "then", "else", "body",
                     "language", "code"},
                    path);
        Step s;
        s.id = str(v, "id", "", path);
        s.rawType = str(v, "type", "", path);
        s.type = parseStepType(s.rawType);
        s.window = str(v, "window", "", path);

        if (s.type == StepType::Script) {
            if (find(v, "language")) {
                ScriptLanguage language{};
                const std::string value = str(v, "language", "", path);
                if (!parseScriptLanguage(value, &language)) {
                    throw ReadError{childPath(path, "language"), "must be \"python\" or \"lua\""};
                }
                s.scriptLanguage = language;
            }
            if (find(v, "code")) s.scriptCode = str(v, "code", "", path);
        } else {
            if (find(v, "language")) wf_.unknownFields.push_back(childPath(path, "language"));
            if (find(v, "code")) wf_.unknownFields.push_back(childPath(path, "code"));
        }

        if (const json* l = find(v, "locator")) s.locator = readLocator(*l, childPath(path, "locator"));

        if (find(v, "text")) s.text = str(v, "text", "", path);
        if (find(v, "textFrom")) {
            const std::string tf = str(v, "textFrom", "", path);
            if (tf != "prompt") throw ReadError{childPath(path, "textFrom"), "must be \"prompt\""};
            s.textFromPrompt = true;
        }
        s.sensitive = boolean(v, "sensitive", false, path);
        if (find(v, "method")) {
            const std::string m = str(v, "method", "", path);
            if (m == "unicode") s.method = InputMethod::Unicode;
            else if (m == "clipboard") s.method = InputMethod::Clipboard;
            else throw ReadError{childPath(path, "method"), "must be \"unicode\" or \"clipboard\""};
        }
        if (find(v, "mode")) {
            const std::string m = str(v, "mode", "", path);
            if (m == "auto") s.clickMode = ClickMode::Auto;
            else if (m == "point") s.clickMode = ClickMode::Point;
            else throw ReadError{childPath(path, "mode"), "must be \"auto\" or \"point\""};
        }
        if (const json* k = find(v, "keys")) {
            const std::string kp = childPath(path, "keys");
            if (!k->is_array()) throw ReadError{kp, "must be an array"};
            for (size_t i = 0; i < k->size(); ++i) {
                if (!(*k)[i].is_string()) throw ReadError{indexPath(kp, i), "must be a string"};
                s.keys.push_back((*k)[i].get<std::string>());
            }
        }
        s.durationMs = optInteger(v, "durationMs", path);
        s.timeoutMs = optInteger(v, "timeoutMs", path);
        if (const json* e = find(v, "expect")) s.expect = readExpect(*e, childPath(path, "expect"));
        if (const json* r = find(v, "retry")) s.retry = readRetry(*r, childPath(path, "retry"));
        if (find(v, "onFailure")) {
            s.onFailure = failurePolicy(str(v, "onFailure", "", path), childPath(path, "onFailure"));
        }

        // ---- 流程控制字段。只在对应的步骤类型上才有意义；放错地方的字段按"未知字段"报告，
        // 而不是静默忽略（否则 "times" 写在 click 上会让人以为点了多次）。
        const bool isIf = s.type == StepType::If;
        const bool isRepeat = s.type == StepType::Repeat;
        const bool isWhile = s.type == StepType::While;
        auto misplaced = [&](const char* key) { wf_.unknownFields.push_back(childPath(path, key)); };

        if (const json* c = find(v, "condition")) {
            if (isIf || isWhile) s.condition = readExpect(*c, childPath(path, "condition"));
            else misplaced("condition");
        }
        if (find(v, "times")) {
            if (isRepeat) s.times = optInteger(v, "times", path);
            else misplaced("times");
        }
        if (find(v, "maxIterations")) {
            if (isWhile) s.maxIterations = optInteger(v, "maxIterations", path);
            else misplaced("maxIterations");
        }
        if (find(v, "intervalMs")) {
            if (isRepeat || isWhile) s.intervalMs = optInteger(v, "intervalMs", path);
            else misplaced("intervalMs");
        }
        if (const json* t = find(v, "then")) {
            if (isIf) s.body = readSteps(*t, childPath(path, "then"), depth + 1);
            else misplaced("then");
        }
        if (const json* e = find(v, "else")) {
            if (isIf) s.elseBody = readSteps(*e, childPath(path, "else"), depth + 1);
            else misplaced("else");
        }
        if (const json* b = find(v, "body")) {
            if (isRepeat || isWhile) s.body = readSteps(*b, childPath(path, "body"), depth + 1);
            else misplaced("body");
        }
        return s;
    }
};

// ================================================================ 写出

const char* policyName(FailurePolicy p) { return p == FailurePolicy::Stop ? "stop" : "continue"; }

ojson writeRetry(const RetryPolicy& r) {
    ojson j = ojson::object();
    j["times"] = r.times;
    j["intervalMs"] = r.intervalMs;
    return j;
}

ojson writeLeaf(const LeafLocator& loc);

ojson writeLocator(const Locator& loc) {
    if (const auto* f = std::get_if<FallbackLocator>(&loc)) {
        ojson j = ojson::object();
        j["strategy"] = "fallback";
        ojson arr = ojson::array();
        for (const auto& item : f->items) arr.push_back(writeLeaf(item));
        j["locators"] = arr;
        return j;
    }
    return std::visit(
        [](auto&& v) -> ojson {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FallbackLocator>) {
                return ojson::object();  // 上面已处理，不会走到这里
            } else {
                return writeLeaf(LeafLocator{v});
            }
        },
        loc);
}

ojson writeLeaf(const LeafLocator& loc) {
    ojson j = ojson::object();
    j["strategy"] = strategyName(loc);
    if (const auto* u = std::get_if<UiaLocator>(&loc)) {
        if (!u->automationId.empty()) j["automationId"] = u->automationId;
        if (!u->name.empty()) j["name"] = u->name;
        if (!u->controlType.empty()) j["controlType"] = u->controlType;
        j["match"] = u->match == Match::Exact ? "exact" : "contains";
        j["ambiguity"] = u->ambiguity == Ambiguity::Error ? "error" : "first";
    } else if (const auto* w = std::get_if<WindowPointLocator>(&loc)) {
        if (w->anchor == Anchor::Ratio) {
            j["x"] = w->x;
            j["y"] = w->y;
        } else {
            j["x"] = static_cast<long long>(w->x);
            j["y"] = static_cast<long long>(w->y);
        }
        j["anchor"] = w->anchor == Anchor::TopLeft       ? "top_left"
                      : w->anchor == Anchor::BottomRight ? "bottom_right"
                                                         : "ratio";
    } else if (const auto* s = std::get_if<ScreenPointLocator>(&loc)) {
        j["x"] = s->x;
        j["y"] = s->y;
    } else if (const auto* im = std::get_if<ImageLocator>(&loc)) {
        j["template"] = im->templatePath;
        j["minConfidence"] = im->minConfidence;
        j["ambiguity"] = im->ambiguity == Ambiguity::Error ? "error" : "first";
        if (im->searchRegion) {
            ojson r = ojson::object();
            r["x"] = im->searchRegion->x;
            r["y"] = im->searchRegion->y;
            r["width"] = im->searchRegion->w;
            r["height"] = im->searchRegion->h;
            j["searchRegion"] = r;
        }
    }
    return j;
}

ojson writeStep(const Step& s);

ojson writeSteps(const std::vector<Step>& steps) {
    ojson arr = ojson::array();
    for (const auto& c : steps) arr.push_back(writeStep(c));
    return arr;
}

ojson writeExpect(const Expect& e) {
    ojson j = ojson::object();
    j["type"] = e.type;
    j["locator"] = writeLocator(e.locator);
    return j;
}

ojson writeStep(const Step& s) {
    ojson j = ojson::object();
    j["id"] = s.id;
    j["type"] = s.type == StepType::Unknown ? s.rawType : std::string(toString(s.type));
    if (s.type == StepType::Script) {
        if (s.scriptLanguage) j["language"] = toString(*s.scriptLanguage);
        if (s.scriptCode) j["code"] = *s.scriptCode;
    }
    if (!s.window.empty()) j["window"] = s.window;
    if (s.locator) j["locator"] = writeLocator(*s.locator);
    if (s.type == StepType::Click) j["mode"] = s.clickMode == ClickMode::Auto ? "auto" : "point";
    if (s.text) j["text"] = *s.text;
    if (s.textFromPrompt) j["textFrom"] = "prompt";
    if (s.type == StepType::InputText) {
        j["method"] = s.method == InputMethod::Unicode ? "unicode" : "clipboard";
    }
    if (s.type == StepType::InputText || s.type == StepType::SetValue) j["sensitive"] = s.sensitive;
    if (s.type == StepType::Hotkey || !s.keys.empty()) {
        ojson arr = ojson::array();
        for (const auto& k : s.keys) arr.push_back(k);
        j["keys"] = arr;
    }
    if (s.durationMs) j["durationMs"] = *s.durationMs;
    if (s.timeoutMs) j["timeoutMs"] = *s.timeoutMs;
    if (s.expect) j["expect"] = writeExpect(*s.expect);
    if (s.retry) j["retry"] = writeRetry(*s.retry);
    if (s.onFailure) j["onFailure"] = policyName(*s.onFailure);

    // 流程控制：只写出该类型用得到的字段，和读取时的规则对称
    if ((s.type == StepType::If || s.type == StepType::While) && s.condition) {
        j["condition"] = writeExpect(*s.condition);
    }
    if (s.type == StepType::Repeat && s.times) j["times"] = *s.times;
    if (s.type == StepType::While && s.maxIterations) j["maxIterations"] = *s.maxIterations;
    if ((s.type == StepType::Repeat || s.type == StepType::While) && s.intervalMs) {
        j["intervalMs"] = *s.intervalMs;
    }
    if (s.type == StepType::If) {
        j["then"] = writeSteps(s.body);
        if (!s.elseBody.empty()) j["else"] = writeSteps(s.elseBody);
    } else if (s.type == StepType::Repeat || s.type == StepType::While) {
        j["body"] = writeSteps(s.body);
    }
    return j;
}

}  // namespace

Result<Workflow> readWorkflow(const std::string& jsonText) {
    json root;
    try {
        root = json::parse(jsonText);
    } catch (const json::parse_error& e) {
        return Result<Workflow>::failure(ErrCode::InvalidWorkflow,
                                         std::string("JSON syntax error: ") + e.what());
    }
    Workflow wf;
    try {
        Reader(wf).readRoot(root);
    } catch (const ReadError& e) {
        const std::string where = e.path.empty() ? "$" : e.path;
        return Result<Workflow>::failure(ErrCode::InvalidWorkflow, where + ": " + e.message);
    }
    return Result<Workflow>::success(std::move(wf));
}

Result<Locator> readLocatorJson(const std::string& jsonText) {
    json root;
    try {
        root = json::parse(jsonText);
    } catch (const json::parse_error& e) {
        return Result<Locator>::failure(ErrCode::InvalidWorkflow, std::string("JSON syntax error: ") + e.what());
    }
    Workflow scratch;  // Reader 需要一个工作流来记录未知字段，这里用不上
    try {
        return Result<Locator>::success(Reader(scratch).readLocatorPublic(root, "locator"));
    } catch (const ReadError& e) {
        return Result<Locator>::failure(ErrCode::InvalidWorkflow, e.path + ": " + e.message);
    }
}

std::string locatorToJson(const Locator& locator, int indent) { return writeLocator(locator).dump(indent); }

std::string writeWorkflow(const Workflow& wf, int indent) {
    ojson j = ojson::object();
    j["schemaVersion"] = wf.schemaVersion;
    j["id"] = wf.id;
    j["name"] = wf.name;

    ojson windows = ojson::object();
    for (const auto& [alias, spec] : wf.windows) {
        ojson w = ojson::object();
        w["processName"] = spec.processName;
        w["titleContains"] = spec.titleContains;
        w["timeoutMs"] = spec.timeoutMs;
        if (!spec.processId.empty()) w["processId"] = spec.processId;
        if (!spec.width.empty()) w["width"] = spec.width;
        if (!spec.height.empty()) w["height"] = spec.height;
        windows[alias] = w;
    }
    j["windows"] = windows;

    ojson exec = ojson::object();
    exec["stepTimeoutMs"] = wf.execution.stepTimeoutMs;
    exec["onFailure"] = policyName(wf.execution.onFailure);
    exec["screenshotOnFailure"] = wf.execution.screenshotOnFailure;
    exec["retry"] = writeRetry(wf.execution.retry);
    if (wf.execution.maxStepExecutions != kDefaultMaxStepExecutions) {
        exec["maxStepExecutions"] = wf.execution.maxStepExecutions;  // 默认值不写出，保持文件简洁
    }
    j["execution"] = exec;

    if (wf.initialDataJson != "{}") j["data"] = json::parse(wf.initialDataJson);
    j["steps"] = writeSteps(wf.steps);

    return j.dump(indent);
}

}  // namespace wa
