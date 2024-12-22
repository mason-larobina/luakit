/*
 * widgets/webview.c - webkit webview widget
 *
 * Copyright © 2010-2011 Mason Larobina <mason.larobina@gmail.com>
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

#include <webkit2/webkit2.h>
#include <math.h>

#include "globalconf.h"
#include "widgets/common.h"
#include "widgets/webview.h"
#include "common/property.h"
#include "luah.h"
#include "clib/widget.h"
#include "clib/request.h"
#include "common/signal.h"
#include "web_context.h"
#include "common/ipc.h"
#include "common/luayield.h"

typedef struct {
    /** The parent widget_t struct */
    widget_t *widget;
    /** The webview widget */
    WebKitWebView *view;
    /** The user content manager for the webview */
    WebKitUserContentManager *user_content;
    /** A list of stylesheets enabled for this user content */
    GList *stylesheets;
    /** Helpers for user content manager updating */
    gboolean stylesheet_added,
             stylesheet_removed,
             stylesheet_refreshed;
    /** Current webview uri */
    gchar *uri;
    /** Currently hovered uri */
    gchar *hover;

    /** Inspector properties */
    WebKitWebInspector *inspector;
    /** Whether inspector is open */
    gboolean inspector_open;

    guint htr_context;
    gboolean is_committed;
    gboolean is_failed;
    gboolean private;

    /** Document size */
    gint doc_w, doc_h;
    /** Viewport size */
    gint win_w, win_h;
    /** Current scroll position */
    gint scroll_x, scroll_y;

    /** TLS Certificate, if using HTTPS */
    GTlsCertificate *cert;

    ipc_endpoint_t *ipc;
    pid_t web_process_id;
} webview_data_t;

static WebKitWebView *related_view;

#define luaH_checkwvdata(L, udx) ((webview_data_t*)(luaH_checkwebview(L, udx)->data))

static struct {
    GSList *refs;
    GSList *old_refs;
} last_popup = { NULL, NULL };

static property_t webview_properties[] = {
  { L_TK_EDITABLE,           "editable",         BOOL,    TRUE },
  { L_TK_PROGRESS,    "estimated-load-progress", DOUBLE, FALSE },
  { L_TK_IS_LOADING,        "is-loading",        BOOL,   FALSE },
  { L_TK_IS_PLAYING_AUDIO,  "is-playing-audio",  BOOL,   FALSE },
  { L_TK_TITLE,             "title",             CHAR,   FALSE },
  { L_TK_ZOOM_LEVEL,        "zoom-level",        DOUBLE,  TRUE },
  { 0,                      NULL,                0,      0     },
};

static property_t webview_settings_properties[] = {
  { L_TK_ALLOW_FILE_ACCESS_FROM_FILE_URLS,          "allow-file-access-from-file-urls",          BOOL,  TRUE },
  { L_TK_ALLOW_MODAL_DIALOGS,                       "allow-modal-dialogs",                       BOOL,  TRUE },
  { L_TK_ALLOW_UNIVERSAL_ACCESS_FROM_FILE_URLS,     "allow-universal-access-from-file-urls",     BOOL,  TRUE },
  { L_TK_AUTO_LOAD_IMAGES,                          "auto-load-images",                          BOOL,  TRUE },
  { L_TK_CURSIVE_FONT_FAMILY,                       "cursive-font-family",                       CHAR,  TRUE },
  { L_TK_DEFAULT_CHARSET,                           "default-charset",                           CHAR,  TRUE },
  { L_TK_DEFAULT_FONT_FAMILY,                       "default-font-family",                       CHAR,  TRUE },
  { L_TK_DEFAULT_FONT_SIZE,                         "default-font-size",                         INT,   TRUE },
  { L_TK_DEFAULT_MONOSPACE_FONT_SIZE,               "default-monospace-font-size",               INT,   TRUE },
  { L_TK_DRAW_COMPOSITING_INDICATORS,               "draw-compositing-indicators",               BOOL,  TRUE },
  { L_TK_ENABLE_ACCELERATED_2D_CANVAS,              "enable-accelerated-2d-canvas",              BOOL,  TRUE },
  { L_TK_ENABLE_CARET_BROWSING,                     "enable-caret-browsing",                     BOOL,  TRUE },
  { L_TK_ENABLE_DEVELOPER_EXTRAS,                   "enable-developer-extras",                   BOOL,  TRUE },
  { L_TK_ENABLE_DNS_PREFETCHING,                    "enable-dns-prefetching",                    BOOL,  TRUE },
  { L_TK_ENABLE_FRAME_FLATTENING,                   "enable-frame-flattening",                   BOOL,  TRUE },
  { L_TK_ENABLE_FULLSCREEN,                         "enable-fullscreen",                         BOOL,  TRUE },
  { L_TK_ENABLE_HTML5_DATABASE,                     "enable-html5-database",                     BOOL,  TRUE },
  { L_TK_ENABLE_HTML5_LOCAL_STORAGE,                "enable-html5-local-storage",                BOOL,  TRUE },
  { L_TK_ENABLE_HYPERLINK_AUDITING,                 "enable-hyperlink-auditing",                 BOOL,  TRUE },
  { L_TK_ENABLE_JAVA,                               "enable-java",                               BOOL,  TRUE },
  { L_TK_ENABLE_JAVASCRIPT,                         "enable-javascript",                         BOOL,  TRUE },
  { L_TK_ENABLE_MEDIA_STREAM,                       "enable-media-stream",                       BOOL,  TRUE },
  { L_TK_ENABLE_MEDIASOURCE,                        "enable-mediasource",                        BOOL,  TRUE },
  { L_TK_ENABLE_PAGE_CACHE,                         "enable-page-cache",                         BOOL,  TRUE },
  { L_TK_ENABLE_PLUGINS,                            "enable-plugins",                            BOOL,  TRUE },
  /* replaces resizable-text-areas */
  { L_TK_ENABLE_RESIZABLE_TEXT_AREAS,               "enable-resizable-text-areas",               BOOL,  TRUE },
  { L_TK_ENABLE_SITE_SPECIFIC_QUIRKS,               "enable-site-specific-quirks",               BOOL,  TRUE },
  { L_TK_ENABLE_SMOOTH_SCROLLING,                   "enable-smooth-scrolling",                   BOOL,  TRUE },
  { L_TK_ENABLE_SPATIAL_NAVIGATION,                 "enable-spatial-navigation",                 BOOL,  TRUE },
  { L_TK_ENABLE_WEBGL,                              "enable-webgl",                              BOOL,  TRUE },
  { L_TK_ENABLE_TABS_TO_LINKS,                      "enable-tabs-to-links",                      BOOL,  TRUE },
  { L_TK_ENABLE_WEBAUDIO,                           "enable-webaudio",                           BOOL,  TRUE },
  { L_TK_ENABLE_WRITE_CONSOLE_MESSAGES_TO_STDOUT,   "enable-write-console-messages-to-stdout",   BOOL,  TRUE },
  { L_TK_ENABLE_XSS_AUDITOR,                        "enable-xss-auditor",                        BOOL,  TRUE },
  { L_TK_FANTASY_FONT_FAMILY,                       "fantasy-font-family",                       CHAR,  TRUE },
  { L_TK_JAVASCRIPT_CAN_ACCESS_CLIPBOARD,           "javascript-can-access-clipboard",           BOOL,  TRUE },
  { L_TK_JAVASCRIPT_CAN_OPEN_WINDOWS_AUTOMATICALLY, "javascript-can-open-windows-automatically", BOOL,  TRUE },
  { L_TK_MEDIA_PLAYBACK_ALLOWS_INLINE,              "media-playback-allows-inline",              BOOL,  TRUE },
  { L_TK_MEDIA_PLAYBACK_REQUIRES_GESTURE,           "media-playback-requires-user-gesture",      BOOL,  TRUE },
  { L_TK_MINIMUM_FONT_SIZE,                         "minimum-font-size",                         INT,   TRUE },
  { L_TK_MONOSPACE_FONT_FAMILY,                     "monospace-font-family",                     CHAR,  TRUE },
  { L_TK_PICTOGRAPH_FONT_FAMILY,                    "pictograph-font-family",                    CHAR,  TRUE },
  { L_TK_PRINT_BACKGROUNDS,                         "print-backgrounds",                         BOOL,  TRUE },
  { L_TK_SANS_SERIF_FONT_FAMILY,                    "sans-serif-font-family",                    CHAR,  TRUE },
  { L_TK_SERIF_FONT_FAMILY,                         "serif-font-family",                         CHAR,  TRUE },
  { L_TK_USER_AGENT,                                "user-agent",                                CHAR,  TRUE },
  { L_TK_ZOOM_TEXT_ONLY,                            "zoom-text-only",                            BOOL,  TRUE },
  { 0,                                              NULL,                                        0,     0    },
};

