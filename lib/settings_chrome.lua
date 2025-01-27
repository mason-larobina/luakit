--- Luakit settings viewer.
--
-- This module supplies the <luakit://settings/> chrome page, which shows all
-- settings and their values, and allows adjusting setting values.
--
-- @module settings_chrome
-- @author Aidan Holm
-- @copyright 2017 Aidan Holm <aidanholm@gmail.com>

local chrome = require "chrome"
local modes = require "modes"
local settings = require "settings"
local markdown = require "markdown"
local lousy = require "lousy"

local _M = {}

--- HTML template for luakit settings chrome page content.
-- @type string
-- @readwrite
_M.html_template = [==[
    <html>
    <head>
        <title>Luakit settings</title>
        <style type="text/css">
            {style}
        </style>
    </head>
    <body>
        <header id="page-header">
            <h1>Luakit settings</h1>
        </header>
        <div class="content-margin">
            <table id=settings><tbody>{content}</tbody></table>
        </div>
    </body>
    <script>
        {script}
    </script>
    </html>
]==]

local settings_chrome_JS = [=[
    function on_change (event) {
        let i = event.target;
        if (!i.matches(".setting input")) return;
        let root = i.closest(".setting");
        let type = root.dataset.type;
        let key = root.querySelector(".title").innerHTML;
        let value;
        if (type == "boolean")
        {
            value = i.checked;
            let span = root.querySelector(".input > label > span");
            span.dataset.value = value;
            span.innerHTML = value ? "Enabled" : "Disabled";
        } else
            value = i.value;
        set_setting(key, value, type).then(function(error) {
            root.classList.toggle("has-error", error)
            root.querySelector(".error-message").innerHTML = error;
        });
    }
    function on_click (event) {
        let btn = event.target;
        if (!btn.matches("td.tbl_row_actions > a")) return;
        event.preventDefault();
    }
    document.body.addEventListener("input", on_change)
    document.body.addEventListener("change", on_change)
    document.body.addEventListener("click", on_click)
]=]

--- CSS applied to the settings chrome page.
-- @type string
-- @readwrite
_M.html_style = [===[
#settings, table.input {
    width: 100%;
    border-collapse: collapse;
}
.setting > td {
    padding: 0.7rem 1rem;
}
.setting > td.input {
    -webkit-user-select: none;
}
.content-margin {
    padding-left: 0;
    padding-right: 0;
}
.setting {
    padding: 1rem;
}
.setting .title {
    font-family: monospace;
    font-size: 1.3rem;
    margin-bottom: 0.5em;
}
.setting .desc {
    font-size: 0.9rem;
}
.setting input {
    padding: 0.3em;
}

.setting.has-error input {
    border: 1px solid #f34;
    background: #fdd;
}
.setting.has-error .tooltip {
    display: block;
    opacity: 1.0;
}

/* ensures that the gradient connects between <td> */
#settings {
    background: url('data:image/gif;base64,R0lGODlhHAAcAPAAAPb29vr6+iH5BAAAAAAALAAAA \
        AAcABwAAAI+DI6Zwe2vInrUSVnzjblu1VHfElrjUZpn2pwoa7hwvMIuMN+5zt54z0v5bMHSEChD1 \
        oTF0JGZhC6Nzc6zVAAAOw==');
}
.setting:not(.disabled) {
    background: white;
}
.setting:hover {
    background: #f6f6f6;
}
.setting.disabled {
    background: transparent;
}

.tooltip {
    background: #121215;
    color: #f66;
    font-size: 0.8rem;
    line-height: 1;
    padding: 0.6em 0.75rem;
    border-radius: 4px;
    position: absolute;
    display: none;
    opacity: 0;
    transition: opacity 0.2s;
    white-space: nowrap;
    box-shadow: 0 0 0.2rem black;
    z-index: 1000;
    -webkit-backface-visibility: hidden;
    right: 0;
    top: 50%;
    margin-top: -1rem;
    height: 2rem;
}
.tooltip::before {
    content: ' ';
    display: block;
    background: inherit;
    width: 10px;
    height: 10px;
    position: absolute;
    transform: rotate(45deg);

    right: -3px;
    top: 50%;
    margin-top: -5px;
}

.setting label {
    display: block;
}

table.input td {
    font-family: monospace;
    padding: 0.1rem 0.3rem;
}
table.input th {
    padding: 0.3rem;
    font-size: 0.9rem;
    font-weight: normal;
    text-align: left;
    padding-bottom: 0.3rem;
    border-bottom: 1px solid #777;
}

.boolean > input { margin-left: 0; }
.boolean > span { font-weight: bold; }
.boolean > span[data-value=true] { color: #799D6A; }
.boolean > span[data-value=false] { color: #CF6A4C; }
]===]

