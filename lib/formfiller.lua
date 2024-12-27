--- Provides functionality to auto-fill forms based on a Lua DSL.
--
-- The formfiller provides support for filling out forms based on the contents
-- of a forms file, which uses a domain-specific language to specify the content to
-- fill forms with.
--
-- The following is an example for a formfiller definition:
--
--     on "example.com" {
--         form "profile1" {
--             method = "post",
--             action = "/login",
--             className = "someFormClass",
--             id = "form_id",
--             input {
--                 name = "username",
--                 type = "text",
--                 className = "someClass",
--                 id = "username_field",
--                 value = "myUsername",
--             },
--             input {
--                 name = "password",
--                 value = "myPassword",
--             },
--             input {
--                 name = "autologin",
--                 type = "checkbox",
--                 checked = true,
--             },
--             submit = true,
--             autofill = false,
--         },
--     }
--
-- * The <code>form</code> function's string argument is optional.
--   It allows you to define multiple profiles for use with the
--   <code>zL</code> binding.
--
-- * All entries are matched top to bottom, until one fully matches
--   or calls <code>submit()</code>.
--
-- * The <code>submit</code> attribute of a form can also be a number, which
--   gives index of the submit button to click (starting with <code>1</code>).
--   If there is no such button or the argument is <code>true</code>,
--   <code>form.submit()</code> will be called instead.
--
-- * Instead of <code>submit</code>, you can also use <code>focus = true</code>
--   inside an <code>input</code> to focus that element or <code>select = true</code>
--   to select the text inside it.
--   <code>focus</code> will trigger input mode.
--
-- * The string argument to the <code>on</code> function (<code>example.com</code>
--   in the example above) takes a Lua pattern!
--   BEWARE its escaping!
--
-- * All of the attributes of the <code>form</code> and <code>input</code> tables
--   are matched as plain text.
--
-- * Setting <code>autofill = true</code> on a form definition will
--   automatically fill and possibly submit any matching forms when a web page
--   with a matching URI finishes loading. This is useful if you wish to have
--   login pages for various web services filled out automatically. It is
--   critically important, however, to verify that the URI pattern of the rule is
--   correct!
--
--   As a basic precaution, autofill only works if the web page domain
--   is present within the URI pattern.
--
-- There is a conversion script in the luakit repository that converts
-- from the old formfiller format to the new one. For more information,
-- see the converter script under <code>extras/convert_formfiller.rb</code>.
--
-- # Files and Directories
--
-- - The formfiller configuration is loaded from the `forms.lua` file stored in
--   the luakit data directory.
--
-- @module formfiller
-- @copyright 2011 Fabian Streitel <luakit@rottenrei.be>
-- @copyright 2011 Mason Larobina <mason.larobina@gmail.com>

local lousy = require("lousy")
local window = require("window")
local webview = require("webview")
local editor = require("editor")
local new_mode = require("modes").new_mode
local binds, modes = require("binds"), require("modes")
local add_binds = modes.add_binds
local menu_binds = binds.menu_binds

local _M = {}

local formfiller_wm = require_web_module("formfiller_wm")

-- The Lua DSL file containing the formfiller rules
local file = luakit.data_dir .. "/forms.lua"

-- The function environment for the formfiller script
local DSL = {
    print = function (_, ...) print(...) end,

    -- DSL method to match a page by its URI
    on = function (s, pattern)
        return function (forms)
            table.insert(s.rules, {
                pattern = pattern,
                forms = forms,
            })
        end
    end,

    -- DSL method to match a form by its attributes
    form = function (_, data)
        local transform = function (inputs, profile)
            local form = {
                profile = profile,
                inputs = {},
            }
            for k, v in pairs(inputs) do
                if type(k) == "number" then
                    form.inputs[k] = v
                else
                    form[k] = v
                end
            end
            return form
        end
        if type(data) == "string" then
            local profile = data
            return function (inputs)
                return transform(inputs, profile)
            end
        else
            return transform(data)
        end
    end,

    -- DSL method to match an input element by its attributes
    input = function (_, attrs)
        return attrs
    end,
}