widget_t*
luaH_checkwebview(lua_State *L, gint udx)
{
    widget_t *w = luaH_checkwidget(L, udx);
    if (w->info->tok != L_TK_WEBVIEW)
        luaL_argerror(L, udx, "incorrect widget type (expected webview)");
    return w;
}

widget_t*
webview_get_by_id(guint64 view_id)
{
    for (unsigned i = 0; i < globalconf.webviews->len; i++) {
        widget_t *w = g_ptr_array_index(globalconf.webviews, i);
        if (webkit_web_view_get_page_id(WEBKIT_WEB_VIEW(w->widget)) == view_id)
            return w;
    }
    return NULL;
}

static void update_uri(widget_t *w, const gchar *uri);

#include "widgets/webview/javascript.c"
#include "widgets/webview/downloads.c"
#include "widgets/webview/history.c"
#include "widgets/webview/scroll.c"
#include "widgets/webview/inspector.c"
#include "widgets/webview/find_controller.c"
#include "widgets/webview/stylesheets.c"
#include "widgets/webview/auth.c"

static gint
luaH_webview_load_string(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    const gchar *string = luaL_checkstring(L, 2);
    const gchar *base_uri = luaL_checkstring(L, 3);
    webkit_web_view_load_alternate_html(d->view, string, base_uri, NULL);
    return 0;
}

struct save_cb_s {
    const gchar *filename;
    widget_t *window;
};

static void
save_cb(GObject *o, GAsyncResult *res, gpointer user_data) {
    WebKitWebView *view = (WebKitWebView *) o;
    struct save_cb_s *scbs = (struct save_cb_s *) user_data;
    lua_State *L = common.L;
    GError *err = NULL;
    gboolean result;

    result = webkit_web_view_save_to_file_finish(view, res, &err);

    luaH_object_push(L, scbs->window->ref);
    lua_pushstring(L, scbs->filename);
    if (result)
        lua_pushnil(L);
    else
        lua_pushstring(L, err->message);
    luaH_object_emit_signal(L, -3, "save-finished", 2, 0);
    lua_pop(L, 1);

    g_free(scbs);
}

static gint
luaH_webview_save(lua_State *L)
{
    struct save_cb_s *scbs = g_new0(struct save_cb_s, 1);
    webview_data_t *d = luaH_checkwvdata(L, 1);
    scbs->filename = luaL_checkstring(L, 2);
    scbs->window = d->widget;
    GFile *fd = g_file_new_for_path(scbs->filename);
    webkit_web_view_save_to_file(d->view, fd, WEBKIT_SAVE_MODE_MHTML, NULL, save_cb, (gpointer) scbs);
    return 0;
}

static void
notify_cb(WebKitWebView* UNUSED(v), GParamSpec *ps, widget_t *w)
{
    static GHashTable *wvprops = NULL;
    property_t *p;

    if (!wvprops) {
        wvprops = g_hash_table_new(g_str_hash, g_str_equal);
        for (p = webview_properties; p->name; p++)
            g_hash_table_insert(wvprops, (gpointer)p->name, (gpointer)p);
    }

    if ((p = g_hash_table_lookup(wvprops, ps->name))) {
        lua_State *L = common.L;
        luaH_object_push(L, w->ref);
        luaH_object_property_signal(L, -1, p->tok);
        lua_pop(L, 1);
    }
}

static void
update_uri(widget_t *w, const gchar *uri)
{
    webview_data_t *d = w->data;

    if (!w->destructor)
        return;

    if (!uri) {
        uri = webkit_web_view_get_uri(d->view);
        if (!uri || !uri[0])
            uri = "about:blank";
    }

    /* uris are the same, do nothing */
    if (g_strcmp0(d->uri, uri)) {
        g_free(d->uri);
        d->uri = g_strdup(uri);
        lua_State *L = common.L;
        luaH_object_push(L, w->ref);
        luaH_object_emit_signal(L, -1, "property::uri", 0, 0);
        lua_pop(L, 1);
    }
}

static gboolean
load_failed_cb(WebKitWebView* UNUSED(v), WebKitLoadEvent UNUSED(e),
        gchar *failing_uri, GError *error, widget_t *w)
{
    update_uri(w, failing_uri);

    lua_State *L = common.L;
    ((webview_data_t*) w->data)->is_failed = TRUE;
    luaH_object_push(L, w->ref);
    lua_pushstring(L, "failed");
    lua_pushstring(L, failing_uri);
    luaH_push_gerror(L, error);
    gint ret = luaH_object_emit_signal(L, -4, "load-status", 3, 1);
    gboolean ignore = ret && lua_toboolean(L, -1);
    lua_pop(L, ret + 1);
    return ignore;
}

static int
luaH_webview_push_certificate_flags(lua_State *L, GTlsCertificateFlags errors)
{
    lua_newtable(L);
    int n = 1;

#define CASE(err, str) \
    if (errors & G_TLS_CERTIFICATE_##err) { \
        lua_pushliteral(L, str); \
        lua_rawseti(L, -2, n++); \
    }

    CASE(UNKNOWN_CA, "unknown-ca")
    CASE(BAD_IDENTITY, "bad-identity")
    CASE(NOT_ACTIVATED, "not-activated")
    CASE(EXPIRED, "expired")
    CASE(REVOKED, "revoked")
    CASE(INSECURE, "insecure")
    CASE(GENERIC_ERROR, "generic-error")

    return 1;
}

