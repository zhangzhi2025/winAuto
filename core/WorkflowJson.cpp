#include "core/WorkflowJson.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <set>

namespace winauto {

namespace {

using Issues = std::vector<Issue>;

void addError(Issues& is, const char* code, std::string path, std::string msg) {
    Issue i;
    i.severity = Severity::Error;
    i.code = code;
    i.path = std::move(path);
    i.message = std::move(msg);
    is.push_back(std::move(i));
}

std::string joinPath(const std::string& base, const std::string& key) {
    return base.empty() ? key : base + "." + key;
}
std::string indexPath(const std::string& base, std::size_t i) {
    return base + "[" + std::to_string(i) + "]";
}

// 带路径的字段读取器：构造时检查是否为对象，并报告不允许 / 未知的字段。
class Fields {
public:
    Fields(const JsonValue& v, std::string path, Issues& is, std::vector<std::string> allowed,
           const std::set<std::string>* knownElsewhere = nullptr)
        : obj_(v), path_(std::move(path)), is_(is) {
        if (!v.isObject()) {
            addError(is_, "E_WRONG_TYPE", path_, "expected an object");
            ok_ = false;
            return;
        }
        for (const std::string& key : v.keys()) {
            if (std::find(allowed.begin(), allowed.end(), key) != allowed.end()) continue;
            if (knownElsewhere && knownElsewhere->count(key)) {
                addError(is_, "E_FIELD_NOT_ALLOWED", joinPath(path_, key),
                         "field '" + key + "' is not allowed here");
            } else {
                addError(is_, "E_UNKNOWN_FIELD", joinPath(path_, key), "unknown field '" + key + "'");
            }
        }
    }

    bool ok() const { return ok_; }
    std::string at(const char* key) const { return joinPath(path_, key); }
    const std::string& path() const { return path_; }

    const JsonValue* get(const char* key) const { return ok_ ? obj_.find(key) : nullptr; }

    std::optional<std::string> str(const char* key, bool required = false) {
        const JsonValue* v = find(key, required);
        if (!v) return std::nullopt;
        if (!v->isString()) return wrongType(key, "a string"), std::nullopt;
        return v->asString();
    }
    std::optional<int> integer(const char* key, bool required = false) {
        const JsonValue* v = find(key, required);
        if (!v) return std::nullopt;
        if (!v->isInt()) return wrongType(key, "an integer"), std::nullopt;
        if (v->asInt() < INT_MIN || v->asInt() > INT_MAX) {
            addError(is_, "E_RANGE", at(key), "integer is out of range");
            return std::nullopt;
        }
        return static_cast<int>(v->asInt());
    }
    std::optional<double> number(const char* key, bool required = false) {
        const JsonValue* v = find(key, required);
        if (!v) return std::nullopt;
        if (!v->isNumber()) return wrongType(key, "a number"), std::nullopt;
        return v->asDouble();
    }
    std::optional<bool> boolean(const char* key, bool required = false) {
        const JsonValue* v = find(key, required);
        if (!v) return std::nullopt;
        if (!v->isBool()) return wrongType(key, "a boolean"), std::nullopt;
        return v->asBool();
    }
    const JsonValue* object(const char* key, bool required = false) {
        const JsonValue* v = find(key, required);
        if (!v) return nullptr;
        if (!v->isObject()) return wrongType(key, "an object"), nullptr;
        return v;
    }
    const JsonValue* array(const char* key, bool required = false) {
        const JsonValue* v = find(key, required);
        if (!v) return nullptr;
        if (!v->isArray()) return wrongType(key, "an array"), nullptr;
        return v;
    }
    template <class E>
    std::optional<E> enumeration(const char* key, bool (*parse)(std::string_view, E*), const char* allowed,
                                 bool required = false) {
        auto s = str(key, required);
        if (!s) return std::nullopt;
        E e{};
        if (!parse(*s, &e)) {
            addError(is_, "E_BAD_ENUM", at(key), "invalid value '" + *s + "', expected one of: " + allowed);
            return std::nullopt;
        }
        return e;
    }

private:
    const JsonValue* find(const char* key, bool required) {
        const JsonValue* v = get(key);
        if (!v && required && ok_)
            addError(is_, "E_MISSING_FIELD", at(key), std::string("missing required field '") + key + "'");
        return v;
    }
    void wrongType(const char* key, const char* expected) {
        addError(is_, "E_WRONG_TYPE", at(key), std::string("expected ") + expected);
    }

