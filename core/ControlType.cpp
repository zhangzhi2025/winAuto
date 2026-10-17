#include "core/ControlType.h"

namespace wa {

namespace {
// 顺序即 ID：下标 + 50000
const std::vector<std::string> kTypes = {
    "Button",   "Calendar", "CheckBox",   "ComboBox",   "Edit",     "Hyperlink", "Image",
    "ListItem", "List",     "Menu",       "MenuBar",    "MenuItem", "ProgressBar", "RadioButton",
    "ScrollBar", "Slider",  "Spinner",    "StatusBar",  "Tab",      "TabItem",   "Text",
    "ToolBar",  "ToolTip",  "Tree",       "TreeItem",   "Custom",   "Group",     "Thumb",
    "DataGrid", "DataItem", "Document",   "SplitButton", "Window",  "Pane",      "Header",
    "HeaderItem", "Table",  "TitleBar",   "Separator",  "SemanticZoom", "AppBar"};

std::string lowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}
}  // namespace

std::string controlTypeName(int id) {
    const int i = id - 50000;
    if (i < 0 || i >= static_cast<int>(kTypes.size())) return {};
    return kTypes[static_cast<size_t>(i)];
}

std::string canonicalControlType(const std::string& name) {
    const std::string want = lowerAscii(name);
    for (const std::string& t : kTypes) {
        if (lowerAscii(t) == want) return t;
    }
    return {};
}

const std::vector<std::string>& allControlTypes() { return kTypes; }

}  // namespace wa