static gboolean
load_failed_tls_cb(WebKitWebView* UNUSED(v), gchar *failing_uri,
        GTlsCertificate *certificate, GTlsCertificateFlags errors, widget_t *w)
{
    lua_State *L = common.L;
    webview_data_t *d = w->data;
    update_uri(w, failing_uri);

    ((webview_data_t*) w->data)->is_failed = TRUE;

    /* Store certificate information */
    if (d->cert) {
        g_object_unref(G_OBJECT(d->cert));
        d->cert = NULL;
    }
    d->cert = certificate;
    g_object_ref(G_OBJECT(d->cert));

    luaH_object_push(L, w->ref);
    lua_pushliteral(L, "failed");
    lua_pushstring(L, failing_uri);

    GError *error = g_error_new_literal(LUAKIT_ERROR, LUAKIT_ERROR_TLS,
            "Unacceptable TLS certificate");
    luaH_push_gerror(L, error);
    g_error_free(error);
    luaH_webview_push_certificate_flags(L, errors);
    lua_setfield(L, -2, "certificate_flags");

    luaH_object_emit_signal(L, -4, "load-status", 3, 0);
    lua_pop(L, 1);
    return TRUE; /* Prevent load-failed signal */
}

static void
webview_get_source_finished(WebKitWebResource *main_resource, GAsyncResult *res, lua_State *L)
{
    gsize length;
    const gchar *source = (gchar*) webkit_web_resource_get_data_finish(main_resource, res, &length, NULL);
    g_object_unref(main_resource);
    lua_pushlstring(L, source, length);
    luaH_resume(L, 1);
}

static gint
luaH_webview_push_source(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    WebKitWebResource *main_resource = webkit_web_view_get_main_resource(d->view);
    if (!main_resource)
        return 0;

    g_object_ref(main_resource);
    webkit_web_resource_get_data(main_resource, NULL,
            (GAsyncReadyCallback) webview_get_source_finished, L);

    return luaH_yield(L);
}

static void
load_changed_cb(WebKitWebView* UNUSED(v), WebKitLoadEvent e, widget_t *w)
{
    webview_data_t *d = w->data;
    lua_State *L = common.L;

    /* get load status literal */
    gchar *name = NULL;
    switch (e) {

#define LT_CASE(a, l) case WEBKIT_LOAD_##a: name = l; break;
        LT_CASE(STARTED,                         "provisional")
        LT_CASE(REDIRECTED,                      "redirected")
        LT_CASE(COMMITTED,                       "committed")
        LT_CASE(FINISHED,                        "finished")
#undef  LT_CASE

      default:
        warn("programmer error, unable to get load status literal");
        break;
    }

    update_uri(w, NULL);

    if (e == WEBKIT_LOAD_STARTED) {
        ((webview_data_t*) w->data)->is_committed = FALSE;
    } else if (e == WEBKIT_LOAD_COMMITTED || e == WEBKIT_LOAD_FINISHED) {
        ((webview_data_t*) w->data)->is_committed = TRUE;
    }

    /* Store certificate information about current page */
    if (e == WEBKIT_LOAD_STARTED) {
        if (d->cert) {
            g_object_unref(G_OBJECT(d->cert));
            d->cert = NULL;
        }
    } else if (e == WEBKIT_LOAD_COMMITTED) {
        g_assert(!d->cert);
        webkit_web_view_get_tls_info(d->view, &d->cert, NULL);
        if (d->cert)
            g_object_ref(G_OBJECT(d->cert));
    }

    if (e == WEBKIT_LOAD_COMMITTED)
        webview_update_stylesheets(L, w);

    /* Don't send "finished" signal after "failed" signal */
    if (e == WEBKIT_LOAD_STARTED)
        ((webview_data_t*) w->data)->is_failed = FALSE;
    if (e == WEBKIT_LOAD_FINISHED && ((webview_data_t*) w->data)->is_failed)
        return;

    luaH_object_push(L, w->ref);
    lua_pushstring(L, name);
    luaH_object_emit_signal(L, -2, "load-status", 1, 0);
    lua_pop(L, 1);
}


static GtkWidget*
create_cb(WebKitWebView* v, WebKitNavigationAction* UNUSED(a), widget_t *w)
{
    WebKitWebView *view = NULL;
    widget_t *new;

    g_assert(!related_view);
    related_view = v;
    lua_State *L = common.L;
    gint top = lua_gettop(L);
    luaH_object_push(L, w->ref);
    gint ret = luaH_object_emit_signal(L, -1, "create-web-view", 0, 1);
    related_view = NULL;

    /* check for new webview widget */
    if (ret) {
        if ((new = luaH_towidget(L, -1))) {
            if (new->info->tok == L_TK_WEBVIEW)
                view = WEBKIT_WEB_VIEW(((webview_data_t*)new->data)->view);
            else
                warn("invalid return widget type (expected webview, got %s)",
                        new->info->name);
        } else
            warn("invalid signal return object type (expected webview widget, "
                    "got %s)", lua_typename(L, lua_type(L, -1)));
    }

    lua_settop(L, top);
    return GTK_WIDGET(view);
}

