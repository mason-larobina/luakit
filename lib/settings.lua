--- Centralized settings system.
--
-- The `settings` module provides a central place to access and modify settings
-- for all of luakit's modules.
--
-- @module settings
-- @author Aidan Holm
-- @copyright 2017 Aidan Holm <aidanholm@gmail.com>

local lousy = require("lousy")

local _M = {}

lousy.signal.setup(_M, true)

local settings_list = {}
local settings_groups

local S = {
    domain = { [""] = {}, }, -- keyed by domain, then by setting name
    source = { [""] = {}, }, -- can be default, persisted, config, or a module name
    view_overrides = setmetatable({}, { __mode = "k" }),
}

local persisted_settings
do
    local ok
    ok, persisted_settings = pcall(function ()
        local path = luakit.data_dir .. "/settings"
        return lousy.pickle.unpickle(lousy.load(path))
    end)
    if not ok then persisted_settings = { domain = {}, } end

    -- move .global to .domain[""]
    local pgs = persisted_settings.domain[""] or {}
    persisted_settings.domain[""] = pgs
    if persisted_settings.global then
        for sn, v in pairs(persisted_settings.global) do
            pgs[sn] = v
        end
        persisted_settings.global = nil
    end
end

local function validate_settings_path (k)
    assert(type(k) == "string", "invalid settings path type: " .. type(k))
    local parts = lousy.util.string.split(k, "%.")
    assert(#parts >= 2, "settings path must have at least two sections")
    for _, part in ipairs(parts) do
        assert(part ~= "on" and part:match("^[%w_]+$"),
            "invalid settings path component '" .. k .."'")
    end
end

local function get_settings_path_type (k)
    if not settings_groups then
        settings_groups = {}
        for path in pairs(settings_list) do
            local parts = lousy.util.string.split(path, "%.")
            table.remove(parts)
            local gk
            for _, part in ipairs(parts) do
                gk = (gk and (gk..".") or "") .. part
                settings_groups[gk] = true
            end
        end
    end
    if settings_groups[k] then return "group" end
    if settings_list[k] then return "value" end
end

--- Register a table of settings.
-- Entries in the table of settings to register should be keyed by the setting
-- path string.
-- @tparam {[string]=table} settings The table of settings to register.
--
-- ## Parameter table format
--
-- Each value elements of the `settings` argument is in the following format:
--
--     { type, default }
--
-- * `type`: _string_, the type of the setting value. When a container, use
--   `conatiner_name:inner_type` (the colon is the important part). Example for
--   an array of strings: `"array:string"`
-- * `default`: _`type`_, the default setting value when it isn't set manually
_M.register_settings = function (list)
    assert(type(list) == "table")

    -- Hack: certain tests rely on re-requiring modules, but registered settings
    -- aren't tied to the lifetime of a module, so prevent repeat-registers failing
    if package.loaded["tests.lib"] then
        for k in pairs(list) do settings_list[k] = nil end
    end

    for k, s in pairs(list) do
        validate_settings_path(k)
        assert(type(s) == "table", "setting '"..k.."' not a table")
        assert(not settings_list[k], "setting '"..k.."' already registered")
        assert(type(s.type) == "string", "setting '"..k.."' missing type")
    end

    -- Set default or load from persisted_settings
    local clone = lousy.util.table.clone
    for k, s in pairs(list) do
        settings_list[k] = s

        S.domain[""][k] = s.type:find(":") and clone(s.default) or s.default
        S.source[""][k] = "default"

        for domain, settings in pairs(persisted_settings.domain) do
            if settings[k] ~= nil then
                if S.domain[domain] == nil then
                    S.domain[domain] = {}
                    S.source[domain] = {}
                end
                local v = settings[k]
                S.domain[domain][k] = type(v) == "table" and clone(v) or v
                S.source[domain][k] = "persisted"
            end
        end
    end

    settings_groups = nil
end

local function setting_validate_new_kv_pair (meta, k, v)
    local ktype, vtype = meta.type:match("^(.*):(.*)$")
    for kk, vv in pairs(v) do
        if ktype ~= "" and type(kk) ~= ktype then
            error(string.format("Wrong type for setting '%s' key: expected %s, got %s",
                k, ktype, type(kk)))
        end
        if vtype ~= "" and type(vv) ~= vtype then
            error(string.format("Wrong type for setting '%s' value (key %s): expected %s, got %s",
                k, kk, vtype, type(vv)))
        end
    end
end

local function setting_validate_new_value (section, k, v)
    section = section or ""
    local meta = assert(settings_list[k], "bad setting " .. k)
    if meta.domain_specific == true and not section then
        error(string.format("Setting '%s' is domain-specific", k))
    elseif meta.domain_specific == false and section ~= "" then
        error(string.format("Setting '%s' cannot be domain-specific", k))
    end
    if meta.type == "enum" then
        if not meta.options[v] then
            local opts = table.concat(lousy.util.table.keys(meta.options), ", ")
            error(string.format("Wrong type for setting '%s': expected one of %s",
                k, opts))
        end
    elseif not meta.type:find(":") and type(v) ~= meta.type then
        error(string.format("Wrong type for setting '%s': expected %s, got %s",
            k, meta.type, type(v)))
    elseif meta.type:find(":") then
        setting_validate_new_kv_pair(meta, k, v)
    end
    if meta.type == "number" then
        if (meta.min and v < meta.min) or (meta.max and v > meta.max) then
            local range = "[" .. tostring(meta.min or "") .. ".." .. tostring(meta.max or "") .. "]"
            error(string.format("Value outside accepted range %s for setting '%s': %s", range, k, v))
        end
    end
    if meta.validator and not meta.validator(v) then
        error(string.format("Invalid value for setting '%s'", k))
    end
end

local function S_get(domain, key)
    domain = domain or ""
    local tree = S.domain[domain] or {}
    return tree[key] ~= nil and tree[key] or nil
end

local function get_overriding_module(domain, sn)
    local om = S.source[domain][sn]
    if om ~= "persisted" and om ~= "config" and om ~= "default" then
        return om
    end
end

local function S_set(domain, key, val, persist)
    domain = domain or ""
    setting_validate_new_value(domain, key, val)

    local function set(root, d, k, v)
        local tree = root.domain[d] or {}
        root.domain[d] = tree
        tree[k] = v
    end

    if persist then
        set(persisted_settings, domain, key, val)
        local fh = io.open(luakit.data_dir .. "/settings", "wb")
        fh:write(lousy.pickle.pickle(persisted_settings))
        io.close(fh)
    end

    S.source[domain] = S.source[domain] or {}
    local source = S.source[domain][key]
    if get_overriding_module(domain, key) then return end
    if persist and source == "config" then return end

    set(S, domain, key, val)
    S.source[domain][key] = persist and "persisted" or "config"
    _M.emit_signal("setting-changed", {
        key = key, value = val, domain = domain,
    })
end

local function S_set_table(domain, sn, key, val, persist)
    assert(not domain, "unimplemented")
    assert(not persist, "unimplemented")
    domain = domain or ""

    local meta = assert(settings_list[sn], "bad setting name "..sn)
    assert(meta.type:find(":"), sn .. " isn't a table setting")
    setting_validate_new_kv_pair(meta, sn, { [key]=val })

    if persist then
        local tbl = persisted_settings[domain][sn]
        tbl[key] = val
        local fh = io.open(luakit.data_dir .. "/settings", "wb")
        fh:write(lousy.pickle.pickle(persisted_settings))
        io.close(fh)
    end

    local source = S.source[domain][sn]
    if get_overriding_module(domain, sn) then return end
    if persist and source == "config" then return end

    local tbl = S.domain[domain][sn]
    tbl[key] = val
    S.source[domain][sn] = persist and "persisted" or "config"
    _M.emit_signal("setting-changed", {
        key = sn, value = val, domain = domain,
    })
end

local function new_settings_table_node(domain, sn)
    domain = domain or ""
    return setmetatable({}, {
        __index = function (_, k)
            local tbl = (S.domain[domain] or {})[sn] or {}
            return tbl[k]
        end,
        __newindex = function (_, k, v)
            S_set_table(nil, sn, k, v)
        end,
        __metatable = false,
    })
end

local function S_overwrite_table(domain, k, v)
    domain = domain or ""
    local tree, tbl = S.domain[domain] or {}, {}
    S.domain[domain] = tree
    tree[k] = tbl
    -- TODO: add validation for tables
    for kk, vv in pairs(v) do tbl[kk] = vv end
end

local uri_domain_cache = {}

--- Retrieve the value of a setting for a webview.
--
-- This function considers, in order:
--
-- 1. any view-specific overrides
-- 2. the setting's domain-specific values
-- 3. the setting's non-domain-specific value
-- 4. the setting's default value
--
-- The settings key must be a valid settings key.
-- @tparam widget view The webview.
-- @tparam string key The key of the setting to retrieve.
-- @return The value of the setting.
_M.get_setting_for_view = function (view, key)
    assert(type(view) == "widget" and view.type == "webview")
    -- view-specific overrides
    local tree, uri = S.view_overrides[view], view.uri
    if tree and tree[key] then return tree[key] end
    -- domain-specific values
    if uri ~= uri_domain_cache.uri then
        uri_domain_cache.uri = uri
        uri_domain_cache.domains = lousy.uri.domains_from_uri(uri)
    end
    local domains = uri_domain_cache.domains
    for _, domain in ipairs(domains) do
        local value = (S.domain[domain] or {})[key]
        if value ~= nil then return value, domain end
    end
    -- non-domain-specific / default value
    return S.domain[""][key]
end

--- Add or remove a view-specific override for a setting.
-- Passing `nil` as the `value` will clear any override.
--
-- The settings key must be a valid settings key.
-- @tparam widget view The webview.
-- @tparam string key The key of the setting to override.
-- @return The new value of the setting override.
_M.override_setting_for_view = function (view, key, value)
    assert(type(view) == "widget" and view.type == "webview")
    local vo = S.view_overrides
    vo[view] = vo[view] or {}
    vo[view][key] = value
end

--- Add an override for a setting.
--
-- The settings key must be a valid settings key.
-- @tparam string key The key of the setting to override.
-- @param The value of the setting override.
_M.override_setting = function (key, value)
    local mod = debug.getinfo(2, "S").short_src:gsub(".*/", ""):gsub("%.lua$","")
    local override = get_overriding_module("", key)
    if override and override ~= mod then error("already overriden by '"..override.."'") end
    S.source[""][key] = "default"
    S_set(nil, key, value)
    S.source[""][key] = mod
end

--- Retrieve the value of a setting, whether it's explicitly set or the default.
--
-- This does not take into account any domain-specific values.
--
-- The settings key must be a valid settings key.
-- @tparam string key The key of the setting to retrieve.
-- @return The value of the setting.
_M.get_setting = function (key)
    return S_get(nil, key)
end

--- Assign a value to a setting. Values assigned in this way are persisted to
-- disk, and automatically set when luakit starts.
--
-- The settings key must be a valid settings key.
-- @tparam string key The key of the setting to retrieve.
-- @param value The new value of the setting.
-- @tparam table opts Table of options. Currently the only valid field is
-- `domain`, which allows setting a domain-specific setting value.
_M.set_setting = function (key, value, opts)
    opts = opts or {}
    S_set(opts.domain, key, value, true)
end

--- Retrieve information about all registered settings and their values.
-- @treturn table A table of records, one for each setting.
_M.get_settings = function ()
    local ret = {}
    for k, meta in pairs(settings_list) do
        local value, src = _M.get_setting(k), S.source[""][k]
        if meta.type:find(":") then value = lousy.util.table.clone(value) end
        ret[k] = {
            type = meta.type,
            desc = meta.desc,
            value = value,
            src = src,
            options = meta.options,
            formatter = meta.formatter,
        }
    end
    return ret
end

local new_settings_node, root

local function new_domain_node()
    local meta = { __metatable = false, subnodes = {} }
    meta.__index = function (_, k)
        if meta.subnodes[k] then return meta.subnodes[k] end
        assert(type(k) == "string" and #k > 0, "invalid domain name")
        if k == "all" then
            msg.warn("settings.on[\"all\"].foo is deprecated: instead, use settings.foo")
            return root
        end
        meta.subnodes[k] = new_settings_node(nil, k)
        return meta.subnodes[k]
    end
    meta.__newindex = function ()
        error("cannot assign a value to a settings group")
    end
    return setmetatable({}, meta)
end

new_settings_node = function (prefix, section)
    assert(section ~= "")
    local meta = { __metatable = false, subnodes = {}, section = section }

    if not prefix and not section then -- True root node generates on[] subnode
        meta.subnodes.on = new_domain_node()
    end

    meta.__index = function (_, k)
        if meta.subnodes[k] then return meta.subnodes[k] end
        local full_path = (prefix and (prefix..".") or "") .. k
        local type = get_settings_path_type(full_path)
        if type == "value" then
            local getter = settings_list[full_path].type:find(":") and new_settings_table_node or S_get
            return (getter)(meta.section, full_path)
        end
        if type == "group" then
            meta.subnodes[k] = new_settings_node(full_path, meta.section)
            return meta.subnodes[k]
        end
    end
    meta.__newindex = function (_, k, v)
        local full_path = (prefix and (prefix..".") or "") .. k
        local type = get_settings_path_type(full_path)
            if string.match(full_path, "zoom_text_only") or
               string.match(full_path, "enable_offline_web_application_cache") or
               string.match(full_path, "load-icons-ignoring-image-load-setting")
               then msg.verbose("Ignoring setting: %s", full_path) return
            end
        if type == "value" then
            local setter = settings_list[full_path].type:find(":") and S_overwrite_table or S_set
            setter(meta.section, full_path, v)
        elseif type == "group" then
            error("cannot assign a value to a settings group")
        else
            error("cannot assign a value to invalid setting path '"..full_path.."'")
        end
    end
    return setmetatable({}, meta)
end

local migration_warnings = {}

--- Migration helper function.
-- @deprecated should be used only for existing code.
_M.add_migration_warning = function (k, v)
    if #migration_warnings == 0 then
        table.insert(migration_warnings, "Globals.lua is deprecated, and will be removed in the next release!")
        table.insert(migration_warnings, "To migrate, add the following to your rc.lua:")
        table.insert(migration_warnings, "")
        luakit.idle_add(function ()
            table.insert(migration_warnings, "")
            table.insert(migration_warnings, "Warnings have only been printed for settings with non-default values")
            msg.warn("%s", table.concat(migration_warnings, "\n"))
        end)
    end
    if type(v) == "string" then v = string.format("%q", v) end
    table.insert(migration_warnings, string.format("  settings.%s = %s", k, v))
end

--- Migration helper function.
-- @deprecated should be used only for existing code.
_M.migrate_global = function (sk, gk)
    local globals = package.loaded.globals or {}
    if globals[gk] and (globals[gk] ~= settings_list[sk].default) then
        _M.add_migration_warning(sk, globals[gk])
        S_set(nil, sk, globals[gk])
    end
end

root = new_settings_node()

return setmetatable(_M, { __index = root, __newindex = root })

-- vim: et:sw=4:ts=8:sts=4:tw=80
