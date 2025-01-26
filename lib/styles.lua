--- User stylesheets.
--
-- This module provides support for Mozilla-format user stylesheets, as a
-- replacement for the old `domain_props`-based `user_stylesheet_uri` method
-- (which is no longer supported). User stylesheets from https://userstyles.org
-- are supported, giving access to a wide variety of already-made themes.
--
-- User stylesheets are automatically detected and loaded when luakit starts up.
-- In addition, user stylesheets can be enabled/disabled instantly, without
-- refreshing the web pages they affect, and it is possible to reload external
-- changes to stylesheets into luakit, without restarting the browser.
--
-- # Adding user stylesheets
--
-- 1. Ensure the @ref{styles} module is enabled in your `rc.lua`.
-- 2. Locate the @ref{styles} sub-directory within luakit's data storage directory.
--    Normally, this is located at `~/.local/share/luakit/styles/`. Create the
--    directory if it does not already exist.
-- 3. Move any CSS rules to a new file within that directory. In order for the
--    @ref{styles} module to load the stylesheet, the filename must end in `.css`.
-- 4. Make sure you specify which sites your stylesheet should apply to. The way to
--    do this is to use `@-moz-document` rules. The Stylish wiki page [Applying styles to specific sites
--    ](https://github.com/stylish-userstyles/stylish/wiki/Applying-styles-to-specific-sites) may be helpful.
-- 5. Run `:styles-reload` to detect new stylesheet files and reload any changes to
--    existing stylesheet files; it isn't necessary to restart luakit.
--
-- # Using the styles menu
--
-- To open the styles menu, run the command `:styles-list`. Here you can
-- enable/disable stylesheets, open stylesheets in your text editor, and view
-- which stylesheets are active.
--
-- If a stylesheet is disabled for all pages, its state will be listed as
-- "Disabled". If a stylesheet is enabled for all pages, but does not apply to
-- the current page, its state will be listed as "Enabled". If a stylesheet is
-- enbaled for all pages _and_ it applies to the current page, its state will be
-- listed as "Active".
--
-- @module styles
-- @copyright 2017 Aidan Holm <aidanholm@gmail.com>

local window = require("window")
local webview = require("webview")
local lousy   = require("lousy")
local lfs     = require("lfs")
local editor  = require("editor")
local binds, modes = require("binds"), require("modes")
local new_mode = require("modes").new_mode
local add_binds, add_cmds = modes.add_binds, modes.add_cmds
local menu_binds = binds.menu_binds

local _M = {}

local styles_dir = luakit.data_dir .. "/styles/"

local stylesheets = {}

local db = sqlite3{ filename = luakit.data_dir .. "/styles.db" }
db:exec("PRAGMA synchronous = OFF; PRAGMA secure_delete = 1;")

local query_create = db:compile [[
    CREATE TABLE IF NOT EXISTS by_file (
        id INTEGER PRIMARY KEY,
        file TEXT,
        enabled INTEGER
    );]]

query_create:exec()

local query_insert = db:compile [[ INSERT INTO by_file VALUES (NULL, ?, ?) ]]
local query_update = db:compile [[ UPDATE by_file SET enabled = ? WHERE id == ?  ]]
local query_select = db:compile [[ SELECT * FROM by_file WHERE file == ?  ]]

local function db_get(file)
    assert(file)
    local rows = query_select:exec{file}
    return (rows[1] and rows[1].enabled or 1) ~= 0
end

local function db_set(file, enabled)
    assert(file)
    local rows = query_select:exec{file}
    if rows[1] then
        query_update:exec{enabled, rows[1].id}
    else
        query_insert:exec{file, enabled}
    end
end

-- Routines to extract an array of domain names from a URI

local function domain_from_uri(uri)
    if not uri or uri == "" then return nil end
    uri = assert(lousy.uri.parse(uri), "invalid uri")
    -- Return the scheme for non-http/https URIs
    if uri.scheme ~= "http" and uri.scheme ~= "https" then
        return uri.scheme
    else
        return string.lower(uri.host)
    end
end

local function domains_from_uri(uri)
    local domain = domain_from_uri(uri)
    local domains = { }
    while domain do
        domains[#domains + 1] = domain
        domain = string.match(domain, "%.(.+)")
    end
    return domains
end

-- Routines to re-apply all stylesheets to a given webview

local function update_stylesheet_application(view, domains, stylesheet, enabled)
    local anymatch

    for _, part in ipairs(stylesheet.parts) do
        local match = false
        for _, w in ipairs(part.when) do
            match = match or (w[1] == "url" and w[2] == view.uri)
            match = match or (w[1] == "url-prefix" and view.uri:find(w[2],1,true) == 1)
            match = match or (w[1] == "regexp" and w[2]:match(view.uri))
            if w[1] == "domain" then
                for _, domain in ipairs(domains) do
                    match = match or (w[2] == domain)
                end
            end
        end
        anymatch = anymatch or match
        match = match and stylesheet.enabled and enabled
        view.stylesheets[part.ss] = match
    end

    return anymatch
end

local function update_stylesheet_application_otherwise(view, domains, stylesheet, enabled)
    -- Just check for otherwise items.
    for _, part in ipairs(stylesheet.parts) do
        local match = false
        for _, w in ipairs(part.when) do
            match = match or (w[1] == 'otherwise')
        end
        match = match and stylesheet.enabled and enabled
        view.stylesheets[part.ss] = match
    end
end

-- Routines to update the stylesheet menu

local function update_stylesheet_applications(v)
    local anymatch
    local enabled = v:emit_signal("enable-styles")
    enabled = enabled ~= false and true
    local domains = domains_from_uri(v.uri)
    for _, s in ipairs(stylesheets or {}) do
        local a = update_stylesheet_application(v, domains, s, enabled ~= false )
        anymatch = anymatch or a
    end
    if not anymatch then
        for _, s in ipairs(stylesheets or {}) do
            update_stylesheet_application_otherwise(v, domains, s, enabled ~= false)
        end
    end
end

local function describe_stylesheet_affected_pages(stylesheet)
    local affects = {}
    for _, part in ipairs(stylesheet.parts) do
        for _, w in ipairs(part.when) do
            local w2 = w[1] == "regexp" and w[2].pattern:gsub("\\/", "/") or w[2]
            local desc = w[1] .. " " .. w2
            if not lousy.util.table.hasitem(affects, desc) then
                table.insert(affects, desc)
            end
        end
    end
    return table.concat(affects, ", ")
end

local menu_row_for_stylesheet = function (w, stylesheet)
    local theme = lousy.theme.get()
    local title = stylesheet.file
    local view = w.view

    -- Determine whether stylesheet is active for the current view
    local enabled, active = stylesheet.enabled, false
    if enabled then
        for _, part in ipairs(stylesheet.parts) do
            active = active or view.stylesheets[part.ss]
        end
    end

    local affects = describe_stylesheet_affected_pages(stylesheet)

    -- Determine state label and row colours
    local state, fg, bg
    if not enabled then
        state, fg, bg = "Disabled", theme.menu_disabled_fg, theme.menu_disabled_bg
    elseif not active then
        state, fg, bg = "Enabled", theme.menu_enabled_fg, theme.menu_enabled_bg
    else
        state, fg, bg = "Active", theme.menu_active_fg, theme.menu_active_bg
    end

    return { title, state, affects, stylesheet = stylesheet, fg = fg, bg = bg }
end

-- Routines to build and update stylesheet menus per-window

local stylesheets_menu_rows = setmetatable({}, { __mode = "k" })

local function create_stylesheet_menu_for_w(w)
    local rows = {{ "Stylesheets", "State", "Affects", title = true }}
    local groups = { Disabled = {}, Enabled = {}, Active = {}, }
    for _, stylesheet in ipairs(stylesheets) do
        local row = menu_row_for_stylesheet(w, stylesheet)
        table.insert(groups[row[2]], row)
    end
    rows = lousy.util.table.join(rows, groups.Active, groups.Enabled, groups.Disabled)
    w.menu:build(rows)
    stylesheets_menu_rows[w] = rows
end

local function update_stylesheet_menu_for_w(w)
    local rows = assert(stylesheets_menu_rows[w])
    for i=2,#rows do
        rows[i] = menu_row_for_stylesheet(w, rows[i].stylesheet)
    end
    w.menu:update()
end

local function update_stylesheet_menus()
    -- Update any windows in styles-list mode
    for _, w in pairs(window.bywidget) do
        if w:is_mode("styles-list") then
            update_stylesheet_menu_for_w(w)
        end
    end
end

local function update_all_stylesheet_applications()
    -- Update page appearances
    for _, ww in pairs(window.bywidget) do
        for _, v in pairs(ww.tabs.children) do
            update_stylesheet_applications(v)
        end
    end
    update_stylesheet_menus()
end

webview.add_signal("init", function (view)
    view:add_signal("stylesheet", function (v)
        if not view.uri:match("^view%-source:") then
            update_stylesheet_applications(v)
        end
    end)
end)

-- Routines to parse the @-moz-document format into CSS chunks

local parse_moz_document_subrule = function (file)
    local word, param, i
    word, i = file:match("^%s*([%w-]+)%s*()")
    file = file:sub(i)
    param, i = file:match("(%b())()")
    file = file:sub(i)
    param = param:match("^%(['\"]?(.-)['\"]?%)$")
    return file, word, param
end

local parse_moz_document_section = function (file, parts)
    file = file:gsub("^%s*%@%-moz%-document%f[%W]", "")
    local when = {}
    local word, param

    while true do
        -- Strip off a subrule
        file, word, param = parse_moz_document_subrule(file)
        local valid_words = { url = true, ["url-prefix"] = true, domain = true, regexp = true, otherwise = true }

        if valid_words[word] then
            if word == "regexp" then
                param = param:gsub("\\\\", "\\"):gsub("/","\\/")
                param = regex{pattern=param}
            end
            when[#when+1] = {word, param}
        else
            msg.warn("Ignoring unrecognized @-moz-document rule '%s'", word)
        end

        if file:match("^%s*,%s*") then
            file = file:sub(file:find(",")+1)
        else
            break
        end
    end
    local css, i = file:match("(%b{})()")
    css = css:sub(2, -2)
    file = file:sub(i)
    parts[#parts+1] = { when = when, css = css }

    return file
end

local parse_file = function (file)
    -- First, strip comments and @namespace
    file = file:gsub("%/%*.-%*%/","")
    file = file:gsub("%@namespace%s*url%b();?", "")
    -- Next, match moz document rules
    local parts = {}
    while file:find("^%s*%@%-moz%-document") do
        file = parse_moz_document_section(file, parts)
    end
    if file:find("%S") then
        parts[#parts+1] = { when = {{"url-prefix", ""}}, css = file }
    end
    return parts
end

local global_comment = "/* i really want this to be global */"
local file_looks_like_old_format = function (source)
    return not source:find("@-moz-document",1,true) and not source:lower():find(global_comment, 1, true)
end

--- Load the contents of a file as a stylesheet for a given domain.
-- @tparam string path The path of the file to load.
_M.load_file = function (path)
    if stylesheet == nil then return end

    local file = io.open(path, "r")
    local source = file:read("*all")
    file:close()

    if file_looks_like_old_format(source) then
        msg.error("stylesheet '%s' is global, refusing to load", path)
        msg.error("to load anyway, add %s to the file", global_comment)
        return true
    end

    local parsed = parse_file(source)

    local parts = {}
    for _, part in ipairs(parsed) do
        table.insert(parts, {
            ss = stylesheet{ source = part.css },
            when = part.when
        })
    end
    stylesheets[#stylesheets+1] = {
        parts = parts,
        file = path,
        enabled = db_get(path),
    }
end

--- Detect all files in the stylesheets directory and automatically load them.
_M.detect_files = function ()
    -- Create styles directory if it doesn't exist
    local cwd = lfs.currentdir()
    if not lfs.chdir(styles_dir) then
        lfs.mkdir(styles_dir)
        lfs.chdir(styles_dir)
    end

    for _, stylesheet in ipairs(stylesheets or {}) do
        for _, part in ipairs(stylesheet.parts) do
            for _, ww in pairs(window.bywidget) do
                for _, v in pairs(ww.tabs.children) do
                    v.stylesheets[part.ss] = false
                end
            end
        end
    end
    stylesheets = {}

    msg.verbose("searching for user stylesheets in %s", styles_dir)
    for filename in lfs.dir(styles_dir) do
        if string.find(filename, ".css$") then
            msg.verbose("found user stylesheet: " .. filename)
            _M.load_file(filename)
        end
    end
    msg.info("found " .. #stylesheets .. " user stylesheet" .. (#stylesheets == 1 and "" or "s"))

    update_all_stylesheet_applications()
    lfs.chdir(cwd)
end

--- Watch a stylesheet in the styles directory for changes and apply them immediately.
-- @tparam table guard a table that controls the watch process. Set `guard[1]
-- = nil` to turn off the watch.
-- @tparam string path the path of the watched style.
_M.watch_styles = function (guard, path)
    luakit.spawn("bash -c 'inotifywait -t 10 \"" .. path .. "\" || sleep 1'", function ()
        _M.detect_files()
        if guard[1] then _M.watch_styles(guard, path) end
    end)
end

--- Create and immediately edit a new style for the current uri.
-- @tparam table w The window table for the window providing the uri.
_M.new_style = function (w)
    -- Create styles directory if it doesn't exist
    local cwd = lfs.currentdir()
    if not lfs.chdir(styles_dir) then
        lfs.mkdir(styles_dir)
        lfs.chdir(styles_dir)
    end
    local path = string.match(w.view.uri, "//([%w*%.]+)") .. ".css"
    local exists = io.open(path, "r")
    if exists then
        exists:close()
        local guard = {0}
        _M.watch_styles(guard, path)
        editor.edit(path, 1, function() guard[1] = nil end)
    else
        local f = io.open(path, "w")
        if nil == f then w:notify(path)
        else
            f:write("@-moz-document url-prefix(\"" .. w.view.uri .. "\") {\n\n}")
            f:close()
            local guard = {0}
            _M.watch_styles(guard, path)
            editor.edit(path, 2, function() guard[1] = nil end)
        end
    end
    lfs.chdir(cwd)
end

--- Toggle the enabled status of a style by filename.
-- @tparam string title the style to toggle.
_M.toggle_sheet = function(title)
    for _, stylesheet in ipairs(stylesheets) do
        if stylesheet.file == title then
            stylesheet.enabled = not stylesheet.enabled
            db_set(stylesheet.file, stylesheet.enabled)
            update_all_stylesheet_applications()
        end
    end
end

add_cmds({
    { ":styles-reload, :sr", "Reload user stylesheets.", function (w)
            w:notify("styles: Reloading files...")
            _M.detect_files()
            w:notify("styles: Reloading files complete.")
        end },
    { ":styles-list", "List installed userstyles.",
        function (w) w:set_mode("styles-list") end },
    { ":styles-new", "Create new userstyle for this domain.", _M.new_style},
})

-- Add mode to display all userscripts in menu
new_mode("styles-list", {
    enter = function (w)
        if #stylesheets == 0 then
            w:notify("No userstyles installed.")
        else
            create_stylesheet_menu_for_w(w)
            w:notify("Use j/k to move, e edit, <space> enable/disable.", false)
        end
    end,

    leave = function (w)
        w.menu:hide()
    end,
})

add_binds("styles-list", lousy.util.table.join({
    -- Delete userscript
    { "<space>", "Enable/disable the currently highlighted userstyle.", function (w)
            local row = w.menu:get()
            if row and row.stylesheet then
                row.stylesheet.enabled = not row.stylesheet.enabled
                db_set(row.stylesheet.file, row.stylesheet.enabled)
                update_all_stylesheet_applications()
            end
        end },
    { "e", "Edit the currently highlighted userstyle.", function (w)
            local row = w.menu:get()
            if row and row.stylesheet then
                local file = luakit.data_dir .. "/styles/" .. row.stylesheet.file
                local guard = {0}
                _M.watch_styles(guard, file)
                editor.edit(file, 1, function() guard[1] = nil end)
            end
        end },
}, menu_binds))

_M.detect_files()

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