static gboolean
decide_policy_cb(WebKitWebView* UNUSED(v), WebKitPolicyDecision *p,
        WebKitPolicyDecisionType type, widget_t *w)
{
    lua_State *L = common.L;

    switch (type) {
/* Raises the "navigation-request" signal on a webkit navigation policy
 * decision request. The default action is to load the requested uri.
 *
 * The signal handler is able to:
 *  - return true for the handler execution to stop and the request to continue
 *  - return false for the handler execution to stop and the request to hault
 *  - do nothing and give the navigation decision to the next signal handler
 *
 * This signal is also where you would attach custom scheme handlers to take
 * over the navigation request by launching an external application.
 */
      case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION:
      case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION:
      {
          gint top = lua_gettop(L);
          WebKitNavigationPolicyDecision *np = WEBKIT_NAVIGATION_POLICY_DECISION(p);
          WebKitNavigationAction *na = webkit_navigation_policy_decision_get_navigation_action(np);
          const gchar *signal_name = type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION
                ? "navigation-request" : "new-window-decision";
          const gchar *uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(na));
          gchar *reason = NULL;

          switch (webkit_navigation_action_get_navigation_type(na)) {
# define NR_CASE(a, l) case WEBKIT_NAVIGATION_TYPE_##a: reason = l; break;
              NR_CASE(LINK_CLICKED,     "link-clicked");
              NR_CASE(FORM_SUBMITTED,   "form-submitted");
              NR_CASE(BACK_FORWARD,     "back-forward");
              NR_CASE(RELOAD,           "reload");
              NR_CASE(FORM_RESUBMITTED, "form-resubmitted");
              NR_CASE(OTHER,            "other");
#undef  NR_CASE
            default:
              warn("programmer error, unable to get web navigation reason literal");
              break;
          }

          luaH_object_push(L, w->ref);
          lua_pushstring(L, uri);
          lua_pushstring(L, reason);
          gint ret = luaH_object_emit_signal(L, -3, signal_name, 2, 1);
          gboolean ignore = ret && !lua_toboolean(L, -1);

          if (ignore)
              webkit_policy_decision_ignore(p);

          lua_settop(L, top);
          return ignore;
      }
      case WEBKIT_POLICY_DECISION_TYPE_RESPONSE:
      {
        // replaces mime_type_decision_cb() in widgets/webview/downloads.c
        WebKitResponsePolicyDecision *rp = WEBKIT_RESPONSE_POLICY_DECISION(p);
        WebKitURIResponse *r = webkit_response_policy_decision_get_response(rp);
        const gchar *uri = webkit_uri_response_get_uri(r);
        const gchar *mime = webkit_uri_response_get_mime_type(r);

        if(webkit_uri_response_get_status_code(r)
            && !SOUP_STATUS_IS_SUCCESSFUL(webkit_uri_response_get_status_code(r)))
            return FALSE;

        luaH_object_push(L, w->ref);
        lua_pushstring(L, uri);
        lua_pushstring(L, mime);
        gint ret = luaH_object_emit_signal(L, -3, "mime-type-decision", 2, 1);

        gboolean ignore = ret && !lua_toboolean(L, -1);
        if (ignore)
            /* User responded with false, ignore request */
            webkit_policy_decision_ignore(p);
        else if (g_str_equal(mime, "application/x-extension-html"))
            webkit_policy_decision_use(p);
        else if (webkit_response_policy_decision_is_mime_type_supported(rp))
            webkit_policy_decision_use(p);
        else
            webkit_policy_decision_download(p);

        lua_pop(L, ret + 1);
        return TRUE;
      }
      default:
        break;
    }
    return FALSE;
}

static gint
luaH_webview_reload(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    webkit_web_view_reload(d->view);
    return 0;
}

static gint
luaH_webview_reload_bypass_cache(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    webkit_web_view_reload_bypass_cache(d->view);
    return 0;
}

static gint
luaH_webview_search(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    const gchar *text = luaL_checkstring(L, 2);
    gboolean case_sensitive = luaH_checkboolean(L, 3);
    gboolean forward = luaH_checkboolean(L, 4);
    gboolean wrap = luaH_checkboolean(L, 5);

    size_t textlen = strlen(text);
    guint max_match_count = textlen < 5 ? 100 : G_MAXUINT;

    WebKitFindController *webkit_fc = webkit_web_view_get_find_controller(d->view);
    webkit_find_controller_search_finish(webkit_fc);
    webkit_find_controller_search(webkit_fc, text,
            WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE * (!case_sensitive) |
            WEBKIT_FIND_OPTIONS_BACKWARDS * (!forward) |
            WEBKIT_FIND_OPTIONS_WRAP_AROUND * wrap,
            max_match_count);
    return 0;
}

static gint
luaH_webview_search_next(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    WebKitFindController *webkit_fc = webkit_web_view_get_find_controller(d->view);
    webkit_find_controller_search_next(webkit_fc);
    return 0;
}

static gint
luaH_webview_search_previous(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    WebKitFindController *webkit_fc = webkit_web_view_get_find_controller(d->view);
    webkit_find_controller_search_previous(webkit_fc);
    return 0;
}
static gint
luaH_webview_clear_search(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    WebKitFindController *webkit_fc = webkit_web_view_get_find_controller(d->view);
    webkit_find_controller_search_finish(webkit_fc);
    return 0;
}

/* Proxy for the is_loading property; included for compatibility */
static gint
luaH_webview_loading(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    luaH_gobject_index(L, webview_properties, L_TK_IS_LOADING, G_OBJECT(d->view));
    return 1;
}

static gint
luaH_webview_stop(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    webkit_web_view_stop_loading(d->view);
    return 0;
}

static gint
luaH_webview_crash(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    ipc_header_t header = {
        .type = IPC_TYPE_crash,
        .length = 0
    };
    ipc_send(d->ipc, &header, NULL);
    return 0;
}

/* check for trusted ssl certificate
* make sure this function is called after WEBKIT_LOAD_COMMITTED
*/
static gint
luaH_webview_ssl_trusted(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    const gchar *uri = webkit_web_view_get_uri(d->view);
    GTlsCertificate *cert;
    GTlsCertificateFlags cert_errors;
    if (uri && d->is_committed &&
            webkit_web_view_get_tls_info(d->view, &cert, &cert_errors)) {
        gboolean is_trusted = (cert_errors == 0);
        lua_pushboolean(L, is_trusted);
        return 1;
    }
    /* return nil if not viewing https uri */
    return 0;
}

static gint
luaH_webview_allow_certificate(lua_State *L)
{
    warn("webview:allow_certificate() is deprecated: use luakit.allow_certificate() instead");
    (void)luaH_checkwvdata(L, 1);
    lua_remove(L, 1);
    luaL_checkstring(L, 1);
    luaL_checkstring(L, 2);
    /* When removing this function, make luaH_luakit_allow_certificate static */
    return luaH_luakit_allow_certificate(L);
}

static gint
luaH_webview_push_certificate(lua_State *L, widget_t *w)
{
    webview_data_t *d = w->data;

    if (!d->cert)
        return 0;

    gchar *cert_pem;
    g_object_get(G_OBJECT(d->cert), "certificate-pem", &cert_pem, NULL);
    lua_pushstring(L, cert_pem);
    g_free(cert_pem);
    return 1;
}

static luakit_token_t
webview_translate_old_token(luakit_token_t token)
{
    switch(token) {
      case L_TK_ENABLE_SCRIPTS: return L_TK_ENABLE_JAVASCRIPT;
      default:                  return token;
    }
}

static void
favicon_cb(WebKitWebView* UNUSED(v), GParamSpec *UNUSED(param_spec), widget_t *w)
{
    lua_State *L = common.L;
    luaH_object_push(L, w->ref);
    luaH_object_emit_signal(L, -1, "favicon", 0, 0);
    lua_pop(L, 1);
}

static void
uri_cb(WebKitWebView* UNUSED(v), GParamSpec *UNUSED(param_spec), widget_t *w)
{
    update_uri(w, NULL);
}

