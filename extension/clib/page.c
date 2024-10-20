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

#include "extension/extension.h"
#include "extension/clib/page.h"
#include "extension/clib/dom_document.h"
#include "extension/clib/dom_element.h"
#include "common/tokenize.h"
#include "common/luautil.h"
#include "common/luauniq.h"
#include "common/luajs.h"
#include "luah.h"

#define REG_KEY "luakit.uniq.registry.page"

static lua_class_t page_class;

LUA_OBJECT_FUNCS(page_class, page_t, page);

static page_t*
luaH_check_page(lua_State *L, gint udx)
{
    page_t *page = luaH_checkudata(L, udx, &page_class);
    if (!page->page || !WEBKIT_IS_WEB_PAGE(page->page))
        luaL_argerror(L, udx, "page no longer valid");
    return page;
}

static gboolean
send_request_cb(WebKitWebPage *web_page, WebKitURIRequest *request,
        WebKitURIResponse *UNUSED(redirected_response), page_t *UNUSED(page))
{
    lua_State *L = common.L;
    const gchar *uri = webkit_uri_request_get_uri(request);
    SoupMessageHeaders *hdrs = webkit_uri_request_get_http_headers(request);

    int top = lua_gettop(L);

    /* Build headers table */
    lua_newtable(L);
    if (hdrs) {
        SoupMessageHeadersIter iter;
        soup_message_headers_iter_init(&iter, hdrs);
        const char *name, *value;
        while (soup_message_headers_iter_next(&iter, &name, &value)) {
            lua_pushstring(L, name);
            lua_pushstring(L, value);
            lua_rawset(L, -3);
        }
    }

    luaH_page_from_web_page(L, web_page);
    lua_pushstring(L, uri);
    lua_pushvalue(L, -3);

    gint ret = luaH_object_emit_signal(L, -3, "send-request", 2, 1);

    if (ret) {
        /* First argument: redirect url or false to block */
        if (lua_isstring(L, -1)) /* redirect */
            webkit_uri_request_set_uri(request, lua_tostring(L, -1));
        else { /* block request */
            if (!lua_isboolean(L, -1) || lua_toboolean(L, -1))
                warn(ANSI_COLOR_BLUE "send-request" ANSI_COLOR_RESET " handler returned %s, should be a string or false",
                        lua_typename(L, lua_type(L, -1)));
            lua_settop(L, top);
            return TRUE;
        }
        lua_pop(L, ret);
    }

    lua_pop(L, 1);

    /* Rebuild HTTP headers from headers table */
    if (hdrs) {
        /* Update all values */
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            soup_message_headers_replace(hdrs, luaL_checkstring(L, -2), luaL_checkstring(L, -1));
            lua_pop(L, 1);
        }

        /* Remove table values that were removed */
        SoupMessageHeadersIter iter;
        soup_message_headers_iter_init(&iter, hdrs);
        const char *name, *value;
        while (soup_message_headers_iter_next(&iter, &name, &value)) {
            lua_pushstring(L, name);
            lua_rawget(L, -2);
            if (lua_isnil(L, -1))
                soup_message_headers_remove(hdrs, name);
            lua_pop(L, 1);
        }
    }

    lua_settop(L, top);
    return FALSE;
}

static void
document_loaded_cb(WebKitWebPage *web_page, page_t *UNUSED(page))
{
    lua_State *L = common.L;
    luaH_page_from_web_page(L, web_page);
    luaH_object_emit_signal(L, -1, "document-loaded", 0, 0);
    lua_pop(L, 1);
}

static gint
luaH_page_eval_js(lua_State *L)
{
    page_t *page = luaH_check_page(L, 1);
    const gchar *script = luaL_checkstring(L, 2);
    const gchar *source = NULL;

    gint top = lua_gettop(L);
    if (top >= 3 && !lua_isnil(L, 3)) {
        luaH_checktable(L, 3);
        if (luaH_rawfield(L, 3, "source"))
            source = luaL_checkstring(L, -1);
        lua_settop(L, top);
    }

    source = source ?: luaH_callerinfo(L);

    WebKitFrame *frame = webkit_web_page_get_main_frame(page->page);
    WebKitScriptWorld *world = extension.script_world;
    JSCContext *ctx = webkit_frame_get_js_context_for_script_world(frame, world);
    int ret = luajs_eval_js(common.L, ctx, script, source, 1, false);
    g_object_unref(ctx);
    return ret;
}

static gint
luaH_page_js_func(lua_State *L)
{
    const void *ctx = lua_topointer(L, lua_upvalueindex(1));
    const void *func = lua_topointer(L, lua_upvalueindex(2));
    page_t *page = luaH_check_page(L, lua_upvalueindex(3));

    gint argc = lua_gettop(L);
    JSValueRef *args = argc > 0 ? g_alloca(sizeof(*args)*argc) : NULL;
    for (gint i = 0; i < argc; i++) {
        dom_element_t *elem = luaH_to_dom_element(L, i+1);
        /* Custom handling of dom_element_t objects here because luaJS_tovalue()
         * is defined in common/, which is shared in the main process, and the
         * main process is not aware of the extension/clib/ stuff */
        if (elem)
            args[i] = dom_element_js_ref(page, elem);
        else
            args[i] = luaJS_tovalue(L, ctx, i+1, NULL);
    }

    /* Call the function */
    JSValueRef ret = JSObjectCallAsFunction(ctx, (JSObjectRef)func, NULL, argc, args, NULL);
    luaJS_pushvalue(L, ctx, ret, NULL);
    return 1;
}

