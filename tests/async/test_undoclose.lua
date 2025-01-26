--- Basic async test functions.
--
-- @copyright 2017 Aidan Holm <aidanholm@gmail.com>

local T = {}
local test = require("tests.lib")
local assert = require("luassert")
local spy = require("luassert.spy")
local match = require("luassert.match")

uris = {"about:blank"}
require "config.rc"

local window = require "window"
local w = assert(select(2, next(window.bywidget)))

T.test_undo_close_restores_tab_history = function ()
    -- Load page in new tab
    local uri = test.http_server() .. "undoclose_page.html"
    w:new_tab(uri)
    assert.is_equal(w.tabs:current(), 2)
    test.wait_for_view(w.view)

    -- Try to open the menu
    local notify_spy = spy.on(window.methods, "notify")
    w:run_cmd(":undolist")
    assert.spy(notify_spy).was.called_with(match._, "No closed tabs to display")
    assert(w:is_mode("normal"))

    -- Navigate to about:blank
    w.view.uri = "about:blank"
    test.wait_for_view(w.view)

    -- Close
    w:close_tab()

    -- Try to open the menu again
    notify_spy = spy.on(window.methods, "notify")
    w:run_cmd(":undolist")
    assert.spy(notify_spy).was_not_called_with(match._, "No closed tabs to display")
    assert(w:is_mode("undolist"))
    w:set_mode("normal")

    -- Undo Close
    w:undo_close_tab()
    test.wait_for_view(w.view)
    assert.is_equal(w.tabs:current(), 2)
    assert.is_equal(w.view.uri, 'about:blank')

    -- Close again
    w:close_tab()

    -- Undo Close again with argument
    w:undo_close_tab(1)
    test.wait_for_view(w.view)
    assert.is_equal(w.tabs:current(), 2)
    assert.is_equal(w.view.uri, 'about:blank')

    -- Navigate back
    w:back(1)
    test.wait_for_view(w.view)
    assert.is_equal(w.view.uri, uri)

    -- Restore to initial state
    w:close_tab()
    assert(w.tabs:current() == 1)
    assert(w.view.uri == "about:blank")
end

return T

-- vim: et:sw=4:ts=8:sts=4:tw=80