static gboolean
permission_request_cb(WebKitWebView *UNUSED(v), WebKitPermissionRequest *request, widget_t *w)
{
    lua_State *L = common.L;
    gint top = lua_gettop(L);

    if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(request))
        lua_pushliteral(L, "notification");
    else if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(request))
        lua_pushliteral(L, "geolocation");
    else if (WEBKIT_IS_INSTALL_MISSING_MEDIA_PLUGINS_PERMISSION_REQUEST(request)) {
        lua_pushliteral(L, "install-missing-media-plugins");
        WebKitInstallMissingMediaPluginsPermissionRequest* ummpr = (WebKitInstallMissingMediaPluginsPermissionRequest*)request;
        lua_pushstring(L, webkit_install_missing_media_plugins_permission_request_get_description(ummpr));
    } else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(request)) {
        lua_pushliteral(L, "user-media");
        lua_createtable(L, 0, 2);
        WebKitUserMediaPermissionRequest* umpr = (WebKitUserMediaPermissionRequest*)request;
        lua_pushboolean(L, webkit_user_media_permission_is_for_audio_device(umpr));
        lua_setfield(L, -2, "audio");
        lua_pushboolean(L, webkit_user_media_permission_is_for_video_device(umpr));
        lua_setfield(L, -2, "video");
    } else
        return FALSE;

    gint argc = lua_gettop(L) - top;
    luaH_object_push(L, w->ref);
    lua_insert(L, top+1);
    gint ret = luaH_object_emit_signal(L, top+1, "permission-request", argc, 1);
    if (ret) {
        if (lua_toboolean(L, -1))
            webkit_permission_request_allow(request);
        else
            webkit_permission_request_deny(request);
    }
    lua_pop(L, 1);
    return ret > 0;
}

static gint
luaH_webview_set_pdfjs(lua_State *L)
{
    webview_data_t *d = luaH_checkwvdata(L, 1);
    gboolean enabled = luaH_checkboolean(L, 2);

    g_autoptr(WebKitFeatureList) features
        = webkit_settings_get_all_features();
    WebKitSettings *settings = webkit_web_view_get_settings(d->view);

    for (gsize i = 0; i < webkit_feature_list_get_length(features); i++) {
        WebKitFeature *feature = webkit_feature_list_get(features, i);
        if (!strcmp(
                webkit_feature_get_identifier(feature),
                "PdfJSViewer")) {
            webkit_settings_set_feature_enabled(settings, feature, enabled);
            break;
        }
    }

    return 0;
}

static gint
luaH_webview_index(lua_State *L, widget_t *w, luakit_token_t token)
{
    webview_data_t *d = w->data;
    gint ret;

    token = webview_translate_old_token(token);

    switch(token) {
      LUAKIT_WIDGET_INDEX_COMMON(w)
      PB_CASE(INSPECTOR,            d->inspector_open);
      PB_CASE(PRIVATE,              d->private);

      /* push property methods */
      PF_CASE(CLEAR_SEARCH,         luaH_webview_clear_search)
      /* push search methods */
      PF_CASE(SEARCH,               luaH_webview_search)
      PF_CASE(SEARCH_NEXT,          luaH_webview_search_next)
      PF_CASE(SEARCH_PREVIOUS,      luaH_webview_search_previous)
      /* push history navigation methods */
      PF_CASE(GO_BACK,              luaH_webview_go_back)
      PF_CASE(GO_FORWARD,           luaH_webview_go_forward)
      PF_CASE(CAN_GO_BACK,          luaH_webview_can_go_back)
      PF_CASE(CAN_GO_FORWARD,       luaH_webview_can_go_forward)
      /* push misc webview methods */
      PF_CASE(EVAL_JS,              luaH_webview_eval_js)
      PF_CASE(LOAD_STRING,          luaH_webview_load_string)
      PF_CASE(SAVE,                 luaH_webview_save)
      /* use is_loading property instead of this function */
      PF_CASE(LOADING,              luaH_webview_loading)
      PF_CASE(RELOAD,               luaH_webview_reload)
      PF_CASE(RELOAD_BYPASS_CACHE,  luaH_webview_reload_bypass_cache)
      PF_CASE(SSL_TRUSTED,          luaH_webview_ssl_trusted)
      PF_CASE(STOP,                 luaH_webview_stop)
      PF_CASE(CRASH,                luaH_webview_crash)
      /* push inspector webview methods */
      PF_CASE(SHOW_INSPECTOR,       luaH_webview_show_inspector)
      PF_CASE(CLOSE_INSPECTOR,      luaH_webview_close_inspector)

      PF_CASE(ALLOW_CERTIFICATE,    luaH_webview_allow_certificate)
      PF_CASE(SET_PDFJS,            luaH_webview_set_pdfjs)

      /* push string properties */
      PS_CASE(HOVERED_URI,          d->hover)
      PS_CASE(URI,                  d->uri)

      PI_CASE(WEB_PROCESS_ID,     d->web_process_id)

      case L_TK_SOURCE:
        return luaL_error(L, "view.source has been removed; use view:get_source() instead");
      case L_TK_GET_SOURCE:
        lua_pushcfunction(L, luaH_webview_push_source);
        luaH_yield_wrap_function(L);
        return 1;
      case L_TK_SESSION_STATE:
        return luaH_webview_push_session_state(L, d);
      case L_TK_STYLESHEETS:
        return luaH_webview_push_stylesheets_table(L);

      PN_CASE(ID, webkit_web_view_get_page_id(d->view))

      case L_TK_HISTORY:
        return luaH_webview_push_history(L, d->view);

      case L_TK_SCROLL:
        return luaH_webview_push_scroll_table(L);

      case L_TK_CERTIFICATE:
        return luaH_webview_push_certificate(L, w);

      default:
        break;
    }

    if ((ret = luaH_gobject_index(L, webview_properties, token,
            G_OBJECT(d->view))))
        return ret;

    if (token == L_TK_HARDWARE_ACCELERATION_POLICY) {
        /* HACK: there's only one exposed property that has an enum type, so we
         * special-case it; this should be refactored if there's more than one */
        switch (webkit_settings_get_hardware_acceleration_policy(webkit_web_view_get_settings(d->view))) {
            case WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND: lua_pushstring (L, "on-demand"); return 1;
            case WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS: lua_pushstring (L, "always"); return 1;
            case WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER: lua_pushstring (L, "never"); return 1;
            default: g_assert_not_reached();
        }
    }

    if ((ret = luaH_gobject_index(L, webview_settings_properties, token,
            G_OBJECT(webkit_web_view_get_settings(d->view)))))
        return ret;

    return luaL_error(L, "cannot get unknown webview property '%s'", lua_tostring(L, 2));
}

static gchar*
parse_uri(const gchar *uri) {
    /* check for null uri */
    if (!uri || !uri[0] || !g_strcmp0(uri, "about:blank"))
        return g_strdup("about:blank");
    /* check for scheme or "about:blank" */
    else if (g_strrstr(uri, "://"))
        return g_strdup(uri);
    /* check if uri points to a file */
    else if (file_exists(uri)) {
        if (g_path_is_absolute(uri))
            return g_strdup_printf("file://%s", uri);
        else { /* make path absolute */
            gchar *cwd = g_get_current_dir();
            gchar *path = g_build_filename(cwd, uri, NULL);
            gchar *new = g_strdup_printf("file://%s", path);
            g_free(cwd);
            g_free(path);
            return new;
        }
    }
    /* default to http:// scheme */
    return g_strdup_printf("http://%s", uri);
}

