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

#ifndef LUAKIT_EXTENSION_CLIB_DOM_ELEMENT_H
#define LUAKIT_EXTENSION_CLIB_DOM_ELEMENT_H

#include "extension/extension.h"

#include "common/util.h"
#include "common/luaclass.h"
#include "common/luaobject.h"
#include "extension/clib/page.h"

#include <gtk/gtk.h>

#define LUA_DOM_ELEMENT_FUNCS(lua_class, type, prefix)        \
    LUA_CLASS_FUNCS(prefix, lua_class)                        \
    static inline type *                                      \
    prefix##_new(lua_State *L) {                              \
        type *p = lua_newuserdata(L, sizeof(type));           \
        p_clear(p, 1);                                        \
        p->signals = signal_new();                            \
        p->dom_events = signal_new();                         \
        luaH_settype(L, &(lua_class));                        \
        lua_newtable(L);                                      \
        lua_newtable(L);                                      \
        lua_setmetatable(L, -2);                              \
        lua_setfenv(L, -2);                                   \
        lua_pushvalue(L, -1);                                 \
        luaH_class_emit_signal(L, &(lua_class), "new", 1, 0); \
        return p;                                             \
    }

typedef struct _dom_element_t {
    LUA_OBJECT_HEADER
    signal_t *dom_events;
    WebKitDOMElement *element;
} dom_element_t;

void dom_element_class_setup(lua_State *);
gint luaH_dom_element_from_node(lua_State *L, WebKitDOMElement* node);
JSCValue *dom_element_js_ref(page_t *page, dom_element_t *element);
dom_element_t * luaH_to_dom_element(lua_State *L, gint idx);

#endif

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
