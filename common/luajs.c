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

gchar*
tostring(JSContextRef context, JSValueRef value, gchar **error)
{
    JSStringRef str = JSValueToStringCopy(context, value, NULL);
    if (!str) {
        if (error)
            *error = g_strdup("string conversion failed");
        return NULL;
    }
    size_t size = JSStringGetMaximumUTF8CStringSize(str);
    gchar *ret = g_malloc(sizeof(gchar)*size);
    JSStringGetUTF8CString(str, ret, size);
    JSStringRelease(str);
    return ret;
}

gint
luaJS_pushstring(lua_State *L, JSContextRef context, JSValueRef value, gchar **error)
{
    gchar *str = tostring(context, value, error);
    if (str) {
        lua_pushstring(L, str);
        g_free(str);
        return 1;
    }
    return 0;
}

gint
luaJS_pushobject(lua_State *L, JSContextRef context, JSObjectRef obj, gchar **error)
{
    gint top = lua_gettop(L);

    JSPropertyNameArrayRef keys = JSObjectCopyPropertyNames(context, obj);
    size_t count = JSPropertyNameArrayGetCount(keys);
    JSValueRef exception = NULL;

    lua_newtable(L);

    for (size_t i = 0; i < count; i++) {
        /* push table key onto stack */
        JSStringRef key = JSPropertyNameArrayGetNameAtIndex(keys, i);
        size_t slen = JSStringGetMaximumUTF8CStringSize(key);
        gchar cstr[slen];
        JSStringGetUTF8CString(key, cstr, slen);

        gchar *eptr = NULL;
        int n = strtol(cstr, &eptr, 10);
        if (!*eptr) /* end at '\0' ? == it's a number! */
            lua_pushinteger(L, ++n); /* 0-index array to 1-index array */
        else
            lua_pushstring(L, cstr);

        /* push table value into stack */
        JSValueRef val = JSObjectGetProperty(context, obj, key, &exception);
        if (exception) {
            lua_settop(L, top);
            if (error) {
                gchar *err = tostring(context, exception, NULL);
                *error = g_strdup_printf("JSObjectGetProperty call failed (%s)",
                        err ? err : "unknown reason");
                g_free(err);
            }
            JSPropertyNameArrayRelease(keys);
            return 0;
        }
        luaJS_pushvalue(L, context, val, error);
        if (error && *error) {
            lua_settop(L, top);
            JSPropertyNameArrayRelease(keys);
            return 0;
        }
        lua_rawset(L, -3);
    }
    JSPropertyNameArrayRelease(keys);
    return 1;
}

/* Push JavaScript value onto Lua stack */
gint
luaJS_pushvalue(lua_State *L, JSContextRef context, JSValueRef value, gchar **error)
{
    switch (JSValueGetType(context, value)) {
      case kJSTypeBoolean:
        lua_pushboolean(L, JSValueToBoolean(context, value));
        return 1;

      case kJSTypeNumber:
        lua_pushnumber(L, JSValueToNumber(context, value, NULL));
        return 1;

      case kJSTypeString:
        return luaJS_pushstring(L, context, value, error);

      case kJSTypeObject:
        return luaJS_pushobject(L, context, (JSObjectRef)value, error);

      case kJSTypeUndefined:
      case kJSTypeNull:
        lua_pushnil(L);
        return 1;

      default:
        break;
    }
    if (error)
        *error = g_strdup("Unable to convert value into equivalent Lua type");
    return 0;
}

JSValueRef
luaJS_fromtable(lua_State *L, JSContextRef context, gint idx, gchar **error)
{
    gint top = lua_gettop(L);

    /* convert relative index into abs */
    if (idx < 0)
        idx = top + idx + 1;

    JSValueRef exception = NULL;
    JSObjectRef obj;

    size_t len = lua_objlen(L, idx);
    if (len) {
        obj = JSObjectMakeArray(context, 0, NULL, &exception);
        if (exception) {
            if (error) {
                gchar *err = tostring(context, exception, NULL);
                *error = g_strdup_printf("JSObjectMakeArray call failed (%s)",
                        err ? err : "unknown reason");
                g_free(err);
            }
            return NULL;
        }

        lua_pushnil(L);
        for (guint i = 0; lua_next(L, idx); i++) {
            JSValueRef val = luaJS_tovalue(L, context, -1, error);
            if (error && *error) {
                lua_settop(L, top);
                return NULL;
            }
            lua_pop(L, 1);
            JSObjectSetPropertyAtIndex(context, obj, i, val, &exception);
        }
    } else {
        obj = JSObjectMake(context, NULL, NULL);
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            /* We only care about string attributes in the table */
            if (lua_type(L, -2) == LUA_TSTRING) {
                JSValueRef val = luaJS_tovalue(L, context, -1, error);
                if (error && *error) {
                    lua_settop(L, top);
                    return NULL;
                }
                JSStringRef key = JSStringCreateWithUTF8CString(lua_tostring(L, -2));
                JSObjectSetProperty(context, obj, key, val,
                        kJSPropertyAttributeNone, &exception);
                JSStringRelease(key);
                if (exception) {
                    if (error) {
                        gchar *err = tostring(context, exception, NULL);
                        *error = g_strdup_printf("JSObjectSetProperty call failed (%s)",
                                err ? err : "unknown reason");
                        g_free(err);
                    }
                    return NULL;
                }
            }
            lua_pop(L, 1);
        }
    }
    return obj;
}

/* Make JavaScript value from Lua value */
JSValueRef
luaJS_tovalue(lua_State *L, JSContextRef context, gint idx, gchar **error)
{
    JSStringRef str;
    JSValueRef ret;

    switch (lua_type(L, idx)) {
      case LUA_TBOOLEAN:
        return JSValueMakeBoolean(context, lua_toboolean(L, idx));

      case LUA_TNUMBER:
        return JSValueMakeNumber(context, lua_tonumber(L, idx));

      case LUA_TNIL:
        return JSValueMakeNull(context);

      case LUA_TNONE:
        return JSValueMakeUndefined(context);

      case LUA_TSTRING:
        str = JSStringCreateWithUTF8CString(lua_tostring(L, idx));
        ret = JSValueMakeString(context, str);
        JSStringRelease(str);
        return ret;

      case LUA_TTABLE:
        return luaJS_fromtable(L, context, idx, error);

      default:
        break;
    }

    if (error)
        *error = g_strdup_printf("unhandled Lua->JS type conversion (type %s)",
                lua_typename(L, lua_type(L, idx)));
    return JSValueMakeUndefined(context);
}

/* create JavaScript exception object from string */
JSValueRef
luaJS_make_exception(JSContextRef context, const gchar *error)
{
    JSStringRef estr = JSStringCreateWithUTF8CString(error);
    JSValueRef exception = JSValueMakeString(context, estr);
    JSStringRelease(estr);
    return JSValueToObject(context, exception, NULL);
}

/*
 * Converts JS value to the corresponding Lua type and pushes the result onto
 * the Lua stack. Returns the number of pushed values, 0 thus signals error.
 */
static int luajs_pushvalue(lua_State *L, JSCValue *value)
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