/* The __newindex method for the webview object */
static gint
luaH_webview_newindex(lua_State *L, widget_t *w, luakit_token_t token)
{
    size_t len;
    webview_data_t *d = w->data;
    gchar *uri;

    token = webview_translate_old_token(token);

    switch(token) {
      LUAKIT_WIDGET_NEWINDEX_COMMON(w)

      case L_TK_URI:
        uri = parse_uri(luaL_checklstring(L, 3, &len));
        webkit_web_view_load_uri(d->view, uri);
        update_uri(w, uri);
        g_free(uri);
        return 0;

      case L_TK_SESSION_STATE:
        luaH_webview_set_session_state(L, d);
        return 0;

      default:
        break;
    }

    /* If setting view.zoom_level = x, x != 1.0, then first reset zoom_level
     * This prevents an issue where the zoom_level is ignored after a view crash
     * https://github.com/luakit/luakit/issues/357 */
    if (token == L_TK_ZOOM_LEVEL && lua_isnumber(L, 3) && (lua_tonumber(L, 3) != 1.0)) {
        g_object_freeze_notify(G_OBJECT(d->view));
        g_object_set(d->view, "zoom-level", 1.0, NULL);
        g_object_thaw_notify(G_OBJECT(d->view));
    }

    /* check for webview widget gobject properties */
    gboolean emit = luaH_gobject_newindex(L, webview_properties, token, 3,
            G_OBJECT(d->view));

    if (token == L_TK_HARDWARE_ACCELERATION_POLICY) {
        /* HACK: there's only one exposed property that has an enum type, so we
         * special-case it; this should be refactored if there's more than one */
        const char *str = luaL_checkstring(L, 3);
        WebKitHardwareAccelerationPolicy value;
        if (g_str_equal(str, "on-demand"))
            value = WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND;
        else if (g_str_equal(str, "always"))
            value = WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS;
        else if (g_str_equal(str, "never"))
            value = WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER;
        else
            return luaL_error(L, "invalid value (expected one of 'on-demand', 'always', 'never')");
        webkit_settings_set_hardware_acceleration_policy(webkit_web_view_get_settings(d->view), value);
        emit = TRUE;
    }

    /* check for webkit widget's settings gobject properties */
    if (!emit)
        emit = luaH_gobject_newindex(L, webview_settings_properties, token, 3,
            G_OBJECT(webkit_web_view_get_settings(d->view)));

    if (emit)
        return luaH_object_property_signal(L, 1, token);

    return luaL_error(L, "cannot set unknown webview property '%s'", lua_tostring(L, 2));
}

static gboolean
expose_cb(GtkWidget* UNUSED(widget), cairo_t *UNUSED(e), widget_t *w)
{
    lua_State *L = common.L;
    luaH_object_push(L, w->ref);
    luaH_object_emit_signal(L, -1, "expose", 0, 0);
    lua_pop(L, 1);
    return FALSE;
}

static void
mouse_target_changed_cb(WebKitWebView* UNUSED(v), WebKitHitTestResult *htr,
        guint UNUSED(modifiers), widget_t *w)
{
    lua_State *L = common.L;
    webview_data_t *d = w->data;
    d->htr_context = webkit_hit_test_result_get_context(htr);

    const char *link = NULL;
    if (webkit_hit_test_result_context_is_link(htr))
        link = webkit_hit_test_result_get_link_uri(htr);

    /* links are identical, do nothing */
    if (d->hover && link && !strcmp(d->hover, link))
        return;

    luaH_object_push(L, w->ref);

    if (d->hover) {
        lua_pushstring(L, d->hover);
        g_free(d->hover);
        luaH_object_emit_signal(L, -2, "link-unhover", 1, 0);
    }

    if (link) {
        d->hover = g_strdup(link);
        lua_pushstring(L, d->hover);
        luaH_object_emit_signal(L, -2, "link-hover", 1, 0);
        luaH_object_emit_signal(L, -1, "property::hovered_uri", 0, 0);
    } else
        d->hover = NULL;

    lua_pop(L, 1);
}

static gint
luaH_push_hit_test(lua_State *L, WebKitWebView* UNUSED(v), widget_t *w)
{
    /* get hit test */
    guint c = ((webview_data_t*) w->data)->htr_context;

    /* create new table to store hit test context data */
    lua_newtable(L);
    const gchar *name;

#define HTR_CHECK(a, l)                             \
    if ((c & WEBKIT_HIT_TEST_RESULT_CONTEXT_##a)) { \
        name = l;                                   \
        lua_pushstring(L, name);                    \
        lua_pushboolean(L, TRUE);                   \
        lua_rawset(L, -3);                          \
    }

    /* add context items to table */
    HTR_CHECK(DOCUMENT,  "document")
    HTR_CHECK(LINK,      "link")
    HTR_CHECK(IMAGE,     "image")
    HTR_CHECK(MEDIA,     "media")
    HTR_CHECK(EDITABLE,  "editable")
    HTR_CHECK(SCROLLBAR, "scrollbar")

#undef HTR_CHECK

    return 1;
}

static gboolean
webview_button_cb(GtkWidget *view, GdkEventButton *ev, widget_t *w)
{
    gint ret;
    lua_State *L = common.L;
    luaH_object_push(L, w->ref);
    luaH_modifier_table_push(L, ev->state);
    lua_pushinteger(L, ev->button);
    /* push webview hit test context */
    luaH_push_hit_test(L, WEBKIT_WEB_VIEW(view), w);

    switch (ev->type) {
      case GDK_2BUTTON_PRESS:
        ret = luaH_object_emit_signal(L, -4, "button-double-click", 3, 1);
        break;
      case GDK_BUTTON_RELEASE:
        ret = luaH_object_emit_signal(L, -4, "button-release", 3, 1);
        break;
      default:
        ret = luaH_object_emit_signal(L, -4, "button-press", 3, 1);
        break;
    }

    /* User responded with TRUE, so do not propagate event any further */
    if (ret && lua_toboolean(L, -1)) {
        lua_pop(L, ret + 1);
        return TRUE;
    }
    lua_pop(L, ret + 1);
    /* propagate event further */
    return FALSE;
}

static gboolean
webview_scroll_cb(GtkWidget *view, GdkEventScroll *ev, widget_t *w)
{
    double dx, dy;
    switch (ev->direction) {
        case GDK_SCROLL_UP:     dx =  0; dy = -1; break;
        case GDK_SCROLL_DOWN:   dx =  0; dy =  1; break;
        case GDK_SCROLL_LEFT:   dx = -1; dy =  0; break;
        case GDK_SCROLL_RIGHT:  dx =  1; dy =  0; break;
        case GDK_SCROLL_SMOOTH: gdk_event_get_scroll_deltas((GdkEvent*)ev, &dx, &dy); break;
        default: g_assert_not_reached();
    }

    lua_State *L = common.L;
    luaH_object_push(L, w->ref);
    luaH_modifier_table_push(L, ev->state);
    lua_pushnumber(L, dx);
    lua_pushnumber(L, dy);
    luaH_push_hit_test(L, WEBKIT_WEB_VIEW(view), w);

    gboolean ret = luaH_object_emit_signal(L, -5, "scroll", 4, 1);
    lua_pop(L, ret + 1);
    return ret;
}

static void
menu_item_cb(GtkAction *action, widget_t *w)
{
    lua_State *L = common.L;
    gpointer ref = g_object_get_data(G_OBJECT(action), "lua_callback");
    luaH_object_push(L, w->ref);
    luaH_object_push(L, ref);
    luaH_dofunction(L, 1, 0);
}

static void
hide_popup_cb(WebKitWebView* UNUSED(v), widget_t* UNUSED(w)) {
    GSList *iter;
    lua_State *L = common.L;

    /* dereference context menu items callback functions from the last
       context menu */
    /* context-menu-dismissed callback gets run before menu_item_cb(),
       causing the lua_callback to not exist if the refs belonging to
       the current context menu are freed during hide_popup_cb(). */
    if (last_popup.old_refs) {
        for (iter = last_popup.old_refs; iter; iter = iter->next)
            luaH_object_unref(L, iter->data);
        g_slist_free(last_popup.old_refs);
        last_popup.old_refs = NULL;
    }
}

static GSList *context_menu_actions;

static int
table_from_context_menu(lua_State *L, WebKitContextMenu *menu, widget_t *w)
{
    guint len = webkit_context_menu_get_n_items(menu);
    lua_createtable(L, len, 0);

    for (guint i = 1; i <= len; i++) {
        WebKitContextMenuItem *item = webkit_context_menu_get_item_at_position(menu, i-1);
        if (webkit_context_menu_item_is_separator(item))
            lua_pushboolean(L, TRUE);
        else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            GtkAction *action = webkit_context_menu_item_get_action(item);
            WebKitContextMenuAction stock_action = webkit_context_menu_item_get_stock_action(item);
            WebKitContextMenu *submenu = webkit_context_menu_item_get_submenu(item);
            lua_createtable(L, 2, 0);
            lua_pushstring(L, gtk_action_get_label(action));
#pragma GCC diagnostic pop
            lua_rawseti(L, -2, 1);
            if (submenu)
                table_from_context_menu(L, submenu, w);
            else if (stock_action == WEBKIT_CONTEXT_MENU_ACTION_CUSTOM) {
                context_menu_actions = g_slist_prepend(context_menu_actions, action);
                g_object_ref(G_OBJECT(action));
                lua_pushlightuserdata(L, action);
            } else
                lua_pushinteger(L, stock_action);
            lua_rawseti(L, -2, 2);
        }
        lua_rawseti(L, -2, i);
    }

    return 1;
}