local dsl_extensions = {}

formfiller_wm:add_signal("dsl_extension_query", function (_, k, arg, view_id)
    local reply = dsl_extensions[k](unpack(arg))
    formfiller_wm:emit_signal(view_id,"dsl_extension_reply", reply, view_id)
end)

--- Extend the formfiller DSL with additional functions. This takes a table of
-- functions. For example, to use the `pass` storage manager:
--
--     formfiller.extend({
--         pass = function(s) return io.popen("pass " .. s):read() end
--     })
--
-- which will then be usable in the fields of `form.lua`:
--
--     input {
--         name = "username",
--         value = pass("emailpassword"),
--     }
--
-- Functions used to extend the DSL will be called only when needed: when
-- matching for attributes used in matching, or once a form is applied, for
-- attributes used in form application.
--
-- @tparam table extensions The table of functions extending the formfiller DSL.
_M.extend = function (extensions)
    for k, v in pairs(extensions) do
        assert(type(v) == "function", "bad DSL extension: values must be functions")
        assert(type(k) == "string", "bad DSL extension: keys must be strings")
        assert(k ~= "on" and k ~= "form" and k ~= "input", "bad DSL extension: don't shadow core DSL functions")
    end
    dsl_extensions = extensions
    for k, _ in pairs(extensions) do
        DSL[k] = function (_, ...)
            return {sentinel = true, arg = {...}, key = k}
        end
    end
end

--- Reads the rules from the formfiller DSL file
local function read_formfiller_rules_from_file()
    local state = {
        rules = {},
    }
    -- the environment of the DSL script
    -- load the script
    local f = io.open(file, "r")
    if not f then return {} end -- file doesn't exist
    local code = f:read("*all")
    f:close()
    local dsl, message = loadstring(code)
    if not dsl then
        msg.warn(string.format("loading formfiller data failed: %s", message))
        return
    end
    -- execute in sandbox
    local env = {}
    -- wrap the DSL functions so they can access the state
    for k in pairs(DSL) do
        env[k] = function (...) return DSL[k](state, ...) end
    end
    setfenv(dsl, env)
    local success, err = pcall(dsl)
    if not success then
        msg.warn("error in %s: %s", file, err)
    end
    return state.rules
end

