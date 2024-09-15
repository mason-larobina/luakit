--- NoScript plugin for luakit.
--
-- This module provides a method of restricting web page access to JavaScript
-- and plugins.
--
-- This module provides keybindings for enabling/disabling either plugins or
-- JavaScript for the current web page, as well as a status bar widget that
-- indicates whether JavaScript is enabled for the current web page.
--
-- @module noscript
-- @copyright 2011 Mason Larobina <mason.larobina@gmail.com>

local window = require("window")
local webview = require("webview")
local modes = require("modes")
local add_binds = modes.add_binds
local lousy = require("lousy")
local sql_escape = lousy.util.sql_escape
local theme = require("theme")

local _M = {}

--- Whether JavaScript should be enabled by default.
-- @type boolean
-- @readwrite
-- @default `true`
_M.enable_scripts = true

--- Whether plugins should be enabled by default.
-- @type boolean
-- @readwrite
-- @default `true`
_M.enable_plugins = true

local create_table = [[
CREATE TABLE IF NOT EXISTS by_domain (
    id INTEGER PRIMARY KEY,
    domain TEXT,
    enable_scripts INTEGER,
    enable_plugins INTEGER
);]]

local db = sqlite3{ filename = luakit.data_dir .. "/noscript.db" }
db:exec("PRAGMA synchronous = OFF; PRAGMA secure_delete = 1;")
db:exec(create_table)

local function btoi(bool) return bool and 1 or 0    end
local function itob(int)  return tonumber(int) ~= 0 end

local function get_domain(uri)
    uri = lousy.uri.parse(uri)
    -- uri parsing will fail on some URIs, e.g. "about:blank"
    return (uri and uri.host) and string.lower(uri.host) or nil
end

local function match_domain(domain)
    local rows = db:exec(string.format("SELECT * FROM by_domain "
        .. "WHERE domain == %s;", sql_escape(domain)))
    if rows[1] then return rows[1] end
end

local function update(id, field, value)
    db:exec(string.format("UPDATE by_domain SET %s = %d WHERE id == %d;",
        field, btoi(value), id))
end

local function insert(domain, enable_scripts, enable_plugins)
    db:exec(string.format("INSERT INTO by_domain VALUES (NULL, %s, %d, %d);",
        sql_escape(domain), btoi(enable_scripts), btoi(enable_plugins)))
end

function webview.methods.toggle_scripts(view, w)
    local domain = get_domain(view.uri)
    local enable_scripts = _M.enable_scripts
    local row = match_domain(domain)

    if row then
        enable_scripts = itob(row.enable_scripts)
        update(row.id, "enable_scripts", not enable_scripts)
    else
        insert(domain, not enable_scripts, _M.enable_plugins)
    end

    w:notify(string.format("%sabled scripts for domain: %s",
        enable_scripts and "Dis" or "En", domain))
end

function webview.methods.toggle_plugins(view, w)
    local domain = get_domain(view.uri)
    local enable_plugins = _M.enable_plugins
    local row = match_domain(domain)

    if row then
        enable_plugins = itob(row.enable_plugins)
        update(row.id, "enable_plugins", not enable_plugins)
    else
        insert(domain, _M.enable_scripts, not enable_plugins)
    end

    w:notify(string.format("%sabled plugins for domain: %s",
        enable_plugins and "Dis" or "En", domain))
end

function webview.methods.toggle_remove(view, w)
    local domain = get_domain(view.uri)
    db:exec(string.format("DELETE FROM by_domain WHERE domain == %s;",
        sql_escape(domain)))
    w:notify("Removed rules for domain: " .. domain)
end

local function string_starts(a, b)
    return string.sub(a, 1, string.len(b)) == b
end

local function lookup_domain(uri)
    if not uri then uri = "" end
    local enable_scripts, enable_plugins = _M.enable_scripts, _M.enable_plugins
    local domain = get_domain(uri)
    -- Enable everything for local pages
    if string_starts(uri, "file://") then return true, true, "file://" end

    -- Look up this domain and all parent domains, returning the first match
    -- E.g. querying a.b.com will lookup a.b.com, then b.com, then com
    while domain do
        local row = match_domain(domain)
        if row then
            return itob(row.enable_scripts), itob(row.enable_plugins), row.domain
        end
        domain = string.match(domain, "%.(.+)")
    end

    return enable_scripts, enable_plugins, nil
end

-- NoScript indicator

local view_noscript_state = setmetatable({}, { __mode = "k" })

local function noscript_indicator_update(v)
    local vns = view_noscript_state[v]
    local w = webview.window(v)
    if not vns or not w then return end

    local ns = w.sbar.r.noscript
    local es, matched_domain = vns.enable_scripts, vns.enable_scripts_domain
    local state = es and "enabled" or "disabled"

    if es then
        ns.text = "S" or "<s>S</s>"
        ns.fg = theme.trust_fg
    else
        ns.text = "<s>S</s>"
        ns.fg = theme.notrust_fg
    end

    if matched_domain == "override" then
        ns.tooltip = "JavaScript " .. state
    elseif matched_domain then
        ns.tooltip = "JavaScript " .. state .. ": URI matched domain '" .. matched_domain .. "'"
    else
        ns.tooltip = "JavaScript " .. state .. ": default setting"
    end
end

local noscript_ss = stylesheet{ source = [===[noscript { display: none !important; }]===] }

window.add_signal("init", function (w)
    local r = w.sbar.r
    r.noscript = widget{type="label"}
    r.layout:pack(r.noscript)
    r.layout:reorder(r.noscript, 1)
    r.noscript.font = theme.font
end)

local update_webview_blocking = function (v)
    local es = v:emit_signal("enable-scripts")
    local ep = v:emit_signal("enable-plugins")
    local vns = {
        enable_scripts_domain = es and "override" or nil,
        enable_plugins_domain = ep and "override" or nil,
    }
    if es == nil or ep == nil then
        local s, p, matched_domain = lookup_domain(v.uri)
        if es == nil then es = s; vns.enable_scripts_domain = matched_domain end
        if ep == nil then ep = p; vns.enable_plugins_domain = matched_domain end
    end
    vns.enable_scripts = es
    vns.enable_plugins = ep
    v.enable_scripts = es
    v.enable_plugins = ep
    -- Update indicator
    view_noscript_state[v] = vns
    noscript_indicator_update(v)
    -- Workaround for https://github.com/aidanholm/luakit/issues/250
    v.stylesheets[noscript_ss] = es
end

webview.add_signal("init", function (view)
    -- Update on new resource load
    view:add_signal("navigation-request", function (v, _, _)
        update_webview_blocking(v)
    end)

    -- Update on history navigation
    view:add_signal("load-status", function (v, status)
        if status == "committed" then
            update_webview_blocking(v)
        end
    end)

    view:add_signal("switched-page", function (v)
        noscript_indicator_update(v)
    end)
end)


add_binds("normal", {
    { "^,ts$", "Enable/disable JavaScript for the current domain.",
        function (w) w:toggle_scripts() end },
    { "^,tp$", "Enable/disable plugins for the current domain.",
        function (w) w:toggle_plugins() end },
    { "^,tr$", "Remove all previously added rules for the current domain.",
        function (w) w:toggle_remove()  end },
})

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
