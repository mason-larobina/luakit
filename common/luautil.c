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

#include <lauxlib.h>
#include <lualib.h>
#include <string.h>

#include "common/luautil.h"
#include "common/lualib.h"
#include "common/log.h"
#include "buildopts.h"

gint
luaH_traceback(lua_State *L, lua_State *T, gint min_level)
{
    lua_Debug ar;
    gint max_level;
    gint loc_pad = 0;

#define AR_SRC(ar) \
    (g_strstr_len((ar).source, 3, "@./") ? (ar).source+3 : \
     (ar).source[0] == '@'               ? (ar).source+1 : \
     (ar).short_src)
#define LENF(fmt, ...) \
    (snprintf(NULL, 0, fmt, ##__VA_ARGS__))

    if (!lua_getstack(T, min_level, &ar)) {
        lua_pushliteral(L, "");
        return 1;
    }

    /* Traverse the stack to determine max level and padding sizes */
    for (gint level = min_level; lua_getstack(T, level, &ar); level++) {
        lua_getinfo(T, "Sl", &ar);

        max_level = level;

        gint cur_pad = LENF("%s:%d", AR_SRC(ar), ar.currentline);
        if (cur_pad > loc_pad) loc_pad = cur_pad;
    }

    GString *tb = g_string_new("");
    gint level_pad = LENF("%d", max_level);

    for (gint level = min_level; level <= max_level; level++) {
        lua_getstack(T, level, &ar);
        lua_getinfo(T, "Sln", &ar);

        /* Current stack level */
        gint shown_level = level - min_level + 1;
        g_string_append_printf(tb, ANSI_COLOR_GRAY "(%*d)" ANSI_COLOR_RESET " ",
                level_pad, shown_level);

        /* Current location, padded */
        if (g_str_equal(ar.what, "C")) {
            g_string_append_printf(tb, "%-*s", loc_pad, "[C]");
        } else {
            const char *src = AR_SRC(ar);
            int n;
            char cl[8] = "";
            snprintf(cl, sizeof(cl), "%d", ar.currentline);
            n = strlen(src) + strlen(cl) + 1;
            g_string_append_printf(tb, "%s:%d", src, ar.currentline);
            g_string_append_printf(tb, "%*.*s", loc_pad-n, loc_pad-n, "");
        }

        /* Function name */
        if (g_str_equal(ar.what, "main")) {
            g_string_append(tb, ANSI_COLOR_GRAY " in main chunk" ANSI_COLOR_RESET);
        } else {
            g_string_append_printf(tb, ANSI_COLOR_GRAY " in function " ANSI_COLOR_RESET "%s",
                    ar.name ?: "[anonymous]");
        }

        if (level != max_level) {
            g_string_append(tb, "\n");
        }
    }

    lua_pushstring(L, tb->str);
    g_string_free(tb, TRUE);
    return 1;
}

static const gchar *
extract_error_message(lua_State *L, const gchar *message)
{
    lua_Debug ar;
    for (gint level = 0; ; level++) {
        if (!lua_getstack(L, level, &ar))
            return message;
        lua_getinfo(L, "Sl", &ar);
        if (!g_str_equal(ar.what, "C"))
            break;
    }

    if (strncmp(message, ar.short_src, strlen(ar.short_src)))
        return message;

    const gchar *tail = message + strlen(ar.short_src);

    if (*tail != ':')
        return message;
    tail ++;
    return strchr(tail, ' ') + 1;
}

gint
luaH_dofunction_on_error(lua_State *L)
{
    /* Guaranteed stack availability: LUA_MINSTACK = 20
     * This function's stack use is five items, so even on stack overflow
     * there should be no problem producing a full stack trace. */
    g_assert(lua_checkstack(L, 5));

    lua_pushliteral(L, "Lua error: ");
    lua_pushstring(L, extract_error_message(L, lua_tostring(L, -2)));

    lua_pushliteral(L, "\nTraceback:\n");
    luaH_traceback(L, L, 1);
    lua_concat(L, 4);
    return 1;
}

void
luaH_add_paths(lua_State *L, const gchar *config_dir)
{
    lua_getglobal(L, "package");
    if(LUA_TTABLE != lua_type(L, -1)) {
        warn("package is not a table");
        return;
    }
    lua_getfield(L, -1, "path");
    if(LUA_TSTRING != lua_type(L, -1)) {
        warn("package.path is not a string");
        lua_pop(L, 1);
        return;
    }

    /* compile list of package search paths */
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);

#if DEVELOPMENT_PATHS
    /* allows for testing luakit in the project directory */
    g_ptr_array_add(paths, g_strdup("./lib"));
    g_ptr_array_add(paths, g_strdup("./config"));
#endif

    /* add luakit install path */
    g_ptr_array_add(paths, g_build_filename(LUAKIT_INSTALL_PATH, "lib", NULL));

    /* add users config dir (see: XDG_CONFIG_DIR) */
    if (config_dir)
        g_ptr_array_add(paths, g_strdup(config_dir));

    /* add configured XDGPREFIX */
    g_ptr_array_add(paths, g_build_filename(LUAKIT_CONFIG_PATH, "luakit", NULL));

    /* add system config dirs (see: XDG_CONFIG_DIRS) */
    const gchar* const *config_dirs = g_get_system_config_dirs();
    for (; *config_dirs; config_dirs++)
        g_ptr_array_add(paths, g_build_filename(*config_dirs, "luakit", NULL));

    const gchar *path;
    for (guint i = 0; i < paths->len; i++) {
        path = paths->pdata[i];
        /* Search for file */
        lua_pushliteral(L, ";");
        lua_pushstring(L, path);
        lua_pushliteral(L, "/?.lua");
        lua_concat(L, 3);
        /* Search for lib */
        lua_pushliteral(L, ";");
        lua_pushstring(L, path);
        lua_pushliteral(L, "/?/init.lua");
        lua_concat(L, 3);
        /* concat with package.path */
        lua_concat(L, 3);
    }

    g_ptr_array_free(paths, TRUE);

    /* package.path = "concatenated string" */
    lua_setfield(L, -2, "path");

    /* remove package module from stack */
    lua_pop(L, 1);
}

gint
luaH_push_gerror(lua_State *L, GError *error)
{
    g_assert(error);
    lua_createtable(L, 0, 2);
    lua_pushfstring(L, "%s-%d", g_quark_to_string(error->domain), error->code);
    lua_setfield(L, -2, "code");
    lua_pushstring(L, error->message);
    lua_setfield(L, -2, "message");
    return 1;
}

gint
luaH_push_strv(lua_State *L, const gchar * const *strv)
{
    lua_newtable(L);
    if (!strv)
        return 1;
    gint n = 1;
    while (*strv) {
        lua_pushstring(L, *strv);
        lua_rawseti(L, -2, n++);
        strv++;
    }
    return 1;
}

const gchar **
luaH_checkstrv(lua_State *L, gint idx)
{
    luaH_checktable(L, idx);
    gint len = lua_objlen(L, idx);
    GPtrArray *langs = g_ptr_array_new();
    for (gint i = 1; i <= len; ++i) {
        lua_rawgeti(L, idx, i);
        if (!lua_isstring(L, -1)) {
            g_ptr_array_free(langs, TRUE);
            luaL_error(L, "bad argument %d ({string} expected, but array item %d has type %s)",
                    idx, i, lua_typename(L, lua_type(L, -1)));
        }
        g_ptr_array_add(langs, (gchar*)lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    g_ptr_array_add(langs, NULL);
    const gchar ** strv = (const gchar **)langs->pdata;
    g_ptr_array_free(langs, FALSE);
    return strv;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
