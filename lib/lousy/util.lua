--- lousy.util library.
--
--- Utility functions for lousy.
--
-- @module lousy.util
-- @author Mason Larobina <mason.larobina@gmail.com>
-- @author Julien Danjou <julien@danjou.info>
-- @copyright 2010 Mason Larobina <mason.larobina@gmail.com>
-- @copyright 2008 Julien Danjou <julien@danjou.info>

--- Grab environment we need
local rstring = string
local rtable = table
local math = require "math"

local _M = {}

local table = {}
local string = {}

--- @local
_M.table = table

--- @local
_M.string = string

local xml_entity_names = {
    ["'"] = "&apos;", ["\""] = "&quot;", ["<"] = "&lt;", [">"] = "&gt;", ["&"] = "&amp;"
};
local xml_entity_chars = {
    lt = "<", gt = ">", nbsp = " ", quot = "\"", apos = "'", ndash = "-", mdash = "-", amp = "&"
};

--- Escape a string from XML characters.
-- @tparam string text The text to escape.
-- @treturn string A string with all XML characters escaped.
function _M.escape(text)
    return text and text:gsub("['&<>\"]", xml_entity_names) or nil
end

--- Unescape a string from XML entities.
-- @tparam strng text The text to un-escape.
-- @treturn string A string with all the XML entities un-escaped.
function _M.unescape(text)
    return text and text:gsub("&(%a+);", xml_entity_chars) or nil
end

--- Create a directory.
-- @tparam string dir The directory.
-- @treturn number The status code returned by `mkdir`; 0 indicates success.
function _M.mkdir(dir)
    return os.execute(rstring.format("mkdir -p %q",  dir))
end

--- Evaluate Lua code.
-- @tparam string s The string of Lua code to evaluate.
-- @return The return value of Lua code.
function _M.eval(s)
    return assert(loadstring(s))()
end

--- Check if a file is a Lua valid file.
-- This is done by loading the content and compiling it with `loadfile()`.
-- @tparam string path The file path.
-- @treturn function|nil A function if the file was loaded successfully,
-- and a string with the error otherwise.
function _M.checkfile(path)
    local f, e = loadfile(path)
    -- Return function if function, otherwise return error.
    if f then return f end
    return e
end

--- Return the difference of one table against another.
-- @tparam table t The original table.
-- @tparam table other The table to perform the difference against.
-- @return All elements in the first table that are not in the other table.
function table.difference(t, other)
    local ret = {}
    for k, v in pairs(t) do
        if type(k) == "number" then
            local found = false
            for _, ov in ipairs(other) do
                if ov == v then
                    found = true
                    break
                end
            end
            if not found then rtable.insert(ret, v) end
        else
            if not other[k] then ret[k] = v end
        end
    end
    return ret
end

--- Join all tables given as parameters.
-- This will iterate all tables and insert all their keys into a new table.
-- @tparam {table} args A list of tables to join.
-- @treturn table A new table containing all keys from the arguments.
function table.join(...)
    local ret = {}
    for _, t in ipairs({...}) do
        for k, v in pairs(t) do
            if type(k) == "number" then
                rtable.insert(ret, v)
            else
                ret[k] = v
            end
        end
    end
    return ret
end

--- Check if a table has an item and return its key.
-- @tparam table t The table.
-- @param item The item to look for in values of the table.
-- @return The key where the item is found, or `nil` if not found.
function table.hasitem(t, item)
    for k, v in pairs(t) do
        if v == item then
            return k
        end
    end
end

--- Get a sorted table with all integer keys from a table.
-- @tparam table t The table for which the keys to get.
-- @treturn table A table with keys.
function table.keys(t)
    local keys = { }
    for k, _ in pairs(t) do
        rtable.insert(keys, k)
    end
    rtable.sort(keys, function (a, b)
        return type(a) == type(b) and a < b or false
    end)
    return keys
end

--- Reverse a table.
-- @tparam table t The table to reverse.
-- @treturn table The reversed table.
function table.reverse(t)
    local tr = { }
    -- reverse all elements with integer keys
    for _, v in ipairs(t) do
        rtable.insert(tr, 1, v)
    end
    -- add the remaining elements
    for k, v in pairs(t) do
        if type(k) ~= "number" then
            tr[k] = v
        end
    end
    return tr
end

--- Clone a table.
-- @tparam table t The table to clone.
-- @treturn table A clone of `t`.
function table.clone(t)
    local c = { }
    for k, v in pairs(t) do
        c[k] = v
    end
    return c
end