static void
context_menu_from_table(lua_State *L, WebKitContextMenu *menu, widget_t *w)
{
    WebKitContextMenuItem *item;
    WebKitContextMenu *submenu;
    gpointer ref;
    const gchar *label;
    gint i, len = lua_objlen(L, -1);

    /* walk table and build context menu */
    for(i = 1; i <= len; i++) {
        lua_rawgeti(L, -1, i);
        if((lua_type(L, -1) == LUA_TTABLE) && (lua_objlen(L, -1) >= 2)) {
            lua_rawgeti(L, -1, 1);
            label = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_rawgeti(L, -1, 2);

            /* add new submenu */
            if(lua_type(L, -1) == LUA_TTABLE) {
                submenu = webkit_context_menu_new();
                item = webkit_context_menu_item_new_with_submenu(label,
                        submenu);
                webkit_context_menu_append(menu, item);
                context_menu_from_table(L, submenu, w);
                lua_pop(L, 1);

            /* add context menu item */
            } else if(lua_type(L, -1) == LUA_TFUNCTION) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
                GtkAction *action = gtk_action_new(label, label,
                        NULL, NULL);
                item = webkit_context_menu_item_new(action);
#pragma GCC diagnostic pop
                ref = luaH_object_ref(L, -1);
                last_popup.refs = g_slist_prepend(last_popup.refs, ref);
                g_object_set_data(G_OBJECT(action), "lua_callback", ref);

                webkit_context_menu_append(menu, item);
                g_signal_connect(action, "activate",
                        G_CALLBACK(menu_item_cb), (gpointer)w);
            } else if(lua_type(L, -1) == LUA_TNUMBER) {
                WebKitContextMenuAction stock_action = lua_tointeger(L, -1);
                g_assert_cmpint(stock_action, !=, WEBKIT_CONTEXT_MENU_ACTION_CUSTOM);
                item = webkit_context_menu_item_new_from_stock_action_with_label(stock_action, label);
                webkit_context_menu_append(menu, item);
                lua_pop(L, 1);
            } else if(lua_type(L, -1) == LUA_TLIGHTUSERDATA) {
                GtkAction *action = (void*)lua_topointer(L, -1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
                item = webkit_context_menu_item_new(action);
#pragma GCC diagnostic pop
                webkit_context_menu_append(menu, item);
                lua_pop(L, 1);
            }

        /* add separator if encounters `true` */
        } else if(lua_type(L, -1) == LUA_TBOOLEAN && lua_toboolean(L, -1)) {
            item = webkit_context_menu_item_new_separator();
            webkit_context_menu_append(menu, item);
        }
        lua_pop(L, 1);
    }
}

static gboolean
context_menu_cb(WebKitWebView* UNUSED(v), WebKitContextMenu *menu,
        GdkEvent* UNUSED(e), WebKitHitTestResult* UNUSED(htr), widget_t *w)
{
    lua_State *L = common.L;
    g_assert(!context_menu_actions);
    table_from_context_menu(L, menu, w);

    luaH_object_push(L, w->ref);
    lua_pushvalue(L, -2);
    luaH_object_emit_signal(L, -2, "populate-popup", 1, 0);
    lua_pop(L, 1);

    last_popup.old_refs = last_popup.refs;
    last_popup.refs = NULL;
    webkit_context_menu_remove_all(menu);
    context_menu_from_table(L, menu, w);

    g_slist_free_full(context_menu_actions, g_object_unref);
    context_menu_actions = NULL;

    lua_pop(L, 1);

    return FALSE;
}

static void
webview_destructor(widget_t *w)
{
    webview_data_t *d = w->data;

    g_idle_remove_by_data(w);

    g_assert(d->ipc);
    ipc_endpoint_decref(d->ipc);
    d->ipc = NULL;

    g_ptr_array_remove(globalconf.webviews, w);
    g_free(d->uri);
    g_free(d->hover);
    g_object_unref(G_OBJECT(d->user_content));
    if (d->cert)
        g_object_unref(G_OBJECT(d->cert));

    g_slice_free(webview_data_t, d);
}

void
luakit_uri_scheme_request_cb(WebKitURISchemeRequest *request, const gchar *scheme)
{
    const gchar *uri = webkit_uri_scheme_request_get_uri(request);

    WebKitWebView *view = webkit_uri_scheme_request_get_web_view(request);
    if (!view)
        return;
    widget_t *w = GOBJECT_TO_LUAKIT_WIDGET(view);

    lua_State *L = common.L;

    g_assert(scheme);
    gchar *sig = g_strconcat("scheme-request::", scheme, NULL);
    luaH_object_push(L, w->ref);
    lua_pushstring(L, uri);
    luaH_request_push_uri_scheme_request(L, request);
    luaH_object_emit_signal(L, -3, sig, 2, 0);
    lua_pop(L, 1);
    g_free(sig);
}

