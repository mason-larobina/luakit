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

#include <jsc/jsc.h>
#include <unistd.h>

#define LUAKIT_LUAJS_REGISTRY_KEY "luakit.luajs.registry"

#include "luah.h"
#include "extension/extension.h"
#include "extension/luajs.h"
#include "extension/clib/page.h"
#include "common/ipc.h"
#include "common/lualib.h"
#include "common/luaserialize.h"
#include "common/luajs.h"

typedef struct _luajs_func_ctx_t {
    gpointer ref;
    guint64 page_id;
} luajs_func_ctx_t;

static gint lua_string_find_ref = LUA_REFNIL;

typedef struct _js_promise_t {
    JSCValue *promise;
    JSCValue *resolve;
    JSCValue *reject;
} js_promise_t;

struct cb_data {
    luajs_func_ctx_t *ctx;
    JSCContext *context;
};

static void
promise_executor_cb(JSCValue *resolve, JSCValue *reject, js_promise_t *promise)
{
    g_assert(jsc_value_is_function(resolve));
    g_assert(jsc_value_is_function(reject));

    g_object_ref(resolve);
    g_object_ref(reject);
    promise->resolve = resolve;
    promise->reject = reject;
}

static void
new_promise(JSCContext *context, js_promise_t *promise)
{
    /* Get the Promise() constructor */
    JSCValue *promise_ctor = jsc_context_get_value(context, "Promise");
    g_assert(jsc_value_is_constructor(promise_ctor));

    JSCValue *func = jsc_value_new_function(context, NULL, G_CALLBACK(promise_executor_cb), promise, NULL, G_TYPE_NONE, 2, JSC_TYPE_VALUE, JSC_TYPE_VALUE);
    promise->promise = jsc_value_constructor_call(promise_ctor, JSC_TYPE_VALUE, func, G_TYPE_NONE);

    g_object_unref(func);
    g_object_unref(promise_ctor);
}

static int
luaJS_promise_resolve_reject(lua_State *L)
{
    guint64 page_id = lua_tointeger(L, lua_upvalueindex(1));
    WebKitWebPage *page = webkit_web_extension_get_page(extension.ext, page_id);
    if (!page || !WEBKIT_IS_WEB_PAGE(page))
        return luaL_error(L, "promise no longer valid (associated page closed)");
    JSCContext *context = webkit_frame_get_js_context(
            webkit_web_page_get_main_frame(page));

    js_promise_t *promise = (js_promise_t*)lua_topointer(L, lua_upvalueindex(2));
    JSCValue *cb = lua_toboolean(L, lua_upvalueindex(3)) ? promise->resolve : promise->reject;

    JSCValue *ret = luajs_tovalue(L, 1, context);

    JSCValue *undefined = jsc_value_function_call(cb, JSC_TYPE_VALUE, ret, G_TYPE_NONE);
    g_object_unref(undefined);

    g_object_unref(promise->reject);
    g_object_unref(promise->resolve);
    g_slice_free(js_promise_t, promise);

    g_object_unref(ret);
    g_object_unref(context);
    return 0;
}

static JSCValue *
luaJS_registered_function_callback(GPtrArray *args, struct cb_data *user_data)
{
    lua_State *L = common.L;
    gint top = lua_gettop(L);

    luajs_func_ctx_t *ctx = user_data->ctx;
    JSCContext *context = user_data->context;

    guint argc = args->len;
    JSCValue **argv = (JSCValue **)args->pdata;

    /* Make promise */
    js_promise_t *promise = g_slice_new(js_promise_t);
    new_promise(context, promise);

    luaH_page_from_web_page(L, webkit_web_extension_get_page(extension.ext, ctx->page_id));

    lua_pushinteger(L, ctx->page_id);
    lua_pushlightuserdata(L, promise);
    lua_pushboolean(L, TRUE);
    lua_pushcclosure(L, luaJS_promise_resolve_reject, 3);

    lua_pushinteger(L, ctx->page_id);
    lua_pushlightuserdata(L, promise);
    lua_pushboolean(L, FALSE);
    lua_pushcclosure(L, luaJS_promise_resolve_reject, 3);

    /* push function arguments onto Lua stack */
    for (guint i = 0; i < argc; i++) {
        if (luajs_pushvalue(L, argv[i]))
            continue;

        /* raise JavaScript exception */
        jsc_context_throw_exception(context, jsc_exception_new_printf(context, "bad argument #%d to Lua function", i));
        lua_settop(L, top);
        return jsc_value_new_undefined(context);
    }

    /* TODO: handle callback failure? */
    luaH_object_push(L, ctx->ref);
    luaH_dofunction(L, argc + 3, 0);

    lua_settop(L, top);
    return promise->promise;
}

