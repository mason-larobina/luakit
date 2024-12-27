--- Web page scroll position - status bar widget.
--
-- Shows the current scroll position of the web page as a percentage.
--
-- @module lousy.widget.scroll
-- @copyright 2017 Aidan Holm <aidanholm@gmail.com>
-- @copyright 2010 Mason Larobina <mason.larobina@gmail.com>

local webview = require("webview")
local lousy = require("lousy")
local theme = lousy.theme.get()
local wc = require("lousy.widget.common")

local _M = {}

local widgets = {
    update = function (w, label)
        w.view:eval_js([=[
            (function () {
                var y = window.scrollY;
                var max = Math.max(window.document.documentElement.scrollHeight - window.innerHeight, 0);
                return y + " " + max;
            })()
        ]=], { callback = function (scroll, err)
            assert(not err, err)
            local y, max = scroll:match("^(%S+) (%S+)$")
            y, max = tonumber(y), tonumber(max)
            local text
            if     max == 0   then text = "All"
            elseif y   <= 2   then text = "Top"
            elseif y   >= (max - 2) then text = "Bot"
            else text = string.format("%2d%%", (y / max) * 100)
            end
            if label.text ~= text then label.text = text end
        end })
    end,
}

webview.add_signal("init", function (view)
    view:add_signal("expose", function (v)
        local w = webview.window(v)
        if w.view == v then
            wc.update_widgets_on_w(widgets, w)
        end
    end)
    view:add_signal("switched-page", function (v)
        wc.update_widgets_on_w(widgets, webview.window(v))
    end)
end)

local function new()
    local scroll = widget{type="label"}
    scroll.fg = theme.scroll_sbar_fg
    scroll.font = theme.scroll_sbar_font
    return wc.add_widget(widgets, scroll)
end

return setmetatable(_M, { __call = function(_, ...) return new(...) end })

-- vim: et:sw=4:ts=8:sts=4:tw=80
