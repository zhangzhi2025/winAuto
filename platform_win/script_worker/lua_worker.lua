local MAX_LINE = 1024 * 1024
local null = {}
local json = { null = null }
local next_request = 1

local function utf8(codepoint)
    if codepoint < 0x80 then return string.char(codepoint) end
    if codepoint < 0x800 then
        return string.char(0xC0 + math.floor(codepoint / 0x40),
                           0x80 + codepoint % 0x40)
    end
    if codepoint < 0x10000 then
        return string.char(0xE0 + math.floor(codepoint / 0x1000),
                           0x80 + math.floor(codepoint / 0x40) % 0x40,
                           0x80 + codepoint % 0x40)
    end
    return string.char(0xF0 + math.floor(codepoint / 0x40000),
                       0x80 + math.floor(codepoint / 0x1000) % 0x40,
                       0x80 + math.floor(codepoint / 0x40) % 0x40,
                       0x80 + codepoint % 0x40)
end

local function decode(text)
    local index = 1
    local function whitespace()
        while text:sub(index, index):match("%s") do index = index + 1 end
    end
    local function parse_string()
        index = index + 1
        local out = {}
        while index <= #text do
            local ch = text:sub(index, index)
            index = index + 1
            if ch == '"' then return table.concat(out) end
            if ch == "\\" then
                local escaped = text:sub(index, index)
                index = index + 1
                local simple = { ['"'] = '"', ["\\"] = "\\", ["/"] = "/",
                                 b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" }
                if simple[escaped] then
                    out[#out + 1] = simple[escaped]
                elseif escaped == "u" then
                    local hex = text:sub(index, index + 3)
                    local code = tonumber(hex, 16)
                    if not code then error("invalid unicode escape") end
                    index = index + 4
                    if code >= 0xD800 and code <= 0xDBFF and text:sub(index, index + 1) == "\\u" then
                        local low = tonumber(text:sub(index + 2, index + 5), 16)
                        if low and low >= 0xDC00 and low <= 0xDFFF then
                            code = 0x10000 + (code - 0xD800) * 0x400 + low - 0xDC00
                            index = index + 6
                        end
                    end
                    out[#out + 1] = utf8(code)
                else
                    error("invalid string escape")
                end
            else
                out[#out + 1] = ch
            end
        end
        error("unterminated string")
    end
    local parse_value
    parse_value = function(depth)
        if depth > 32 then error("JSON nesting limit exceeded") end
        whitespace()
        local ch = text:sub(index, index)
        if ch == '"' then return parse_string() end
        if ch == "{" then
            index = index + 1
            local value = setmetatable({}, { __json_kind = "object" })
            whitespace()
            if text:sub(index, index) == "}" then index = index + 1; return value end
            while true do
                whitespace()
                if text:sub(index, index) ~= '"' then error("expected object key") end
                local key = parse_string()
                whitespace()
                if text:sub(index, index) ~= ":" then error("expected colon") end
                index = index + 1
                value[key] = parse_value(depth + 1)
                whitespace()
                local separator = text:sub(index, index)
                index = index + 1
                if separator == "}" then return value end
                if separator ~= "," then error("expected comma") end
            end
        end
        if ch == "[" then
            index = index + 1
            local value = setmetatable({}, { __json_kind = "array" })
            whitespace()
            if text:sub(index, index) == "]" then index = index + 1; return value end
            while true do
                value[#value + 1] = parse_value(depth + 1)
                whitespace()
                local separator = text:sub(index, index)
                index = index + 1
                if separator == "]" then return value end
                if separator ~= "," then error("expected comma") end
            end
        end
        for token, value in pairs({ ["true"] = true, ["false"] = false, ["null"] = null }) do
            if text:sub(index, index + #token - 1) == token then
                index = index + #token
                return value
            end
        end
        local number = text:sub(index):match("^-?%d+%.?%d*[eE]?[+-]?%d*")
        if number and number ~= "" then
            local value = tonumber(number)
            if value then index = index + #number; return value end
        end
        error("invalid JSON value")
    end
    local value = parse_value(0)
    whitespace()
    if index <= #text then error("trailing JSON data") end
    return value
end

local function encode_string(value)
    return '"' .. value:gsub('[%z\1-\31\\"]', function(ch)
        local escaped = { ['"'] = '\\"', ["\\"] = "\\\\", ["\b"] = "\\b",
                          ["\f"] = "\\f", ["\n"] = "\\n", ["\r"] = "\\r",
                          ["\t"] = "\\t" }
        return escaped[ch] or string.format("\\u%04x", string.byte(ch))
    end) .. '"'
end

local function encode(value, depth)
    if depth > 32 then error("JSON nesting limit exceeded") end
    if value == null then return "null" end
    local kind = type(value)
    if kind == "nil" then return "null" end
    if kind == "boolean" then return value and "true" or "false" end
    if kind == "number" then
        if value ~= value or value == math.huge or value == -math.huge then error("non-finite number") end
        return tostring(value)
    end
    if kind == "string" then return encode_string(value) end
    if kind ~= "table" then error("unsupported JSON value") end
    local mt = getmetatable(value)
    local array = mt and mt.__json_kind == "array"
    if not mt or not mt.__json_kind then
        array = true
        for key in pairs(value) do
            if type(key) ~= "number" or key < 1 or key % 1 ~= 0 then array = false; break end
        end
    end
    local out = {}
    if array then
        local count, maximum = 0, 0
        for key in pairs(value) do
            if type(key) ~= "number" or key < 1 or key % 1 ~= 0 then error("invalid array index") end
            count = count + 1
            maximum = math.max(maximum, key)
        end
        if maximum ~= count then error("sparse arrays are not valid JSON") end
        for i = 1, maximum do out[#out + 1] = encode(value[i], depth + 1) end
        return "[" .. table.concat(out, ",") .. "]"
    end
    for key, item in pairs(value) do
        if type(key) ~= "string" then error("object keys must be strings") end
        out[#out + 1] = encode_string(key) .. ":" .. encode(item, depth + 1)
    end
    return "{" .. table.concat(out, ",") .. "}"
end

json.encode = function(value) return encode(value, 0) end
json.decode = decode
json.array = function(value) return setmetatable(value or {}, { __json_kind = "array" }) end
json.object = function(value) return setmetatable(value or {}, { __json_kind = "object" }) end

local function send(value)
    local line = json.encode(value)
    if #line > MAX_LINE then error("protocol message too large") end
    io.stdout:write(line, "\n")
    io.stdout:flush()
end

local function receive()
    local line = io.stdin:read("*l")
    if not line or #line > MAX_LINE then error("invalid or oversized protocol input") end
    return decode(line)
end

local function call(operation, arguments)
    local id = next_request
    next_request = next_request + 1
    send({ type = "request", request_id = id, operation = operation, arguments = arguments })
    local response = receive()
    if response.type == "cancel" then error("cancelled") end
    if response.type ~= "response" or response.request_id ~= id then error("invalid host response") end
    if not response.ok then error("host operation failed: " .. tostring(response.error.code)) end
    return response.result
end

local data = {
    get = function(key)
        local result = call("data.get", { key = key })
        if not result.found then return nil end
        return result.value
    end,
    set = function(key, value) return call("data.set", { key = key, value = value }) end
}
local automation = {
    activate_window = function(window) return call("window.activate", { window = window }) end,
    read = function(window, locator) return call("element.read", { window = window, locator = locator }) end,
    click = function(window, locator) return call("element.click", { window = window, locator = locator }) end,
    move = function(window, locator) return call("element.move", { window = window, locator = locator }) end,
    set_value = function(window, locator, text)
        return call("element.set_value", { window = window, locator = locator, text = text })
    end,
    input_text = function(window, text) return call("input.text", { window = window, text = text }) end,
    hotkey = function(window, keys) return call("input.hotkey", { window = window, keys = keys }) end
}

_G.json = json
_G.data = data
_G.automation = automation

local function main()
    local start = receive()
    if start.type ~= "start" or start.version ~= 1 or start.language ~= "lua" or
       type(start.code) ~= "string" or type(start.data) ~= "table" then
        send({ type = "error", diagnostic = "invalid Lua worker start message" })
        return
    end
    local print_count = 0
    print = function(...)
        print_count = math.min(MAX_LINE, print_count + 1)
    end
    local chunk, compile_error
    if _VERSION ~= "Lua 5.1" and load then
        chunk, compile_error = load(start.code, "workflow-script", "t", _G)
    end
    if not chunk and loadstring then chunk, compile_error = loadstring(start.code, "workflow-script") end
    if not chunk then
        send({ type = "error", diagnostic = "syntax error" })
        return
    end
    local ok, err = pcall(function() chunk() end)
    if ok then
        send({ type = "complete" })
    else
        local message = tostring(err)
        local line = message:match(":(%d+):")
        send({ type = "error", diagnostic = "LuaError" .. (line and (" at line " .. line) or "") })
    end
end

local ok = pcall(main)
if not ok then
    pcall(function() send({ type = "error", diagnostic = "Lua worker protocol failure" }) end)
end