static void luaJS_registered_function_destroy(void *user_data)
{
    struct cb_data *cb_data = user_data;

    g_object_unref(cb_data->context);
    g_slice_free(luajs_func_ctx_t, cb_data->ctx);
    g_slice_free(struct cb_data, cb_data);
}

void
luaJS_register_function(lua_State *L)
{
    g_assert(lua_isstring(L, -3));
    g_assert(lua_isstring(L, -2));
    g_assert(lua_isfunction(L, -1));

    /* push pattern_table[pattern] */
    lua_pushliteral(L, LUAKIT_LUAJS_REGISTRY_KEY);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, -4);
    lua_rawget(L, -2);

    /* If table[pattern] is nil, set it to an empty table */
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        /* Push key, {}, and set the value */
        lua_pushvalue(L, -4);
        lua_newtable(L);
        lua_rawset(L, -3);
        /* Push key, and get the newly-set value */
        lua_pushvalue(L, -4);
        lua_rawget(L, -2);
    }

    lua_replace(L, -2);

    /* Shift the table down, and set it */
    lua_insert(L, -3);
    lua_rawset(L, -3);
    lua_pop(L, 2);
}

static void register_func(WebKitScriptWorld *world, WebKitWebPage *web_page, WebKitFrame *frame, const gchar *name, gpointer ref)
{
    JSCContext *context = webkit_frame_get_js_context_for_script_world(frame, world);
    luajs_func_ctx_t *ctx = g_slice_new(luajs_func_ctx_t);
    ctx->page_id = webkit_web_page_get_id(web_page);
    ctx->ref = ref;

    struct cb_data *user_data = g_slice_new(struct cb_data);
    user_data->ctx = ctx;
    user_data->context = context;
    JSCValue *fun = jsc_value_new_function_variadic(context, name, G_CALLBACK(luaJS_registered_function_callback), user_data, luaJS_registered_function_destroy, JSC_TYPE_VALUE);

    jsc_context_set_value(context, name, fun);

    g_object_unref(fun);
}

static void
window_object_cleared_cb(WebKitScriptWorld *world, WebKitWebPage *web_page, WebKitFrame *frame, gpointer UNUSED(user_data))
{
    if (!webkit_frame_is_main_frame(frame))
        return;

    lua_State *L = common.L;
    const gchar *uri = webkit_web_page_get_uri(web_page) ?: "about:blank";

    /* Push pattern -> funclist table */
    lua_pushliteral(L, LUAKIT_LUAJS_REGISTRY_KEY);
    lua_rawget(L, LUA_REGISTRYINDEX);

    /* Iterate over all patterns */
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        /* Entries must be string -> function-list */
        g_assert(lua_isstring(L, -2));
        g_assert(lua_istable(L, -1));

        /* Call string.find(uri, pattern) */
        lua_pushstring(L, uri);
        lua_pushvalue(L, -3);
        luaH_dofunction_from_registry(L, lua_string_find_ref, 2, 1);

        if (!lua_isnil(L, -1)) {
            /* got a match: iterate over all functions */
            lua_pushnil(L);
            while (lua_next(L, -3) != 0) {
                /* Entries must be name -> ref */
                g_assert(lua_isstring(L, -2));
                g_assert(lua_isfunction(L, -1));
                gpointer ref = luaH_object_ref(L, -1);
                register_func(world, web_page, frame, lua_tostring(L, -1), ref);
            }
        }

        /* Pop off return code and the function value */
        lua_pop(L, 2);
    }

    /* Pop off table and string.find() */
    lua_pop(L, 1);
}

void
web_luajs_init(void)
{
    g_signal_connect(webkit_script_world_get_default(), "window-object-cleared",
            G_CALLBACK (window_object_cleared_cb), NULL);

    /* Push empty function registration table */
    lua_State *L = common.L;
    lua_pushliteral(L, LUAKIT_LUAJS_REGISTRY_KEY);
    lua_newtable(L);
    lua_rawset(L, LUA_REGISTRYINDEX);

    /* Save reference to string.find() */
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "find");
    luaH_registerfct(L, -1, &lua_string_find_ref);
    lua_pop(L, 2);
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