--- Clone table and set metatable.
-- @tparam table t The table to clone.
-- @treturn table A clone of `t` with `t`'s metatable.
function table.copy(t)
    local c = table.clone(t)
    return setmetatable(c, getmetatable(t))
end

--- Check if two tables are identical.
-- @tparam table a The first table.
-- @tparam table b The second table.
-- @treturn boolean `true` if both tables are identical.
function table.isclone(a, b)
    if #a ~= #b then return false end
    for k, _ in pairs(a) do
        if a[k] ~= b[k] then return false end
    end
    return true
end

--- Clone a table with all values as array items.
-- @tparam table t The table to clone.
-- @treturn table All values in `t`.
function table.values(t)
    local ret = {}
    for _, v in pairs(t) do
        rtable.insert(ret, v)
    end
    return ret
end

--- Convert a table to an array by removing all keys that are not sequential numbers.
-- @tparam table t The table to convert.
-- @treturn table A new table with all non-number keys removed.
function table.toarray(t)
    local ret = {}
    for k, v in ipairs(t) do
        ret[k] = v
    end
    return ret
end

--- Filters an array with a predicate function. Element indices are shifted down
-- to fill gaps.
-- @tparam table t The array to filter.
-- @tparam function pred The predicate function: called with (key, value); return
-- `true` to keep element, `false` to remove.
-- @treturn table The filtered array.
function table.filter_array(t, pred)
    local ret = {}
    for i, v in ipairs(t) do
        if pred(i, v) then
            ret[#ret+1] = v
        end
    end
    return ret
end

--- Check if a file exists and is readable.
-- @tparam string f The file path.
-- @treturn boolean `true` if the file exists and is readable.
function os.exists(f)
    assert(type(f) == "string", "invalid path")
    local fh = io.open(f)
    if fh then
        fh:close()
        return f
    end
end

--- Python like string split (source: lua wiki).
-- @tparam string s The string to split.
-- @tparam string pattern The split pattern (I.e. "%s+" to split text by one or more
-- whitespace characters).
-- @tparam[opt] table ret The table to insert the split items in to or a new table if `nil`.
-- @treturn table A table of the string split by the pattern.
function string.split(s, pattern, ret)
    if not pattern then pattern = "%s+" end
    if not ret then ret = {} end
    local pos = 1
    local fstart, fend = rstring.find(s, pattern, pos)
    while fstart do
        rtable.insert(ret, rstring.sub(s, pos, fstart - 1))
        pos = fend + 1
        fstart, fend = rstring.find(s, pattern, pos)
    end
    rtable.insert(ret, rstring.sub(s, pos))
    return ret
end

-- Python like string strip.
-- @tparam string s The string to strip.
-- @tparam string pattern The pattern to strip from the left-most and right-most of the
-- string.
-- @return The inner string segment.
function string.strip(s, pattern)
    local p = pattern or "%s*"
    local sub_start, sub_end

    -- Find start point
    local _, f_end = rstring.find(s, "^"..p)
    if f_end then sub_start = f_end + 1 end

    -- Find end point
    local f_start = rstring.find(s, p.."$")
    if f_start then sub_end = f_start - 1 end

    return rstring.sub(s, sub_start or 1, sub_end or #s)
end

function string.dedent(text, first)
    local min = first and #rstring.match(text, "^(%s*)") or nil
    rstring.gsub(text, "\n(%s*)", function (spaces)
        local len = #spaces
        if not min or len < min then min = len end
    end)
    if min and min > 0 then
        local pat = "\n" .. rstring.rep(" ", min)
        text = rstring.gsub(text, pat, "\n")
    end
    return first and rstring.sub(text, min + 1) or text
end

--- Find glyph backward (used in readline.lua).
-- @tparam string s The string to be searched.
-- @tparam number o The starting offset to search a glyph backward.
-- @treturn number string Offset and glyph if found, otherwise nil.
function string.prev_glyph (s, o)
    if not o or not s or o > s:len() or o < 1 then return nil end
    local glen = 0
    for i = o, 1, -1 do
        if s:sub(i):match("^"..utf8.charpattern) then
            return i - 1, s:sub(i,i+glen)
        else
            glen = glen + 1
        end
    end
    return nil
end

--- Find glyph forward (used in readline.lua).
-- @tparam string s The string to be searched.
-- @tparam number o The starting offset to search a glyph forward.
-- @treturn number string Offset and glyph if found, otherwise nil.
function string.next_glyph (s, o)
    if not o or not s or o > s:len() or o < 1 then return nil end
    local m = s:match(utf8.charpattern, o)
    return o + #m, m
end

local function find_file(paths)
    for _, p in ipairs(paths) do
        if os.exists(p) then return p end
    end
    return error(rstring.format("No such file at: \n\t%s\n", rtable.concat(paths, ",\n\t")))
end

--- Search and return the filepath of a file in the current working directory,
-- the luakit configuration directory, or the system luakit configuration
-- directory.
-- @tparam string f The relative filepath.
-- @treturn string The first valid filepath or an error.
function _M.find_config(f)
    if rstring.match(f, "^/") then return f end
    -- Search locations
    local paths = { "config/"..f, luakit.config_dir.."/"..f, luakit.xdgprefix.."/luakit/"..f }
    for _, path in ipairs(xdg.system_config_dirs) do
        rtable.insert(paths, path.."/luakit/"..f)
    end
    return find_file(paths)
end

--- Search and return the filepath of a file in the current working directory,
-- the luakit data directory, or the luakit installation directory.
-- @tparam string f The relative filepath.
-- @treturn string The first valid filepath or an error.
function _M.find_data(f)
    if rstring.match(f, "^/") then return f end
    -- Search locations
    local paths = { f, luakit.data_dir.."/"..f, luakit.install_paths.install_dir.."/"..f }
    return find_file(paths)
end

--- Search and return the filepath of a file in the current working directory
-- or the luakit cache directory.
-- @tparam string f The relative filepath.
-- @treturn string The first valid filepath or an error.
function _M.find_cache(f)
    -- Ignore absolute paths
    if rstring.match(f, "^/") then return f end
    -- Search locations
    local paths = { luakit.cache_dir.."/"..f }
    return find_file(paths)
end

--- Search for and return the filepath of a file in luakit's resource directories.
-- @tparam string f The relative filepath.
-- @treturn string The first valid filepath or an error.
function _M.find_resource(f)
    -- Ignore absolute paths
    if rstring.match(f, "^/") then return f end
    -- Search locations
    local paths = string.split(luakit.resource_path, ";")
    for i in ipairs(paths) do paths[i] = paths[i]:gsub("/*$", "") .. "/" .. f end
    return find_file(paths)
end

--- Recursively traverse widget tree and return all widgets.
-- @tparam widget wi The widget.
function _M.recursive_remove(wi)
    local ret = {}
    -- Empty other container widgets
    for _, child in ipairs(wi.children or {}) do
        wi:remove(child)
        rtable.insert(ret, child)
        for _, c in ipairs(_M.recursive_remove(child)) do
            rtable.insert(ret, c)
        end
    end
    return ret
end

--- Convert a number to string independent from locale.
-- @tparam number num A number.
-- @tparam number sigs Signifigant figures (if float).
-- @treturn string The string representation of the number.
function _M.ntos(num, sigs)
    local dec = rstring.sub(tostring(num % 1), 3, 2 + (sigs or 4))
    num = tostring(math.floor(num))
    return (#dec == 0 and num) or (num .. "." .. dec)
end

--- Escape values for SQL queries.
-- In sqlite3: "A string constant is formed by enclosing the string in single
-- quotes ('). A single quote within the string can be encoded by putting two
-- single quotes in a row - as in Pascal."
-- Read: <http://sqlite.org/lang_expr.html>.
-- @tparam string s A string.
-- @treturn string The escaped string.
function _M.sql_escape(s)
    return "'" .. rstring.gsub(s or "", "'", "''") .. "'"
end

--- Escape values for lua patterns.
--
-- Escapes the magic characters <code>^$()%.[]*+-?)</code> by prepending a
-- <code>%</code>.
--
-- @tparam string s A string.
-- @treturn string The escaped pattern.
function _M.lua_escape(s)
    return s:gsub("([%^%$%(%)%%%.%[%]%*%+%-%?%)])", "%%%1")
end

local etc_hosts

--- Get all hostnames in `/etc/hosts`.
-- @tparam boolean force Force re-load of `/etc/hosts`.
-- @treturn {string} Table of all hostnames in `/etc/hosts`.
function _M.get_etc_hosts(force)
    -- Unless forced return previous hostnames
    if not force and etc_hosts then
        return etc_hosts
    end
    -- Parse /etc/hosts
    local match, find, gsub = rstring.match, rstring.find, rstring.gsub
    local h = { localhost = "localhost" }
    for line in io.lines("/etc/hosts") do
        if not find(line, "^#") then
            local names = match(line, "^%S+%s+(.+)$")
            gsub(names or "", "(%S+)", function (name)
                h[name] = name -- key add removes duplicates
            end)
        end
    end
    etc_hosts = table.values(h)
    return etc_hosts
end

return _M

-- vim: et:sw=4:ts=8:sts=4:tw=80