static gint
luaH_page_wrap_js(lua_State *L)
{
    page_t *page = luaH_check_page(L, 1);
    const gchar *script = luaL_checkstring(L, 2);
    if (!lua_isnil(L, 3))
        luaH_checktable(L, 3);

    /* Get the page JS context */
    WebKitFrame *frame = webkit_web_page_get_main_frame(page->page);
    WebKitScriptWorld *world = extension.script_world;
    JSGlobalContextRef ctx = webkit_frame_get_javascript_context_for_script_world(frame, world);

    /* Construct argument names array */
    int argc = lua_objlen(L, 3), i = 0;
    JSStringRef *args = argc > 0 ? g_alloca(sizeof(*args)*argc) : NULL;

    /* {}, index, tbl val */
    if (argc > 0) {
        while (lua_pushnumber(L, ++i), lua_rawget(L, -2), !lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TSTRING);
            const char *name = lua_tostring(L, -1);
            args[i-1] = JSStringCreateWithUTF8CString(name);
            lua_pop(L, 1);
        }
    }

    /* Convert script to a JS function */
    JSStringRef body = JSStringCreateWithUTF8CString(script);
    JSObjectRef func = JSObjectMakeFunction(ctx, NULL, argc, args, body, NULL, 1, NULL);

    lua_pushlightuserdata(L, ctx);
    lua_pushlightuserdata(L, func);
    lua_pushvalue(L, 1);
    lua_pushcclosure(L, luaH_page_js_func, 3);

    return 1;
}

static void
webkit_web_page_destroy_cb(page_t *page, GObject *web_page)
{
    lua_State *L = common.L;
    luaH_uniq_get_ptr(L, REG_KEY, web_page);
    luaH_object_emit_signal(L, -1, "destroy", 0, 0);
    lua_pop(L, 1);

    page->page = NULL;
    luaH_uniq_del_ptr(common.L, REG_KEY, web_page);
}

gint
luaH_page_from_web_page(lua_State *L, WebKitWebPage *web_page)
{
    if (!web_page) {
        lua_pushnil(L);
        return 1;
    }

    if (luaH_uniq_get_ptr(L, REG_KEY, web_page))
        return 1;

    page_t *page = page_new(L);
    page->page = web_page;

    g_signal_connect(page->page, "send-request", G_CALLBACK(send_request_cb), page);
    g_signal_connect(page->page, "document-loaded", G_CALLBACK(document_loaded_cb), page);

    luaH_uniq_add_ptr(L, REG_KEY, web_page, -1);
    g_object_weak_ref(G_OBJECT(web_page), (GWeakNotify)webkit_web_page_destroy_cb, page);

    return 1;
}

static int
luaH_page_new(lua_State *L)
{
    guint64 page_id = luaL_checknumber(L, -1);
    WebKitWebPage *page = webkit_web_extension_get_page(extension.ext, page_id);
    return luaH_page_from_web_page(L, page);
}

static gint
luaH_page_push_document(lua_State *L, page_t *page)
{
    WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->page);
    return luaH_dom_document_from_webkit_dom_document(L, doc);
}

static gint
luaH_page_index(lua_State *L)
{
    const char *prop = luaL_checkstring(L, 2);

    if(luaH_usemetatable(L, 1, 2))
        return 1;

    page_t *page = luaH_check_page(L, 1);
    luakit_token_t token = l_tokenize(prop);

    switch(token) {
        PS_CASE(URI, webkit_web_page_get_uri(page->page));
        PI_CASE(ID, webkit_web_page_get_id(page->page));
        PF_CASE(EVAL_JS, luaH_page_eval_js)
        PF_CASE(WRAP_JS, luaH_page_wrap_js)
        case L_TK_DOCUMENT:
            return luaH_page_push_document(L, page);
        default:
            return 0;
    }
}

void
page_class_setup(lua_State *L)
{
    static const struct luaL_Reg page_methods[] =
    {
        LUA_CLASS_METHODS(page)
        { "__call", luaH_page_new },
        { NULL, NULL }
    };

    static const struct luaL_Reg page_meta[] =
    {
        LUA_OBJECT_META(page)
        { "__index", luaH_page_index },
        { "__gc", luaH_object_gc },
        { NULL, NULL }
    };

    luaH_class_setup(L, &page_class, "page",
            (lua_class_allocator_t) page_new,
            NULL, NULL,
            page_methods, page_meta);

    luaH_uniq_setup(L, REG_KEY, "");
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
