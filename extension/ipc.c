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
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "extension/extension.h"
#include "extension/clib/luakit.h"
#include "extension/ipc.h"
#include "extension/scroll.h"
#include "common/util.h"
#include "common/luajs.h"
#include "common/luaserialize.h"
#include "common/clib/ipc.h"

static GPtrArray *queued_page_ipc;

IPC_NO_HANDLER(page_created)
IPC_NO_HANDLER(log)

void
ipc_recv_lua_require_module(ipc_endpoint_t *UNUSED(ipc), const ipc_lua_require_module_t *msg, guint length)
{
    const char *module_name = msg->module_name;
    assert(strlen(module_name) > 0);
    assert(strlen(module_name) == length-1);

    lua_pushstring(common.L, module_name);
    lua_getglobal(common.L, "require");
    luaH_dofunction(common.L, 1, 0);
}

void
ipc_recv_lua_ipc(ipc_endpoint_t *UNUSED(ipc), const ipc_lua_ipc_t *msg, guint length)
{
    ipc_channel_recv(common.L, msg->arg, length);
}

void
ipc_recv_extension_init(ipc_endpoint_t *UNUSED(ipc), gpointer UNUSED(msg), guint UNUSED(length))
{
    emit_pending_page_creation_ipc();
    luakit_lib_emit_pending_signals(common.L);
}

void
ipc_recv_scroll(ipc_endpoint_t *UNUSED(ipc), const guint8 *msg, guint length)
{
    lua_State *L = common.L;
    gint n = lua_deserialize_range(L, msg, length);
    g_assert_cmpint(n, ==, 3);

    guint64 page_id = lua_tointeger(L, -3);
    gint scroll_x = lua_tointeger(L, -2);
    gint scroll_y = lua_tointeger(L, -1);

    web_scroll_to(page_id, scroll_x, scroll_y);

    lua_pop(L, 3);
}

void
ipc_recv_eval_js(ipc_endpoint_t *UNUSED(ipc), const guint8 *msg, guint length)
{
    lua_State *L = common.L;
    gint top = lua_gettop(L);
    gint n = lua_deserialize_range(L, msg, length);
    g_assert_cmpint(n, ==, 5);

    gboolean no_return = lua_toboolean(L, -5);
    const gchar *script = lua_tostring(L, -4);
    const gchar *source = lua_tostring(L, -3);
    guint64 page_id = lua_tointeger(L, -2);
    /* cb ref is index -1 */

    WebKitWebPage *page = webkit_web_extension_get_page(extension.ext, page_id);
    if (!page) {
        /* Notify UI to free callback ref */
        ipc_send_lua(extension.ipc, IPC_TYPE_eval_js, L, -2, -1);
        lua_settop(L, top);
        return;
    }

    WebKitFrame *frame = webkit_web_page_get_main_frame(page);
    WebKitScriptWorld *world = webkit_script_world_get_default();
    JSCContext *ctx = webkit_frame_get_js_context_for_script_world(frame, world);
    n = luajs_eval_js(L, ctx, script, source, 1, no_return);
    g_object_unref(ctx);
    /* Send [page_id, cb, ret] or [page_id, cb, nil, error] */
    ipc_send_lua(extension.ipc, IPC_TYPE_eval_js, L, -n-2, -1);
    lua_settop(L, top);
}

void
ipc_recv_crash(ipc_endpoint_t *UNUSED(ipc), const guint8 *UNUSED(msg), guint UNUSED(length))
{
    raise(SIGKILL);
}

static void
emit_page_created_ipc(WebKitWebPage *web_page, gpointer UNUSED(user_data))
{
    ipc_page_created_t msg = {
        .page_id = webkit_web_page_get_id(web_page),
        .pid = getpid(),
    };

    ipc_header_t header = { .type = IPC_TYPE_page_created, .length = sizeof(msg) };
    ipc_send(extension.ipc, &header, &msg);
}

void
emit_pending_page_creation_ipc(void)
{
    if (queued_page_ipc) {
        g_ptr_array_foreach(queued_page_ipc, (GFunc)emit_page_created_ipc, NULL);
        g_ptr_array_free(queued_page_ipc, TRUE);
        queued_page_ipc = NULL;
    }
}

static void
web_page_created_cb(WebKitWebExtension *UNUSED(ext), WebKitWebPage *web_page, gpointer UNUSED(user_data))
{
    /* QUEUE until we've fully loaded web modules */
    if (queued_page_ipc)
        g_ptr_array_add(queued_page_ipc, web_page);
    else
        emit_page_created_ipc(web_page, NULL);
}

int
web_extension_connect(const gchar *socket_path)
{
    int sock;

    struct sockaddr_un remote;
    memset(&remote, 0, sizeof(remote));
    remote.sun_family = AF_UNIX;
    strcpy(remote.sun_path, socket_path);
    int len = offsetof(struct sockaddr_un, sun_path) + strlen(remote.sun_path);

    debug("luakit web process: connecting to %s", socket_path);

    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        goto fail_socket;
    }

    if (connect(sock, (struct sockaddr *)&remote, len) == -1) {
        perror("connect");
        goto fail_connect;
    }

    debug("luakit web process: connected");

    ipc_endpoint_connect_to_socket(extension.ipc, sock);

    g_signal_connect(extension.ext, "page-created", G_CALLBACK(web_page_created_cb), NULL);
    queued_page_ipc = g_ptr_array_sized_new(1);

    return 0;
fail_connect:
    close(sock);
fail_socket:
    return 1;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