    const JsonValue& obj_;
    std::string path_;
    Issues& is_;
    bool ok_ = true;
};

// ------------------------------------------------------------------ locator

constexpr int kMaxLocatorDepth = 8;

void readLocator(const JsonValue& v, const std::string& path, Issues& is, Locator* out, int depth) {
    if (!v.isObject()) {
        addError(is, "E_WRONG_TYPE", path, "expected an object");
        return;
    }
    if (depth > kMaxLocatorDepth) {
        addError(is, "E_TOO_DEEP", path, "locator nesting is too deep");
        return;
    }
    const JsonValue* s = v.find("strategy");
    if (!s) {
        addError(is, "E_MISSING_FIELD", joinPath(path, "strategy"), "missing required field 'strategy'");
        return;
    }
    if (!s->isString()) {
        addError(is, "E_WRONG_TYPE", joinPath(path, "strategy"), "expected a string");
        return;
    }
    const std::string& strat = s->asString();

    if (strat == "uia") {
        Fields f(v, path, is, {"strategy", "automationId", "name", "controlType", "match", "ambiguity"});
        UiaLocator u;
        u.automationId = f.str("automationId").value_or("");
        u.name = f.str("name").value_or("");
        u.controlType = f.str("controlType").value_or("");
        u.match = f.enumeration<Match>("match", parseMatch, "exact, contains");
        u.ambiguity = f.enumeration<Ambiguity>("ambiguity", parseAmbiguity, "error, first");
        out->value = std::move(u);
    } else if (strat == "window_point") {
        Fields f(v, path, is, {"strategy", "x", "y", "anchor"});
        WindowPointLocator w;
        w.anchor = f.enumeration<PointAnchor>("anchor", parsePointAnchor, "top_left, bottom_right, ratio");
        if (w.anchor && *w.anchor == PointAnchor::Ratio) {
            w.x = f.number("x", true).value_or(0);
            w.y = f.number("y", true).value_or(0);
        } else {
            w.x = f.integer("x", true).value_or(0);
            w.y = f.integer("y", true).value_or(0);
        }
        out->value = std::move(w);
    } else if (strat == "screen_point") {
        Fields f(v, path, is, {"strategy", "x", "y"});
        ScreenPointLocator p;
        p.x = f.integer("x", true).value_or(0);
        p.y = f.integer("y", true).value_or(0);
        out->value = p;
    } else if (strat == "image") {
        Fields f(v, path, is, {"strategy", "template", "minConfidence", "searchRegion"});
        ImageLocator im;
        im.templatePath = f.str("template", true).value_or("");
        im.minConfidence = f.number("minConfidence");
        if (const JsonValue* r = f.object("searchRegion")) {
            Fields rf(*r, f.at("searchRegion"), is, {"x", "y", "width", "height"});
            SearchRegion sr;
            sr.x = rf.integer("x", true).value_or(0);
            sr.y = rf.integer("y", true).value_or(0);
            sr.width = rf.integer("width", true).value_or(0);
            sr.height = rf.integer("height", true).value_or(0);
            im.searchRegion = sr;
        }
        out->value = std::move(im);
    } else if (strat == "fallback") {
        Fields f(v, path, is, {"strategy", "locators"});
        FallbackLocator fb;
        if (const JsonValue* arr = f.array("locators", true)) {
            for (std::size_t i = 0; i < arr->size(); ++i) {
                Locator child;
                readLocator(arr->items()[i], indexPath(f.at("locators"), i), is, &child, depth + 1);
                fb.locators.push_back(std::move(child));
            }
        }
        out->value = std::move(fb);
    } else {
        addError(is, "E_UNKNOWN_STRATEGY", joinPath(path, "strategy"),
                 "unknown strategy '" + strat + "', expected one of: uia, window_point, screen_point, image, fallback");
    }
}

// --------------------------------------------------------------------- step

const std::set<std::string>& allStepFields() {
    static const std::set<std::string> k = {"id",         "type",       "description", "onFailure", "window",
                                            "locator",    "text",       "textFrom",    "sensitive", "clickMode",
                                            "method",     "keys",       "durationMs",  "timeoutMs", "expect",
                                            "retry"};
    return k;
}

std::vector<std::string> allowedStepFields(StepType t) {
    std::vector<std::string> a = {"id", "type", "description", "onFailure"};
    if (t == StepType::Wait) {
        a.push_back("durationMs");
        return a;
    }
    for (const char* k : {"window", "timeoutMs", "expect", "retry"}) a.push_back(k);
    switch (t) {
    case StepType::Click:
        a.push_back("locator");
        a.push_back("clickMode");
        break;
    case StepType::SetValue:
        for (const char* k : {"locator", "text", "textFrom", "sensitive"}) a.push_back(k);
        break;
    case StepType::InputText:
        for (const char* k : {"text", "textFrom", "sensitive", "method", "locator"}) a.push_back(k);
        break;
    case StepType::Hotkey: a.push_back("keys"); break;
    case StepType::WaitForElement:
    case StepType::AssertElement: a.push_back("locator"); break;
    default: break;
    }
    return a;
}

void readStep(const JsonValue& v, const std::string& path, Issues& is, Step* out) {
    if (!v.isObject()) {
        addError(is, "E_WRONG_TYPE", path, "expected an object");
        return;
    }
    const JsonValue* t = v.find("type");
    if (!t) {
        addError(is, "E_MISSING_FIELD", joinPath(path, "type"), "missing required field 'type'");
        return;
    }
    if (!t->isString()) {
        addError(is, "E_WRONG_TYPE", joinPath(path, "type"), "expected a string");
        return;
    }
    StepType type{};
    if (!parseStepType(t->asString(), &type)) {
        addError(is, "E_UNKNOWN_STEP_TYPE", joinPath(path, "type"),
                 "unknown step type '" + t->asString() + "'");
        return;
    }
    out->type = type;

    Fields f(v, path, is, allowedStepFields(type), &allStepFields());
    out->id = f.str("id", true).value_or("");
    out->description = f.str("description");
    out->window = f.str("window");
    if (const JsonValue* l = f.get("locator")) {
        Locator loc;
        readLocator(*l, f.at("locator"), is, &loc, 0);
        out->locator = std::move(loc);
    }
    out->text = f.str("text");
    if (auto tf = f.str("textFrom")) {
        if (*tf == "prompt") out->textFromPrompt = true;
        else addError(is, "E_BAD_ENUM", f.at("textFrom"), "invalid value '" + *tf + "', expected one of: prompt");
    }
    out->sensitive = f.boolean("sensitive");
    out->clickMode = f.enumeration<ClickMode>("clickMode", parseClickMode, "auto, invoke, mouse");
    out->method = f.enumeration<InputMethod>("method", parseInputMethod, "unicode, clipboard, value_pattern");
    if (const JsonValue* keys = f.array("keys")) {
        for (std::size_t i = 0; i < keys->size(); ++i) {
            const JsonValue& k = keys->items()[i];
            if (!k.isString()) {
                addError(is, "E_WRONG_TYPE", indexPath(f.at("keys"), i), "expected a string");
                continue;
            }
            out->keys.push_back(k.asString());
        }
    }
    out->durationMs = f.integer("durationMs");
    out->timeoutMs = f.integer("timeoutMs");
    if (const JsonValue* e = f.object("expect")) {
        Fields ef(*e, f.at("expect"), is, {"type", "locator", "timeoutMs"});
        Expect ex;
        if (auto et = ef.str("type", true)) {
            if (*et != "element_exists")
                addError(is, "E_BAD_ENUM", ef.at("type"), "invalid value '" + *et + "', expected one of: element_exists");
        }
        if (const JsonValue* l = ef.get("locator")) readLocator(*l, ef.at("locator"), is, &ex.locator, 0);
        else ef.str("locator", true);  // 触发 E_MISSING_FIELD
        ex.timeoutMs = ef.integer("timeoutMs");
        out->expect = std::move(ex);
    }
    if (const JsonValue* r = f.object("retry")) {
        Fields rf(*r, f.at("retry"), is, {"times", "intervalMs"});
        RetryPolicy rp;
        rp.times = rf.integer("times", true).value_or(0);
        rp.intervalMs = rf.integer("intervalMs");
        out->retry = rp;
    }
    out->onFailure = f.enumeration<FailurePolicy>("onFailure", parseFailurePolicy, "stop, continue");
}

// ------------------------------------------------------------------ writers

JsonValue S(const std::string& s) { return JsonValue::makeString(s); }
JsonValue I(long long i) { return JsonValue::makeInt(i); }

JsonValue writeLocator(const Locator& loc) {
    JsonValue o = JsonValue::makeObject();
    o.set("strategy", S(strategyName(loc)));
    if (const auto* u = std::get_if<UiaLocator>(&loc.value)) {
        if (!u->automationId.empty()) o.set("automationId", S(u->automationId));
        if (!u->name.empty()) o.set("name", S(u->name));
        if (!u->controlType.empty()) o.set("controlType", S(u->controlType));
        if (u->match) o.set("match", S(toString(*u->match)));
        if (u->ambiguity) o.set("ambiguity", S(toString(*u->ambiguity)));
    } else if (const auto* w = std::get_if<WindowPointLocator>(&loc.value)) {
        const bool ratio = w->anchor && *w->anchor == PointAnchor::Ratio;
        if (ratio) {
            o.set("x", JsonValue::makeDouble(w->x));
            o.set("y", JsonValue::makeDouble(w->y));
        } else {
            o.set("x", I(std::llround(w->x)));
            o.set("y", I(std::llround(w->y)));
        }
        if (w->anchor) o.set("anchor", S(toString(*w->anchor)));
    } else if (const auto* p = std::get_if<ScreenPointLocator>(&loc.value)) {
        o.set("x", I(p->x));
        o.set("y", I(p->y));
    } else if (const auto* im = std::get_if<ImageLocator>(&loc.value)) {
        o.set("template", S(im->templatePath));
        if (im->minConfidence) o.set("minConfidence", JsonValue::makeDouble(*im->minConfidence));
        if (im->searchRegion) {
            JsonValue r = JsonValue::makeObject();
            r.set("x", I(im->searchRegion->x));
            r.set("y", I(im->searchRegion->y));
            r.set("width", I(im->searchRegion->width));
            r.set("height", I(im->searchRegion->height));
            o.set("searchRegion", std::move(r));
        }
    } else if (const auto* fb = std::get_if<FallbackLocator>(&loc.value)) {
        JsonValue arr = JsonValue::makeArray();
        for (const auto& c : fb->locators) arr.push(writeLocator(c));
        o.set("locators", std::move(arr));
    }
    return o;
}

JsonValue writeStep(const Step& s) {
    JsonValue o = JsonValue::makeObject();
    o.set("id", S(s.id));
    o.set("type", S(toString(s.type)));
    if (s.description) o.set("description", S(*s.description));
    if (s.window) o.set("window", S(*s.window));
    if (s.locator) o.set("locator", writeLocator(*s.locator));
    if (s.text) o.set("text", S(*s.text));
    if (s.textFromPrompt) o.set("textFrom", S("prompt"));
    if (s.sensitive) o.set("sensitive", JsonValue::makeBool(*s.sensitive));
    if (s.clickMode) o.set("clickMode", S(toString(*s.clickMode)));
    if (s.method) o.set("method", S(toString(*s.method)));
    if (!s.keys.empty()) {
        JsonValue arr = JsonValue::makeArray();
        for (const auto& k : s.keys) arr.push(S(k));
        o.set("keys", std::move(arr));
    }
    if (s.durationMs) o.set("durationMs", I(*s.durationMs));
    if (s.timeoutMs) o.set("timeoutMs", I(*s.timeoutMs));
    if (s.expect) {
        JsonValue e = JsonValue::makeObject();
        e.set("type", S("element_exists"));
        e.set("locator", writeLocator(s.expect->locator));
        if (s.expect->timeoutMs) e.set("timeoutMs", I(*s.expect->timeoutMs));
        o.set("expect", std::move(e));
    }
    if (s.retry) {
        JsonValue r = JsonValue::makeObject();
        r.set("times", I(s.retry->times));
        if (s.retry->intervalMs) r.set("intervalMs", I(*s.retry->intervalMs));
        o.set("retry", std::move(r));
    }
    if (s.onFailure) o.set("onFailure", S(toString(*s.onFailure)));
    return o;
}

}  // namespace

bool readWorkflow(const JsonValue& root, Workflow* out, std::vector<Issue>* issues) {
    Issues local;
    Issues& is = issues ? *issues : local;
    const std::size_t before = is.size();

    Workflow wf;
    Fields f(root, "", is, {"schemaVersion", "id", "name", "windows", "execution", "steps"});
    if (f.ok()) {
        wf.schemaVersion = f.str("schemaVersion", true).value_or("");
        wf.id = f.str("id", true).value_or("");
        wf.name = f.str("name", true).value_or("");

        if (const JsonValue* w = f.object("windows", true)) {
            for (std::size_t i = 0; i < w->size(); ++i) {
                const std::string& alias = w->keys()[i];
                const std::string wpath = joinPath("windows", alias);
                Fields wf2(w->valueAt(i), wpath, is, {"processName", "titleContains", "timeoutMs"});
                if (!wf2.ok()) continue;
                NamedWindow nw;
                nw.alias = alias;
                nw.spec.processName = wf2.str("processName", true).value_or("");
                nw.spec.titleContains = wf2.str("titleContains");
                nw.spec.timeoutMs = wf2.integer("timeoutMs");
                wf.windows.push_back(std::move(nw));
            }
        }
        if (const JsonValue* e = f.object("execution")) {
            Fields ef(*e, "execution", is, {"stepTimeoutMs", "onFailure", "screenshotOnFailure"});
            wf.execution.stepTimeoutMs = ef.integer("stepTimeoutMs");
            wf.execution.onFailure = ef.enumeration<FailurePolicy>("onFailure", parseFailurePolicy, "stop, continue");
            wf.execution.screenshotOnFailure = ef.boolean("screenshotOnFailure");
        }
        if (const JsonValue* steps = f.array("steps", true)) {
            for (std::size_t i = 0; i < steps->size(); ++i) {
                Step st;
                readStep(steps->items()[i], indexPath("steps", i), is, &st);
                wf.steps.push_back(std::move(st));
            }
        }
    }

    bool ok = true;
    for (std::size_t i = before; i < is.size(); ++i)
        if (is[i].severity == Severity::Error) ok = false;
    if (ok && out) *out = std::move(wf);
    return ok;
}

JsonValue writeWorkflow(const Workflow& wf) {
    JsonValue o = JsonValue::makeObject();
    o.set("schemaVersion", S(wf.schemaVersion));
    o.set("id", S(wf.id));
    o.set("name", S(wf.name));

    JsonValue windows = JsonValue::makeObject();
    for (const auto& nw : wf.windows) {
        JsonValue w = JsonValue::makeObject();
        w.set("processName", S(nw.spec.processName));
        if (nw.spec.titleContains) w.set("titleContains", S(*nw.spec.titleContains));
        if (nw.spec.timeoutMs) w.set("timeoutMs", I(*nw.spec.timeoutMs));
        windows.set(nw.alias, std::move(w));
    }
    o.set("windows", std::move(windows));

    const ExecutionOptions& ex = wf.execution;
    if (ex.stepTimeoutMs || ex.onFailure || ex.screenshotOnFailure) {
        JsonValue e = JsonValue::makeObject();
        if (ex.stepTimeoutMs) e.set("stepTimeoutMs", I(*ex.stepTimeoutMs));
        if (ex.onFailure) e.set("onFailure", S(toString(*ex.onFailure)));
        if (ex.screenshotOnFailure) e.set("screenshotOnFailure", JsonValue::makeBool(*ex.screenshotOnFailure));
        o.set("execution", std::move(e));
    }

    JsonValue steps = JsonValue::makeArray();
    for (const auto& s : wf.steps) steps.push(writeStep(s));
    o.set("steps", std::move(steps));
    return o;
}

}  // namespace winauto