local function form_specs_for_uri (all_rules, uri)
    -- Filter rules to the given uri
    local rules = lousy.util.table.filter_array(all_rules, function(_, rule)
        return string.find(uri, rule.pattern)
    end)

    -- Get list of all form specs that can be matched
    local form_specs = {}
    for _, rule in ipairs(rules) do
        for _, form in ipairs(rule.forms) do
            form.pattern = rule.pattern
            form_specs[#form_specs + 1] = form
        end
    end

    return form_specs
end

--- Edits the formfiller rules.
local function edit()
    editor.edit(file)
end

local function w_from_view_id(view_id)
    assert(type(view_id) == "number", type(view_id))
    for _, w in pairs(window.bywidget) do
        if w.view.id == view_id then return w end
    end
end

formfiller_wm:add_signal("failed", function (_, view_id, msg)
    local w = w_from_view_id(view_id)
    w:error(msg)
    w:set_mode()
end)
formfiller_wm:add_signal("add", function (_, view_id, str)
    local w = w_from_view_id(view_id)
    w:set_mode()
    local f = io.open(file, "a")
    f:write(str)
    f:close()
    edit()
end)

--- Fills the current page from the formfiller rules.
-- @tparam table w The window on which to fill the forms.
local function fill_form_fast(w)
    local rules = read_formfiller_rules_from_file(w)
    local form_specs = form_specs_for_uri(rules, w.view.uri)
    if #form_specs == 0 then
        w:error("no rules matched")
        return
    end
    formfiller_wm:emit_signal(w.view, "fill-fast", form_specs)
end

-- Support for choosing a form with a menu
local function fill_form_menu(w)
    local rules = read_formfiller_rules_from_file(w)
    local form_specs = form_specs_for_uri(rules, w.view.uri)
    if #form_specs == 0 then
        w:error("no rules matched")
        return
    end
    formfiller_wm:emit_signal(w.view, "filter", form_specs)
end

formfiller_wm:add_signal("filter", function (_, view_id, form_specs)
    local w = w_from_view_id(view_id)
    -- Build menu
    local menu = {}
    for _, form in ipairs(form_specs) do
        if form.profile then
            table.insert(menu, { form.profile, form = form })
        end
    end
    -- show menu if necessary
    if #menu == 0 then
        w:error("no forms with profile names found")
    else
        w:set_mode("formfiller-menu", menu)
    end
end)

webview.add_signal("init", function (view)
    view:add_signal("load-status", function (v, status)
        if status ~= "finished" then return end

        local rules = read_formfiller_rules_from_file()
        local form_specs = form_specs_for_uri(rules, v.uri)
        for _, form_spec in ipairs(form_specs) do
            if type(form_spec.autofill) == "table" and form_spec.autofill.sentinel then
                form_spec.autofill = dsl_extensions[form_spec.autofill.key](unpack(form_spec.autofill.arg))
            end
            if form_spec.autofill then
                -- Precaution: pattern must contain full domain of page URI
                local uri = lousy.uri.parse(v.uri)
                local domain = uri.host
                if uri.port ~= 80 and uri.port ~= 443 then
                    domain = domain .. ":" .. uri.port
                end
                domain = lousy.util.lua_escape(domain .. "/")
                if form_spec.pattern:find(domain, 1, true) then
                    msg.info("auto-filling form profile '%s'", form_spec.profile)
                    formfiller_wm:emit_signal(view, "apply_form", form_spec)
                else
                    local w = webview.window(view)
                    w:error("refusing to autofill: URI pattern does not contain current page domain")
                end
            end
        end
    end)
end)

-- Add formfiller menu mode
new_mode("formfiller-menu", {
    enter = function (w, menu)
        local rows = {{ "Profile", title = true }}
        for _, m in ipairs(menu) do
            table.insert(rows, m)
        end
        w.menu:build(rows)
    end,

    leave = function (w)
        w.menu:hide()
    end,
})

add_binds("formfiller-menu", lousy.util.table.join({
    -- use profile
    { "<Return>", "Select formfiller profile.",
        function (w)
            local row = w.menu:get()
            local form = row.form
            w:set_mode()
            formfiller_wm:emit_signal(w.view, "apply_form", form)
        end },
}, menu_binds))

-- Visual form selection for adding a form
new_mode("formfiller-add", {
    enter = function (w)
        w:set_prompt("Add form:")
        w:set_input("")
        w:set_ibar_theme()

        formfiller_wm:emit_signal(w.view, "enter")
    end,

    changed = function (w, text)
        formfiller_wm:emit_signal(w.view, "changed", text)
    end,

    leave = function (w)
        w:set_ibar_theme()
        formfiller_wm:emit_signal(w.view, "leave")
    end,
})
add_binds("formfiller-add", {
    { "<Tab>",    "Focus the next form hint.",
        function (w) formfiller_wm:emit_signal(w.view, "focus",  1) end },
    { "<Shift-Tab>",    "Focus the previous form hint.",
        function (w) formfiller_wm:emit_signal(w.view, "focus", -1) end },
    { "<Return>", "Add the currently focused form to the formfiller file.",
        function (w) formfiller_wm:emit_signal(w.view, "select") end },
})

-- Setup formfiller binds
add_binds("normal", {
    { "za", "Add formfiller form.",
       function (w) w:set_mode("formfiller-add") end },
    { "ze", "Edit formfiller forms for current domain.",
       function (_) edit() end },
    { "zl", "Load formfiller form (use first profile).",
       fill_form_fast },
    { "zL", "Load formfiller form.",
        fill_form_menu },
})

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
