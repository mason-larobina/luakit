--- Select a page element with a visual interface.
--
-- This web module allows other Lua modules to select page elements with the
-- same interface as that used by the follow mode plugin: a visual overlay is
-- shown that allows the users to type to filter visible hints. For example,
-- this module is used by the `formfiller` module when selecting a form to add.
--
-- @module select_wm
-- @copyright 2017 Aidan Holm <aidanholm@gmail.com>

local ceil, floor, max = math.ceil, math.floor, math.max

local _M = {}

local ui = ipc_channel("select_wm")

local has_client_rects_api = tonumber(luakit.webkit_version:match("^2%.(%d+)%.")) > 16

-- Label making

-- Calculates the minimum number of characters needed in a hint given a
-- charset of a certain length (I.e. the base)
local function max_hint_len(size, base)
    local len = 0
    if base == 1 then return size end
    while size > 0 do size, len = floor(size / base), len + 1 end
    return len
end

-- Reverse a UTF8 string: multibyte sequences are reversed twice
local function utf8_rev (s)
    s = s:gsub(utf8.charpattern, function (ch) return #ch > 1 and ch:reverse() end)
    return s:reverse()
end

local function charset(seq, size)
    local base, digits, labels = utf8.len(seq), {}, {}
    for ch in seq:gmatch(utf8.charpattern) do digits[#digits+1] = ch end

    local maxlen = max_hint_len(size, base)

    for n = 1, size do
        local t, i, j, d = {}, 1, n
        repeat
            d, n = (n % base) + 1, floor(n / base)
            rawset(t, i, rawget(digits, d))
            i = i + 1
        until n == 0

        rawset(labels, j, string.rep(digits[1], maxlen-i+1) .. utf8_rev(table.concat(t, "")))
    end
    return labels
end

-- Different hint label styles
local label_styles = {
    charset = function (seq)
        assert(type(seq) == "string" and #seq > 0, "invalid sequence")
        return function (size) return charset(seq, size) end
    end,

    numbers = function ()
        return function (size) return charset("0123456789", size) end
    end,

    -- Interleave style
    interleave = function (left, right)
        assert(type(left) == "string" and type(right) == "string",
               "left and right parameters must be strings")
        assert(#left > 1 or #right > 1,
               "either left or right parameters' length must be greater than 1")
        local cmap = {}
        for ch in (left..right):gmatch(utf8.charpattern) do
            if cmap[ch] then
                error("duplicate characters %s in hint strings %s, %s", ch, left, right)
            else
                cmap[ch] = 1
            end
        end
        return function (size)
            local function allstrings(n, t, k, s)
                k, s = k or 1, s or {}
                if k > n then
                    coroutine.yield(table.concat(s))
                else
                    for i = 1, #t do
                        s[k] = t[i]
                        allstrings(n, t, k+1, s)
                    end
                end
            end
            local function permute(n, t)
                return coroutine.wrap(allstrings), n, t
            end

            -- calculate the hinting length
            local hint_len = 1
            while true do
                local lo, hi = floor(hint_len/2), ceil(hint_len/2)
                if (#left)^lo * (#right)^hi + (#left)^hi * (#right)^lo >= size then break end
                hint_len = hint_len + 1
            end

            local tleft, tright = {}, {}
            left:gsub(utf8.charpattern, function(c) table.insert(tleft, c) end)
            right:gsub(utf8.charpattern, function(c) table.insert(tright, c) end)
            local labels = {}
            local lo, hi = floor(hint_len/2), ceil(hint_len/2)
            for a in permute(hi, tleft) do
                for b in permute(lo, tright) do
                    rawset(labels, size, a:gsub('()('..utf8.charpattern..')',
                                                function(p, c) return c..b:sub(p, p+#c-1) end))
                    size = size - 1
                    if size == 0 then return labels end
                end
            end
            for a in permute(hi, tright) do
                for b in permute(lo, tleft) do
                    rawset(labels, size, a:gsub('()('..utf8.charpattern..')',
                                                function(p, c) return c..b:sub(p, p+#c-1) end))
                    size = size - 1
                    if size == 0 then return labels end
                end
            end
            return labels
        end
    end,

    -- Chainable style: sorts labels
    sort = function (make_labels)
        return function (size)
            local labels = make_labels(size)
            table.sort(labels)
            return labels
        end
    end,

    -- Chainable style: reverses label strings
    reverse = function (make_labels)
        return function (size)
            local labels = make_labels(size)
            for i = 1, #labels do
                rawset(labels, i, utf8_rev(rawget(labels, i)))
            end
            return labels
        end
    end,

    trim = function (make_labels)
        return function (size)
            local labels = make_labels(size)
            local P = {}
            for _, l in ipairs(labels) do
                local p = l:gsub(utf8.charpattern.."$", "")
                if #p > 0 then P[p] = (P[p] or 0) + 1 end
            end
            for p, count in pairs(P) do
                if count == 1 then
                    for i, l in ipairs(labels) do
                        if l:sub(1, #p) == p then labels[i] = p end
                    end
                end
            end
            return labels
        end
    end,
}

-- Default label style
local label_maker
do
    local s = label_styles
    label_maker = s.trim(s.sort(s.interleave("12345", "67890")))
end

local function bounding_boxes_intersect(a, b)
    if a.x + a.w < b.x then return false end
    if b.x + b.w < a.x then return false end
    if a.y + a.h < b.y then return false end
    if b.y + b.h < a.y then return false end
    return true
end

local function get_element_bb_if_visible(element, wbb, client_rects)
    -- Find the element bounding box
    local r

    if has_client_rects_api and not element.first_child then
        r = element:client_rects()
        for i=#r,1,-1 do
            if r[i].width == 0 or r[i].height == 0 then table.remove(r, i) end
        end
        if #r == 0 then return nil end
        r = r[1]
    else
        r = client_rects(element) or element.rect
    end

    local rbb = {
        x = wbb.x + r.left,
        y = wbb.y + r.top,
        w = r.width,
        h = r.height,
    }

    if rbb.w == 0 or rbb.h == 0 then return nil end

    local style = element.style
    local display = style.display
    local visibility = style.visibility

    if display == 'none' or visibility == 'hidden' then return nil end

    -- Clip bounding box!
    if display == "inline" then
        local parent = element.parent
        local pd = parent.style.display
        if pd == "block" or pd == "inline-block" then
            local w = parent.rect.width
            w = w - (r.left - parent.rect.left)
            if rbb.w > w then rbb.w = w end
        end
    end

    if not bounding_boxes_intersect(wbb, rbb) then return nil end

    -- If a link element contains one image, use the image dimensions
    if element.tag_name == "A" then
        local first = element.first_child
        if first and first.tag_name == "IMG" and not first.next_sibling then
            return get_element_bb_if_visible(first, wbb, client_rects) or rbb
        end
    end

    return rbb
end

local function frame_find_hints(client_rects, frame, elements)
    local hints = {}

    if type(elements) == "string" then
        elements = frame.body:query(elements)
    else
        local elems = {}
        for _, e in ipairs(elements) do
            if e.owner_document == frame.doc then
                elems[#elems + 1] = e
            end
        end
        elements = elems
    end

    -- Find the visible bounding box
    local w = frame.doc.window
    local wbb = {
        x = w.scroll_x,
        y = w.scroll_y,
        w = w.inner_width,
        h = w.inner_height,
    }

    for _, element in ipairs(elements) do
        local rbb = get_element_bb_if_visible(element,wbb, client_rects)

        if rbb then
            local text = ""
            if element.type ~= "password" then
                text = element.text_content
                if text == "" then text = element.value or "" end
            end
            if text == "" then text = element.attr.placeholder or "" end
            hints[#hints+1] = { elem = element, bb = rbb, text = text }
        end
    end

    return hints
end

local function sort_hints_top_left(a, b)
    local dtop = a.bb.y - b.bb.y
    if dtop ~= 0 then
        return dtop < 0
    else
        return a.bb.x - b.bb.x < 0
    end
end

local function make_labels(num)
    return label_maker(num)
end

local function find_frames(root_frame)
    if not root_frame.body then
        return {}
    end

    local subframes = root_frame.body:query("frame, iframe")
    local frames = { root_frame }

    -- For each frame/iframe element, recurse
    for _, frame in ipairs(subframes) do
        local f = { doc = frame.document, body = frame.document.body }
        local s = find_frames(f)
        for _, sf in ipairs(s) do
            frames[#frames + 1] = sf
        end
    end

    return frames
end

local page_states = {}

local function init_frame(frame, stylesheet)
    assert(frame.doc)
    assert(frame.body)

    frame.overlay = frame.doc:create_element("div", { id = "luakit_select_overlay" })
    frame.stylesheet = frame.doc:create_element("style", { id = "luakit_select_stylesheet" }, stylesheet)

    frame.body.parent:append(frame.overlay)
    frame.body.parent:append(frame.stylesheet)
end

local function cleanup_frame(frame)
    if frame.overlay then
        frame.overlay:remove()
        frame.overlay = nil
    end
    if frame.stylesheet then
        frame.stylesheet:remove()
        frame.stylesheet = nil
    end
end

local function hint_matches(hint, hint_pat, text_pat)
    if hint_pat ~= nil and string.find(hint.label, hint_pat) then return true end
    if text_pat ~= nil and string.find(hint.text, text_pat) then return true end
    return false
end

local function filter(state, hint_pat, text_pat)
    state.num_visible_hints = 0
    for _, hint in pairs(state.hints) do
        local old_hidden = hint.hidden
        hint.hidden = not hint_matches(hint, hint_pat, text_pat)

        if not hint.hidden then
            state.num_visible_hints = state.num_visible_hints + 1
        end

        if not old_hidden and hint.hidden then
            -- Save old style, set new style to "display: none"
            hint.overlay_style = hint.overlay_elem.attr.style
            hint.label_style = hint.label_elem.attr.style
            hint.overlay_elem.attr.style = "display: none;"
            hint.label_elem.attr.style = "display: none;"
        elseif old_hidden and not hint.hidden then
            -- Restore saved style
            hint.overlay_elem.attr.style = hint.overlay_style
            hint.label_elem.attr.style = hint.label_style
        end
    end
end

local function focus(state, step)
    local last = state.focused
    local index

    local function sign(n) return n > 0 and 1 or n < 0 and -1 or 0 end

    if state.num_visible_hints == 0 then return end

    -- Advance index to the first non-hidden item
    if step == 0 then
        index = last and last or 1
        while state.hints[index].hidden do
            index = index + 1
            if index > #state.hints then index = 1 end
        end
        if index == last then return end
    end

    -- Which hint to focus?
    if step ~= 0 and last then
        index = last
        while step ~= 0 do
            repeat
                index = index + sign(step)
                if index < 1 then index = #state.hints end
                if index > #state.hints then index = 1 end
            until not state.hints[index].hidden
            step = step - sign(step)
        end
    end

    local new_hint = state.hints[index]

    -- Save and update class for the new hint
    new_hint.orig_class = new_hint.overlay_elem.attr.class
    new_hint.overlay_elem.attr.class = new_hint.orig_class .. " hint_selected"

    -- Restore the original class for the old hint
    if last then
        local old_hint = state.hints[last]
        old_hint.overlay_elem.attr.class = old_hint.orig_class
        old_hint.orig_class = nil
    end

    state.focused = index

    return new_hint
end

--- Enter element selection mode on a web page.
--
-- The web page must not already be in element selection mode.
--
-- @tparam page page The web page in which to enter element selection.
-- @tparam string|{dom_element} elements A selector to filter elements, or an array of elements.
-- @tparam string stylesheet The stylesheet to apply.
-- @tparam boolean ignore_case `true` if text case should be ignored.
-- @treturn {...} Table with data for the currently focused hint.
-- @treturn number The number of currently visible hints.
function _M.enter(page, elements, stylesheet, ignore_case)
    assert(type(page) == "page")
    assert(type(elements) == "string" or type(elements) == "table")
    assert(type(stylesheet) == "string")
    local page_id = page.id
    assert(page_states[page_id] == nil)

    local root = page.document
    local root_frame = { doc = root, body = root.body }

    local state = {}
    page_states[page_id] = state

    state.frames = find_frames(root_frame)
    state.focused = nil
    state.hints = {}
    state.ignore_case = ignore_case or false

    local client_rects = page:wrap_js([=[
        function client_rects(element) {
            var rects = element.getClientRects();
            if (rects.length == 0)
                return undefined;
            var rect = {
                "top": rects[0].top,
                "bottom": rects[0].bottom,
                "left": rects[0].left,
                "right": rects[0].right,
            };
            for (var i = 1; i < rects.length; i++) {
                rect.top = Math.min(rect.top, rects[i].top);
                rect.bottom = Math.max(rect.bottom, rects[i].bottom);
                rect.left = Math.min(rect.left, rects[i].left);
                rect.right = Math.max(rect.right, rects[i].right);
            }
            rect.width = rect.right - rect.left;
            rect.height = rect.bottom - rect.top;
            return rect;
        }
        client_rects;
    ]=])

    -- Find all hints in the viewport
    for _, frame in ipairs(state.frames) do
        -- Set up the frame, and find hints
        init_frame(frame, stylesheet)
        frame.hints = frame_find_hints(client_rects, frame, elements)
        -- Build an array of all hints
        for _, hint in ipairs(frame.hints) do
            state.hints[#state.hints+1] = hint
        end
    end

    -- Sort them by on-screen position, and assign labels
    local labels = make_labels(#state.hints)
    assert(#state.hints == #labels)

    table.sort(state.hints, sort_hints_top_left)

    for i, hint in ipairs(state.hints) do
        hint.label = labels[i]
    end

    for _, frame in ipairs(state.frames) do
        local fwr = frame.doc.window
        local fsx, fsy = fwr.scroll_x, fwr.scroll_y
        for _, hint in ipairs(frame.hints) do
            -- Append hint elements to overlay
            local e = hint.elem
            local r = hint.bb

            local overlay_style = string.format("left: %dpx; top: %dpx; width: %dpx; height: %dpx;", r.x, r.y, r.w, r.h)
            local label_style = string.format("left: %dpx; top: %dpx;", max(r.x-10, fsx), max(r.y-10, fsy), r.w, r.h)

            local overlay_class = "hint_overlay hint_overlay_" .. e.tag_name
            local label_class = "hint_label hint_label_" .. e.tag_name
            hint.overlay_elem = frame.doc:create_element("span", {class = overlay_class, style = overlay_style})
            hint.label_elem = frame.doc:create_element("span", {class = label_class, style = label_style}, hint.label)

            frame.overlay:append(hint.overlay_elem)
            frame.overlay:append(hint.label_elem)
        end
    end

    for _, frame in ipairs(state.frames) do
        frame.doc:add_signal("destroy", function ()
            cleanup_frame(frame)
        end)
    end

    filter(state, "", "")
    return focus(state, 0), state.num_visible_hints
end

--- Leave element selection mode on a web page.
--
-- The web page must be in element selection mode.
--
-- @tparam page|number page The web page (or the web page id) in which to
-- leave element selection.
function _M.leave(page)
    if type(page) == "page" then page = page.id end
    assert(type(page) == "number")

    local state = page_states[page]
    if not state then return end
    for _, frame in ipairs(state.frames) do
        cleanup_frame(frame)
    end
    page_states[page] = nil
end

--- Update the element selection interface when user selection text changes.
--
-- The web page must be in element selection mode.
--
-- @tparam page page The web page.
-- @tparam string hint_pat The hint pattern filter.
-- @tparam string text_pat The text pattern filter.
-- @tparam string text The full text.
-- @treturn table The currently focused hint.
-- @treturn number The number of currently visible hints.
function _M.changed(page, hint_pat, text_pat, text)
    assert(type(page) == "page")
    assert(hint_pat == nil or type(hint_pat) == "string")
    assert(text_pat == nil or type(text_pat) == "string")
    assert(type(text) == "string")

    local state = assert(page_states[page.id])

    if state.ignore_case then
        local convert = function(pat)
            if pat == nil then return nil end
            local converter = function (ch) return '[' .. string.upper(ch) .. string.lower(ch) .. ']' end
            return string.gsub(pat, '(%a)', converter)
        end
        hint_pat = convert(hint_pat)
        text_pat = convert(text_pat)
    end

    filter(state, hint_pat, text_pat)
    return focus(state, 0), state.num_visible_hints
end

--- Update the element selection interface when the user moves the focus.
--
-- The web page must be in element selection mode.
--
-- @usage
--
--     function handle_next (page)
--         select_wm.focus(page, 1)
--     end
--     function handle_prev (page)
--         select_wm.focus(page, -1)
--     end
--
-- @tparam page page The web page.
-- @tparam number step Relative number of tags to shift focus by.
-- @treturn table The currently focused hint.
-- @treturn number The number of currently visible hints.
function _M.focus(page, step)
    assert(type(page) == "page")
    assert(type(step) == "number")
    local state = assert(page_states[page.id])
    return focus(state, step), state.num_visible_hints
end

--- Get the current state of element hints on a web page.
--
-- The web page must be in element selection mode.
--
-- @tparam page page The web page.
-- @treturn table The current hint state for `page`.
function _M.hints(page)
    assert(type(page) == "page")
    local state = assert(page_states[page.id])
    return state.hints
end

--- Get the currently focused element hint on a web page.
--
-- The web page must be in element selection mode.
--
-- @tparam page page The web page.
-- @treturn table The currently focused hint.
function _M.focused_hint(page)
    assert(type(page) == "page")
    local state = assert(page_states[page.id])
    return state.hints[state.focused]
end

ui:add_signal("set_label_maker", function (_, _, f)
    setfenv(f, label_styles)
    label_maker = f(label_styles)
end)

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
