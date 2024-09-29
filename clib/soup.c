/*
 * clib/soup.c - soup library
 *
 * Copyright © 2011 Mason Larobina <mason.larobina@gmail.com>
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

#include "luah.h"
#include "clib/soup.h"
#include "common/property.h"
#include "common/signal.h"
#include "web_context.h"

#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <webkit2/webkit2.h>

static lua_class_t soup_class;

static gchar *proxy_uri;
static gchar *accept_policy;
static gchar *cookies_storage;

LUA_CLASS_FUNCS(soup, soup_class);

#include "common/clib/soup.h"

static gint
luaH_soup_index(lua_State *L)
{
    const gchar *prop = luaL_checkstring(L, 2);
    luakit_token_t token = l_tokenize(prop);

    switch (token) {
        PS_CASE(PROXY_URI, proxy_uri)
        PS_CASE(ACCEPT_POLICY, accept_policy)
        PS_CASE(COOKIES_STORAGE, cookies_storage)
        default:
            break;
    }
    return 0;
}

static void
luaH_soup_set_proxy_uri(lua_State *L)
{
    WebKitWebContext *ctx = web_context_get();
    WebKitWebsiteDataManager *dm = webkit_web_context_get_website_data_manager(ctx);
    const gchar *new_proxy_uri = lua_isnil(L, 3) ? "default" : luaL_checkstring(L, 3);
    g_free(proxy_uri);
    proxy_uri = g_strdup(new_proxy_uri);

    if (!proxy_uri || g_str_equal(proxy_uri, "default")) {
        webkit_website_data_manager_set_network_proxy_settings(dm, WEBKIT_NETWORK_PROXY_MODE_DEFAULT, NULL);
    } else if (g_str_equal(proxy_uri, "no_proxy")) {
        webkit_website_data_manager_set_network_proxy_settings(dm, WEBKIT_NETWORK_PROXY_MODE_NO_PROXY, NULL);
    } else {
        WebKitNetworkProxySettings *proxy_settings = webkit_network_proxy_settings_new(proxy_uri, NULL);
        webkit_website_data_manager_set_network_proxy_settings(dm, WEBKIT_NETWORK_PROXY_MODE_CUSTOM, proxy_settings);
        webkit_network_proxy_settings_free(proxy_settings);
    }
}

static void
luaH_soup_set_accept_policy(lua_State *L)
{
    const gchar *new_policy = luaL_checkstring(L, 3);
    if (!g_str_equal(new_policy, "always"))
    if (!g_str_equal(new_policy, "never"))
    if (!g_str_equal(new_policy, "no_third_party"))
        luaL_error(L, "accept_policy must be one of 'always', 'never', 'no_third_party'");
    g_free(accept_policy);
    accept_policy = g_strdup(new_policy);

    WebKitWebContext * web_context = web_context_get();
    WebKitCookieManager *cookie_mgr = webkit_web_context_get_cookie_manager(web_context);
    WebKitCookieAcceptPolicy policy;
    if (g_str_equal(new_policy, "always"))
        policy = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
    else if (g_str_equal(new_policy, "never"))
        policy = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
    else if (g_str_equal(new_policy, "no_third_party"))
        policy = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
    else g_assert_not_reached();
    webkit_cookie_manager_set_accept_policy(cookie_mgr, policy);
}

static void
luaH_soup_set_cookies_storage(lua_State *L)
{
    const gchar *new_path = luaL_checkstring(L, 3);
    FILE *f;
    if (g_str_equal(new_path, ""))
        luaL_error(L, "cookies_storage cannot be empty");
    g_free(cookies_storage);
    cookies_storage = g_strdup(new_path);

    if ((f = g_fopen(cookies_storage, "a")) != NULL) {
        g_chmod(cookies_storage, 0600);
        fclose(f);
    }

    WebKitWebContext * web_context = web_context_get();
    WebKitCookieManager *cookie_mgr = webkit_web_context_get_cookie_manager(web_context);
    webkit_cookie_manager_set_persistent_storage(cookie_mgr, cookies_storage,
            WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
}

static gint
luaH_soup_newindex(lua_State *L)
{
    const gchar *prop = luaL_checkstring(L, 2);
    luakit_token_t token = l_tokenize(prop);

    switch (token) {
        case L_TK_PROXY_URI:
            luaH_soup_set_proxy_uri(L);
            break;
        case L_TK_ACCEPT_POLICY:
            luaH_soup_set_accept_policy(L);
            break;
        case L_TK_COOKIES_STORAGE:
            luaH_soup_set_cookies_storage(L);
            break;
        default:
            return 0;
    }
    return 0;
}

void
soup_lib_setup(lua_State *L)
{
    soup_lib_setup_common();

    static const struct luaL_Reg soup_lib[] =
    {
        LUA_CLASS_METHODS(soup)
        { "__index",       luaH_soup_index },
        { "__newindex",    luaH_soup_newindex },
        { "parse_uri",     luaH_soup_parse_uri },
        { "uri_tostring",  luaH_soup_uri_tostring },
        { NULL,            NULL },
    };

    /* create signals array */
    soup_class.signals = signal_new();

    /* export soup lib */
    luaH_openlib(L, "soup", soup_lib, soup_lib);

    /* Initial settings */
    proxy_uri = g_strdup("default");
    accept_policy = g_strdup("no_third_party"); /* Must match default set in web_context.c */
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