local function build_settings_entry_table_html(meta)
    local rows = {}
    for k, v in pairs(meta.value) do
        rows[#rows+1] = { key = k, value = v }
    end
    table.sort(rows, function (a, b) return a.key < b.key end)

    local formatter = meta.formatter or function (t, k)
        return {
            key = lousy.util.escape(tostring(k)),
            value = lousy.util.escape(tostring(t[k])),
        }
    end

    local rows_html = ""
    for _, row in ipairs(rows) do
        local row_html = [==[
            <tr>
                <td>{key}</td><td>{value}</td>
            </tr>
        ]==]
        local subs = formatter(meta.value, row.key)
        rows_html = rows_html .. row_html:gsub("{(%w+)}", subs)
    end

    return ([==[
        <table class=input>
            <thead>
                <tr><th>Key</th><th>Value</th></tr>
            </thead>
            <tbody>
                {rows}
            </tbody>
        </table>
    ]==]):gsub("{(%w+)}", { rows = rows_html:gsub("%%","%%%%") } )
end

local build_settings_entry_html = function (meta)
    local settings_entry_fmt = [==[
        <tr class="setting {disabled}" data-type={type}>
            <td style="position: relative;">
                <div class=title>{key}</div>
                <div class=desc>{desc}</div>
                <span class=tooltip><b>Error: </b><span class="error-message"></span></span>
            </td>
            <td class=input>
                {input}
            </td>
        </tr>
    ]==]
    local settings_table_entry_fmt = [==[
        <tr class="setting {disabled}" data-type={type}>
            <td colspan=2 style="position: relative;">
                <div class=title>{key}</div>
                <div class=desc>{desc}</div>
                <span class=tooltip><b>Error: </b><span class="error-message"></span></span>
                {input}
            </td>
        </tr>
    ]==]

    local desc = meta.desc or "No description."
    desc = desc:gsub("^\n*", ""):gsub("[\n ]+$","")
    local fl = #(desc:match("^( +)") or "\n") - 1
    desc = ("\n" .. desc):gsub("\n" .. string.rep(" ", fl), "\n"):sub(2)
    meta.desc = markdown(desc)

    local disabled_attr = (meta.src ~= "persisted" and meta.src ~= "default") and "disabled" or ""

    local input
    if meta.type == "boolean" then
        local fmt = ([==[
            <label class=boolean>
                <input type=checkbox {checked} {disabled} />
                <span data-value={value}>{text}</span>
            </label>
        ]==])
        input = fmt:gsub("{(%w+)}", {
                checked = meta.value and "checked=true" or "",
                text = meta.value and "Enabled" or "Disabled",
                value = meta.value and "true" or "false",
            })
    elseif meta.type == "enum" then
        input = ""
        for k, opt in pairs(meta.options) do
            local tmpl = [==[<label>
            <input type=radio name="{name}" value="{value}" {checked} {disabled} />{label}</label>]==]
            input = input ..  tmpl:gsub("{(%w+)}", {
                    name = meta.key,
                    value = k,
                    label = opt.label or k,
                    checked = (meta.value == k) and "checked=true " or "",
                })
        end
    elseif meta.type:find(":") then
        input = build_settings_entry_table_html(meta)
    else
        input = [==[<input type=text value="{value}" {disabled} />]==]
    end

    local fmt = meta.type:find(":") and settings_table_entry_fmt or settings_entry_fmt
    return fmt:gsub("{input}", input):gsub("{(%w+)}", {
            disabled = disabled_attr,
            type = meta.type,
            key = meta.key,
            desc = meta.desc,
            value = tostring(meta.value),
        })
end

chrome.add("settings", function ()
    local rows, sm = {}, {}
    for k, meta in pairs(settings.get_settings()) do
        meta.key = k
        sm[#sm+1] = meta
    end
    table.sort(sm, function (a, b) return a.key < b.key end)
    for i, meta in ipairs(sm) do
        rows[i] = build_settings_entry_html(meta)
    end

    local html_subs = {
        title = _M.html_page_title,
        style = chrome.stylesheet .. _M.html_style,
        content = table.concat(rows, "\n"),
        script = settings_chrome_JS,
    }
    local html = string.gsub(_M.html_template, "{(%w+)}", html_subs)
    return html
end, nil, {
    set_setting = function (_, key, value, type)
        if type == "number" then
            value = tonumber(value)
            if not value then return "Not a number!" end
        end
        local ok, err = pcall(settings.set_setting, key, value)
        if not ok then
            err = err:gsub("^.-: ", "")
            local range_err = err:match("Value outside accepted range (%[[%d%.]+%]) ")
            if range_err then return "value outside accepted range " .. range_err end
            return err
        end
    end,
})

modes.add_cmds({
    { ":settings", "Open <luakit://settings/> in a new tab.", function (w)
        w:new_tab("luakit://settings/")
    end },
})

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
