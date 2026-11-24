#include "RegionConfigJson.h"

#include <fstream>
#include <sstream>
#include <utility>

#include "nlohmann/json.hpp"

namespace stock_monitor {
namespace {

using ojson = nlohmann::ordered_json;
using json = nlohmann::json;

ConfigValidation failure(std::string path, std::string message) {
    return ConfigValidation{false, std::move(path), std::move(message)};
}

bool isObject(const json& node) { return node.is_object(); }

bool readString(const json& node,
                const char* key,
                const std::string& base,
                std::string& out,
                ConfigValidation& error) {
    const auto it = node.find(key);
    if(it == node.end() || !it->is_string()) {
        error = failure(base.empty() ? std::string(key) : base + "." + key, "必须是字符串");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool readInt(const json& node,
             const char* key,
             const std::string& base,
             int& out,
             ConfigValidation& error) {
    const auto it = node.find(key);
    if(it == node.end() || !it->is_number_integer()) {
        error = failure(base.empty() ? std::string(key) : base + "." + key, "必须是整数");
        return false;
    }
    out = it->get<int>();
    return true;
}

bool readBool(const json& node,
              const char* key,
              const std::string& base,
              bool& out,
              ConfigValidation& error) {
    const auto it = node.find(key);
    if(it == node.end() || !it->is_boolean()) {
        error = failure(base.empty() ? std::string(key) : base + "." + key, "必须是布尔值");
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool readChild(const json& node,
               const char* key,
               const std::string& base,
               json& out,
               ConfigValidation& error) {
    const auto it = node.find(key);
    const std::string path = base.empty() ? std::string(key) : base + "." + key;
    if(it == node.end() || !isObject(*it)) {
        error = failure(path, "必须是对象");
        return false;
    }
    out = *it;
    return true;
}

bool readRect(const json& node, const std::string& base, Rect& out, ConfigValidation& error) {
    json rect;
    if(!readChild(node, "rect", base, rect, error)) {
        return false;
    }
    const std::string rectPath = base + ".rect";
    return readInt(rect, "x", rectPath, out.x, error) &&
           readInt(rect, "y", rectPath, out.y, error) &&
           readInt(rect, "width", rectPath, out.width, error) &&
           readInt(rect, "height", rectPath, out.height, error);
}

bool readField(const json& node, const std::string& base, FieldDefinition& out, ConfigValidation& error) {
    if(!readString(node, "id", base, out.id, error) ||
       !readString(node, "name", base, out.name, error) ||
       !readInt(node, "x", base, out.x, error) || !readInt(node, "width", base, out.width, error)) {
        return false;
    }
    std::string type;
    if(!readString(node, "valueType", base, type, error)) {
        return false;
    }
    if(!parseFieldValueType(type, out.valueType)) {
        error = failure(base + ".valueType", "未知的取值类型: " + type);
        return false;
    }
    const auto analyze = node.find("analyzeFontColor");
    if(analyze == node.end()) {
        out.analyzeFontColor = false;
        return true;
    }
    return readBool(node, "analyzeFontColor", base, out.analyzeFontColor, error);
}

bool readTable(const json& node, const std::string& base, TableTemplate& out, ConfigValidation& error) {
    json table;
    if(!readChild(node, "table", base, table, error)) {
        return false;
    }
    const std::string tablePath = base + ".table";
    if(!readInt(table, "firstRowY", tablePath, out.firstRowY, error) ||
       !readInt(table, "rowHeight", tablePath, out.rowHeight, error) ||
       !readInt(table, "rowPitch", tablePath, out.rowPitch, error)) {
        return false;
    }
    const auto fields = table.find("fields");
    if(fields == table.end() || !fields->is_array()) {
        error = failure(tablePath + ".fields", "必须是数组");
        return false;
    }
    out.fields.clear();
    int index = 0;
    for(const auto& entry : *fields) {
        const std::string fieldPath = tablePath + ".fields[" + std::to_string(index) + "]";
        if(!isObject(entry)) {
            error = failure(fieldPath, "必须是对象");
            return false;
        }
        FieldDefinition field;
        if(!readField(entry, fieldPath, field, error)) {
            return false;
        }
        out.fields.push_back(std::move(field));
        ++index;
    }
    return true;
}

bool readRegion(const json& node, const std::string& base, CaptureRegion& out, ConfigValidation& error) {
    if(!readString(node, "id", base, out.id, error) || !readString(node, "name", base, out.name, error) ||
       !readRect(node, base, out.rect, error) || !readInt(node, "intervalMs", base, out.intervalMs, error) ||
       !readBool(node, "enabled", base, out.enabled, error)) {
        return false;
    }
    std::string mode;
    if(!readString(node, "mode", base, mode, error)) {
        return false;
    }
    if(!parseCaptureRegionMode(mode, out.mode)) {
        error = failure(base + ".mode", "未知的区域模式: " + mode);
        return false;
    }
    const auto analyze = node.find("analyzeFontColor");
    if(analyze != node.end() && !readBool(node, "analyzeFontColor", base, out.analyzeFontColor, error)) {
        return false;
    }
    if(out.mode != CaptureRegionMode::Table) {
        return true;
    }
    return readTable(node, base, out.table, error);
}

}  // namespace

std::string writeRegionConfigJson(const RegionCaptureConfig& config) {
    ojson root;
    root["schemaVersion"] = config.schemaVersion;
    root["configId"] = config.configId;
    root["name"] = config.name;
    root["targetWindow"]["title"] = config.targetWindow.title;
    root["targetWindow"]["processName"] = config.targetWindow.processName;
    root["referenceClientSize"]["width"] = config.referenceClientSize.width;
    root["referenceClientSize"]["height"] = config.referenceClientSize.height;
    root["referenceDpi"] = config.referenceDpi;
    root["dailyWindow"]["start"] = config.dailyWindow.start;
    root["dailyWindow"]["end"] = config.dailyWindow.end;

    ojson regions = ojson::array();
    for(const CaptureRegion& region : config.regions) {
        ojson item;
        item["id"] = region.id;
        item["name"] = region.name;
        item["rect"]["x"] = region.rect.x;
        item["rect"]["y"] = region.rect.y;
        item["rect"]["width"] = region.rect.width;
        item["rect"]["height"] = region.rect.height;
        item["intervalMs"] = region.intervalMs;
        item["enabled"] = region.enabled;
        item["mode"] = toString(region.mode);
        item["analyzeFontColor"] = region.analyzeFontColor;
        if(region.mode == CaptureRegionMode::Table) {
            item["table"]["firstRowY"] = region.table.firstRowY;
            item["table"]["rowHeight"] = region.table.rowHeight;
            item["table"]["rowPitch"] = region.table.rowPitch;
            ojson fields = ojson::array();
            for(const FieldDefinition& field : region.table.fields) {
                ojson entry;
                entry["id"] = field.id;
                entry["name"] = field.name;
                entry["x"] = field.x;
                entry["width"] = field.width;
                entry["valueType"] = toString(field.valueType);
                entry["analyzeFontColor"] = field.analyzeFontColor;
                fields.push_back(std::move(entry));
            }
            item["table"]["fields"] = std::move(fields);
        }
        regions.push_back(std::move(item));
    }
    root["regions"] = std::move(regions);
    return root.dump(2) + "\n";
}

RegionConfigParseResult readRegionConfigJson(const std::string& text) {
    RegionConfigParseResult result;
    json document;
    try {
        document = json::parse(text);
    } catch(const std::exception& ex) {
        result.validation = failure("", std::string("JSON 解析失败: ") + ex.what());
        return result;
    }
    if(!isObject(document)) {
        result.validation = failure("", "配置根节点必须是对象");
        return result;
    }

    RegionCaptureConfig config;
    ConfigValidation error;
    if(!readString(document, "schemaVersion", "", config.schemaVersion, error) ||
       !readString(document, "configId", "", config.configId, error) ||
       !readString(document, "name", "", config.name, error)) {
        result.validation = error;
        return result;
    }
    json targetWindow;
    if(!readChild(document, "targetWindow", "", targetWindow, error) ||
       !readString(targetWindow, "title", "targetWindow", config.targetWindow.title, error) ||
       !readString(targetWindow, "processName", "targetWindow", config.targetWindow.processName, error)) {
        result.validation = error;
        return result;
    }
    json referenceSize;
    if(!readChild(document, "referenceClientSize", "", referenceSize, error) ||
       !readInt(referenceSize, "width", "referenceClientSize", config.referenceClientSize.width, error) ||
       !readInt(referenceSize, "height", "referenceClientSize", config.referenceClientSize.height, error)) {
        result.validation = error;
        return result;
    }
    const auto dpi = document.find("referenceDpi");
    if(dpi != document.end() && !readInt(document, "referenceDpi", "", config.referenceDpi, error)) {
        result.validation = error;
        return result;
    }
    json dailyWindow;
    if(!readChild(document, "dailyWindow", "", dailyWindow, error) ||
       !readString(dailyWindow, "start", "dailyWindow", config.dailyWindow.start, error) ||
       !readString(dailyWindow, "end", "dailyWindow", config.dailyWindow.end, error)) {
        result.validation = error;
        return result;
    }
    const auto regions = document.find("regions");
    if(regions == document.end() || !regions->is_array()) {
        result.validation = failure("regions", "必须是数组");
        return result;
    }
    int index = 0;
    for(const auto& entry : *regions) {
        const std::string path = "regions[" + std::to_string(index) + "]";
        if(!isObject(entry)) {
            result.validation = failure(path, "必须是对象");
            return result;
        }
        CaptureRegion region;
        if(!readRegion(entry, path, region, error)) {
            result.validation = error;
            return result;
        }
        config.regions.push_back(std::move(region));
        ++index;
    }

    const ConfigValidation validation = validate(config);
    if(!validation.valid) {
        result.validation = validation;
        return result;
    }
    result.ok = true;
    result.config = std::move(config);
    return result;
}

RegionConfigParseResult readRegionConfigFile(const std::string& path) {
    RegionConfigParseResult result;
    std::ifstream stream(path, std::ios::binary);
    if(!stream) {
        result.validation = failure("", "无法打开配置文件: " + path);
        return result;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if(!stream.good() && !stream.eof()) {
        result.validation = failure("", "读取配置文件失败: " + path);
        return result;
    }
    return readRegionConfigJson(buffer.str());
}

bool writeRegionConfigFile(const std::string& path,
                           const RegionCaptureConfig& config,
                           std::string& error) {
    const ConfigValidation validation = validate(config);
    if(!validation.valid) {
        error = validation.path + ": " + validation.message;
        return false;
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if(!stream) {
        error = "无法写入配置文件: " + path;
        return false;
    }
    stream << writeRegionConfigJson(config);
    stream.flush();
    if(!stream) {
        error = "写入配置文件失败: " + path;
        return false;
    }
    return true;
}

}  // namespace stock_monitor
