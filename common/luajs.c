/*
 * Copyright © 2016 Aidan Holm <aidanholm@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/luajs.h"

/*
 * Converts Lua value referenced by idx to the corresponding JavaScript type.
 * Returns NULL on failure.
 */
JSCValue *luajs_tovalue(lua_State *L, int idx, JSCContext *ctx)
{
    switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN:
            return jsc_value_new_boolean(ctx, lua_toboolean(L, idx));
        case LUA_TNUMBER:
            return jsc_value_new_number(ctx, lua_tonumber(L, idx));
        case LUA_TNIL:
            return jsc_value_new_null(ctx);
        case LUA_TNONE:
            return jsc_value_new_undefined(ctx);
        case LUA_TSTRING:
            return jsc_value_new_string(ctx, lua_tostring(L, idx));
        case LUA_TTABLE:
            ;
            size_t len = lua_objlen(L, idx);
            int top = lua_gettop(L);
            JSCValue *res, *val;

            if (idx < 0)
                idx += top + 1;

            if (len) {
                res = jsc_value_new_array(ctx, G_TYPE_NONE);
                lua_pushnil(L);
                int i = 0;
                while (lua_next(L, idx)) {
                    val = luajs_tovalue(L, -1, ctx);
                    if (!val) {
                        lua_settop(L, top);
                        g_object_unref(res);
                        return NULL;
                    }
                    jsc_value_object_set_property_at_index(res, i++, val);
                    lua_pop(L, 1);
                    g_object_unref(val);
                }
            } else {
                res = jsc_value_new_object(ctx, NULL, NULL);
                lua_pushnil(L);
                while (lua_next(L, idx)) {
                    if (lua_type(L, -2) != LUA_TSTRING)
                        continue;
                    val = luajs_tovalue(L, -1, ctx);
                    if (!val) {
                        lua_settop(L, top);
                        g_object_unref(res);
                        return NULL;
                    }
                    jsc_value_object_set_property(res, lua_tostring(L, -2), val);
                    lua_pop(L, 1);
                    g_object_unref(val);
                }
            }
            return res;
    }
    return NULL;
}

/*
 * Converts JS value to the corresponding Lua type and pushes the result onto
 * the Lua stack. Returns the number of pushed values, 0 thus signals error.
 */
int luajs_pushvalue(lua_State *L, JSCValue *value)
{
    if (jsc_value_is_undefined(value) || jsc_value_is_null(value))
        lua_pushnil(L);
    else if (jsc_value_is_boolean(value))
        lua_pushboolean(L, jsc_value_to_boolean(value));
    else if (jsc_value_is_number(value))
        lua_pushnumber(L, jsc_value_to_double(value));
    else if (jsc_value_is_string(value)) {
        char *str = jsc_value_to_string(value);
        lua_pushstring(L, str);
        free(str);
    } else if (jsc_value_is_object(value)) {
        char **keys = jsc_value_object_enumerate_properties(value);
        int top = lua_gettop(L);
        JSCValue *val;
        char *eptr;
        char *key;
        int i = 0;
        long n;

        lua_newtable(L);
        while (keys && (key = keys[i++])) {
            if (*key && (n = strtol(key, &eptr, 10), !*eptr))
                lua_pushinteger(L, ++n);
            else
                lua_pushstring(L, key);

            val = jsc_value_object_get_property(value, key);
            if (!luajs_pushvalue(L, val)) {
                g_object_unref(val);
                lua_settop(L, top);
                g_strfreev(keys);
                return 0;
            }
            g_object_unref(val);
            lua_rawset(L, -3);
        }

        g_strfreev(keys);
    } else
        return 0;
    return 1;
}

/*
 * Executes the given JS code in the provided context and pushes the last
 * generated value onto the Lua stack, unless no_return is set. Code must be a
 * NUL-terminated string. If error occurs, pushes a nil value and an error
 * string instead. source and line are only used in the JS execution error
 * message to specify its origin, they do not affect the execution itself.
 * Returns the number of values pushed onto the Lua stack.
 */
int luajs_eval_js(lua_State *L, JSCContext *ctx, const char *code, const char *source, guint line, bool no_return)
{
    JSCValue *result = jsc_context_evaluate_with_source_uri(ctx, code, -1, source, line);

    JSCException *exception = jsc_context_get_exception(ctx);
    if (exception) {
        char *e = jsc_exception_to_string(exception);
        lua_pushnil(L);
        lua_pushstring(L, e);
        free(e);
        return 2;
    }

    if (no_return)
        return 0;

    int ret = luajs_pushvalue(L, result);
    g_object_unref(result);
    if (!ret) {
        lua_pushnil(L);
        lua_pushstring(L, "unable to push the result onto the Lua stack");
        return 2;
    }
    return ret;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
