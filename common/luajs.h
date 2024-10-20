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

#ifndef LUAKIT_COMMON_LUAJS_H
#define LUAKIT_COMMON_LUAJS_H

#include <JavaScriptCore/JavaScript.h>
#include <jsc/jsc.h>
#include <stdlib.h>
#include <glib.h>
#include <lua.h>

gchar* tostring(JSContextRef context, JSValueRef value, gchar **error);
gint luaJS_pushstring(lua_State *L, JSContextRef context, JSValueRef value, gchar **error);
gint luaJS_pushobject(lua_State *L, JSContextRef context, JSObjectRef obj, gchar **error);
gint luaJS_pushvalue(lua_State *L, JSContextRef context, JSValueRef value, gchar **error);
JSValueRef luaJS_fromtable(lua_State *L, JSContextRef context, gint idx, gchar **error);
JSValueRef luaJS_tovalue(lua_State *L, JSContextRef context, gint idx, gchar **error);
JSValueRef luaJS_make_exception(JSContextRef context, const gchar *error);

int luajs_eval_js(lua_State *L, JSCContext *ctx, const char *code, const char *source, guint line, bool no_return);

#endif /* end of include guard: LUAKIT_COMMON_LUAJS_H */

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