gboolean
webview_crashed_cb(WebKitWebView *UNUSED(view), widget_t *w)
{
    /* Give webview a new disconnected IPC endpoint */
    webview_data_t *d = w->data;
    d->ipc = ipc_endpoint_new("UI");

    /* Emit 'crashed' signal on web view */
    lua_State *L = common.L;
    luaH_object_push(L, w->ref);
    luaH_object_emit_signal(L, -1, "crashed", 0, 0);
    lua_pop(L, 1);

    return FALSE;
}

void
webview_connect_to_endpoint(widget_t *w, ipc_endpoint_t *ipc)
{
    g_assert(w->info->tok == L_TK_WEBVIEW);
    g_assert(ipc);

    /* Replace old endpoint with new, sendinq queued data */
    webview_data_t *d = w->data;
    d->ipc = ipc_endpoint_replace(d->ipc, ipc);

    lua_State *L = common.L;

    /* Emit 'web-extension-created' signal on luakit if necessary */
    /* TODO: move signal emission to somewhere else */
    if (!ipc->creation_notified) {
        ipc->creation_notified = TRUE;

        gint top = lua_gettop(L);
        luaH_object_push(L, w->ref);
        lua_class_t *luakit_class = luakit_lib_get_luakit_class();
        luaH_class_emit_signal(L, luakit_class, "web-extension-created", 1, 0);
        lua_settop(L, top);
    }

    /* Emit 'web-extension-loaded' signal on webview */
    luaH_object_push(L, w->ref);
    if (!lua_isnil(L, -1))
        luaH_object_emit_signal(L, -1, "web-extension-loaded", 0, 0);
    lua_pop(L, 1);
}

ipc_endpoint_t *
webview_get_endpoint(widget_t *w)
{
    g_assert(w->info->tok == L_TK_WEBVIEW);
    webview_data_t *d = w->data;
    return d->ipc;
}

void
webview_set_web_process_id(widget_t *w, pid_t pid)
{
    webview_data_t *d = w->data;
    d->web_process_id = pid;
}

widget_t *
widget_webview(lua_State *L, widget_t *w, luakit_token_t UNUSED(token))
{
    w->index = luaH_webview_index;
    w->newindex = luaH_webview_newindex;
    w->destructor = webview_destructor;

    /* create private webview data struct */
    webview_data_t *d = g_slice_new0(webview_data_t);
    d->widget = w;
    w->data = d;

    /* Determine whether webview should be ephemeral */
    /* Lua stack: [{class meta}, {props}, new widget, "type", "webview"] */
    gint prop_tbl_idx = luaH_absindex(L, -4);
    g_assert(lua_istable(L, prop_tbl_idx));
    lua_pushstring(L, "private");
    lua_rawget(L, prop_tbl_idx);
    gboolean private = lua_type(L, -1) == LUA_TNIL ? FALSE : lua_toboolean(L, -1);
    lua_pop(L, 1);
    d->private = private;

    /* keep a list of all webview widgets */
    if (!globalconf.webviews)
        globalconf.webviews = g_ptr_array_new();

    if (!globalconf.stylesheets)
        globalconf.stylesheets = g_ptr_array_new();
    d->stylesheets = NULL;

    /* Set web process limits if not already set */
    web_context_init_finish();

    /* create widgets */
    d->user_content = webkit_user_content_manager_new();
    d->view = g_object_new(WEBKIT_TYPE_WEB_VIEW,
                 "web-context", web_context_get(),
                 "is-ephemeral", d->private,
                 "user-content-manager", d->user_content,
                 related_view ? "related-view" : NULL, related_view,
                 NULL);
    d->inspector = webkit_web_view_get_inspector(d->view);

    d->is_committed = FALSE;

    /* Create a new endpoint with one ref (this webview) */
    d->ipc = ipc_endpoint_new("UI");

    w->widget = GTK_WIDGET(d->view);

    /* insert data into global tables and arrays */
    g_ptr_array_add(globalconf.webviews, w);

    g_object_connect(G_OBJECT(d->view),
      LUAKIT_WIDGET_SIGNAL_COMMON(w)
      "signal::button-press-event",                   G_CALLBACK(webview_button_cb),            w,
      "signal::button-release-event",                 G_CALLBACK(webview_button_cb),            w,
      "signal::scroll-event",                         G_CALLBACK(webview_scroll_cb),            w,
      "signal::create",                               G_CALLBACK(create_cb),                    w,
      "signal::web-process-crashed",                  G_CALLBACK(webview_crashed_cb),           w,
      "signal::draw",                                 G_CALLBACK(expose_cb),                    w,
      "signal::mouse-target-changed",                 G_CALLBACK(mouse_target_changed_cb),      w,
      "signal::key-press-event",                      G_CALLBACK(key_press_cb),                 w,
      "signal::decide-policy",                        G_CALLBACK(decide_policy_cb),             w,
      "signal::notify",                               G_CALLBACK(notify_cb),                    w,
      "signal::load-changed",                         G_CALLBACK(load_changed_cb),              w,
      "signal::load-failed",                          G_CALLBACK(load_failed_cb),               w,
      "signal::load-failed-with-tls-errors",          G_CALLBACK(load_failed_tls_cb),           w,
      "signal::context-menu",                         G_CALLBACK(context_menu_cb),              w,
      "signal::context-menu-dismissed",               G_CALLBACK(hide_popup_cb),                w,
      "signal::notify::favicon",                      G_CALLBACK(favicon_cb),                   w,
      "signal::notify::uri",                          G_CALLBACK(uri_cb),                       w,
      "signal::authenticate",                         G_CALLBACK(session_authenticate),         w,
      "signal::permission-request",                   G_CALLBACK(permission_request_cb),        w,
      NULL);

    g_object_connect(G_OBJECT(webkit_web_view_get_find_controller(d->view)),
      "signal::found-text",                           G_CALLBACK(found_text_cb),                w,
      "signal::failed-to-find-text",                  G_CALLBACK(failed_to_find_text_cb),       w,
      NULL);

    g_object_connect(G_OBJECT(d->view),
      "signal::parent-set",                           G_CALLBACK(parent_set_cb),                w,
      NULL);

    g_object_connect(G_OBJECT(d->inspector),
      "signal::attach",                               G_CALLBACK(inspector_attach_window_cb),   w,
      "signal::bring-to-front",                       G_CALLBACK(inspector_show_window_cb),     w,
      "signal::closed",                               G_CALLBACK(inspector_close_window_cb),    w,
      "signal::detach",                               G_CALLBACK(inspector_detach_window_cb),   w,
      "signal::open-window",                          G_CALLBACK(inspector_open_window_cb),     w,
      NULL);

    /* show widgets */
    gtk_widget_show(GTK_WIDGET(d->view));

    return w;
}

// vim: ft=c:et:sw=4:ts=8:sts=4:tw=80
