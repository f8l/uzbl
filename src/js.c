#include "js.h"
#include "uzbl-core.h"
#include "util.h"

#include <stdlib.h>

static JSValueRef
uzbl_js_evaluate(JSGlobalContextRef   jsctx,
                 const gchar         *script,
                 const gchar         *path,
                 GError             **err)
{
    JSObjectRef globalobject = JSContextGetGlobalObject (jsctx);
    JSValueRef js_exc = NULL;

    JSStringRef js_script = JSStringCreateWithUTF8CString (script);
    JSStringRef js_file = JSStringCreateWithUTF8CString (path);
    JSValueRef js_result = JSEvaluateScript (jsctx, js_script, globalobject, js_file, 0, &js_exc);

    if (js_exc) {
        JSObjectRef exc = JSValueToObject (jsctx, js_exc, NULL);

        gchar *file = uzbl_js_to_string (jsctx, uzbl_js_get (jsctx, exc, "sourceURL"));
        gchar *line = uzbl_js_to_string (jsctx, uzbl_js_get (jsctx, exc, "line"));
        gchar *msg = uzbl_js_to_string (jsctx, exc);

        g_set_error (err, UZBL_JS_ERROR, UZBL_JS_ERROR_EVAL,
                     "%s:%s: %s",
                     file, line, msg);

        g_free (file);
        g_free (line);
        g_free (msg);
    }

    JSStringRelease (js_file);
    JSStringRelease (js_script);

    return js_result;
}

static JSGlobalContextRef
uzbl_js_get_context(UzblJSContext context)
{
    JSGlobalContextRef jsctx;

    switch (context) {
    case JSCTX_UZBL:
        jsctx = uzbl.state.jscontext;
        JSGlobalContextRetain (jsctx);
        break;
    case JSCTX_CLEAN:
        jsctx = JSGlobalContextCreate (NULL);
        break;
    case JSCTX_PAGE:
    default:
        g_warning ("Invalid javascript context");
    }

    return jsctx;
}

static void
web_view_run_javascript_cb(GObject      *source,
                           GAsyncResult *result,
                           gpointer      data)
{
    GTask *task = G_TASK (data);
    GError *err = NULL;
    JSGlobalContextRef context;
    JSValueRef value;
    WebKitJavascriptResult *jsr;
    jsr = webkit_web_view_run_javascript_finish (WEBKIT_WEB_VIEW (source),
                                                result, &err);
    if (err) {
        g_debug ("JS error: %s", err->message);
        g_task_return_error (task, err);
        g_object_unref (task);
        return;
    }

    context = webkit_javascript_result_get_global_context (jsr);
    value = webkit_javascript_result_get_value (jsr);
    gchar *result_utf8 = uzbl_js_to_string (context, value);

    g_task_return_pointer (task, result_utf8, g_free);
}

/* =========================== PUBLIC API =========================== */
void
uzbl_js_init ()
{
    uzbl.state.jscontext = JSGlobalContextCreate (NULL);

    JSObjectRef global = JSContextGetGlobalObject (uzbl.state.jscontext);
    JSObjectRef uzbl_obj = JSObjectMake (uzbl.state.jscontext, NULL, NULL);

    uzbl_js_set (uzbl.state.jscontext,
        global, "uzbl", uzbl_obj,
        kJSPropertyAttributeReadOnly | kJSPropertyAttributeDontDelete);
}

GQuark
uzbl_js_error_quark ()
{
    return g_quark_from_static_string ("uzbl-js-error-quark");
}

JSValueRef
uzbl_js_get (JSContextRef ctx, JSObjectRef obj, const gchar *prop)
{
    JSStringRef js_prop;
    JSValueRef js_prop_val;

    js_prop = JSStringCreateWithUTF8CString (prop);
    js_prop_val = JSObjectGetProperty (ctx, obj, js_prop, NULL);

    JSStringRelease (js_prop);

    return js_prop_val;
}

void
uzbl_js_set (JSContextRef ctx, JSObjectRef obj, const gchar *prop, JSValueRef val, JSPropertyAttributes props)
{
    JSStringRef name = JSStringCreateWithUTF8CString (prop);

    JSObjectSetProperty (ctx, obj, name, val, props, NULL);

    JSStringRelease (name);
}

gchar *
uzbl_js_to_string (JSContextRef ctx, JSValueRef val)
{
    JSStringRef str = JSValueToStringCopy (ctx, val, NULL);
    gchar *result = uzbl_js_extract_string (str);
    JSStringRelease (str);

    return result;
}

gchar *
uzbl_js_extract_string (JSStringRef str)
{
    size_t max_size = JSStringGetMaximumUTF8CStringSize (str);
    gchar *gstr = (gchar *)malloc (max_size * sizeof (gchar));
    JSStringGetUTF8CString (str, gstr, max_size);

    return gstr;
}


JSObjectRef
uzbl_js_object (JSContextRef ctx, const gchar *prop)
{
    JSObjectRef global = JSContextGetGlobalObject (ctx);
    JSValueRef val = uzbl_js_get (ctx, global, prop);

    return JSValueToObject (ctx, val, NULL);
}

void
uzbl_js_run_string_async (UzblJSContext        context,
                          const gchar         *script,
                          GAsyncReadyCallback  callback,
                          gpointer             data)
{
    GTask *task = g_task_new (NULL, NULL, callback, data);
    gchar *result_utf8 = NULL;

    if (context == JSCTX_PAGE) {
        webkit_web_view_run_javascript (uzbl.gui.web_view, script, NULL,
                                        web_view_run_javascript_cb, task);
        return;
    }

    GError *err = NULL;
    JSGlobalContextRef jsctx = uzbl_js_get_context (context);
    JSValueRef result = uzbl_js_evaluate (jsctx, script, "(uzbl command)", &err);
    if (err != NULL) {
        g_debug ("Exception occured while executing script: %s", err->message);
        g_task_return_error (task, err);
        g_object_unref (task);
        return;
    }
    if (result && !JSValueIsUndefined (jsctx, result)) {
        result_utf8 = uzbl_js_to_string (jsctx, result);
    }

    JSGlobalContextRelease (jsctx);
    g_task_return_pointer (task, result_utf8, g_free);
    g_object_unref (task);
}

void
uzbl_js_run_file_async (UzblJSContext        context,
                        const gchar         *path,
                        GArray              *argv,
                        GAsyncReadyCallback  callback,
                        gpointer             data)
{
    GTask *task = g_task_new (NULL, NULL, callback, data);

    gchar *script;
    gchar *result_utf8 = NULL;

    GIOChannel *chan = g_io_channel_new_file (path, "r", NULL);
    if (chan) {
        gsize len;
        g_io_channel_read_to_end (chan, &script, &len, NULL);
        g_io_channel_unref (chan);
    }

    uzbl_debug ("External JavaScript file loaded: %s\n", path);
    guint i;
    for (i = argv->len; 0 != i; --i) {
        const gchar *arg = argv_idx (argv, i - 1);
        gchar *needle = g_strdup_printf ("%%%d", i);

        gchar *new_file_contents = str_replace (needle, arg ? arg : "", script);

        g_free (needle);
        g_free (script);
        script = new_file_contents;
    }

    if (context == JSCTX_PAGE) {
        webkit_web_view_run_javascript (uzbl.gui.web_view, script, NULL,
                                        web_view_run_javascript_cb, task);
        return;
    }

    GError *err = NULL;
    JSGlobalContextRef jsctx = uzbl_js_get_context (context);
    JSValueRef result = uzbl_js_evaluate (jsctx, script, path, &err);
    if (err != NULL) {
        g_debug ("Exception occured while executing script: %s", err->message);
        g_task_return_error (task, err);
        g_object_unref (task);
        return;
    }
    if (result && !JSValueIsUndefined (jsctx, result)) {
        result_utf8 = uzbl_js_to_string (jsctx, result);
    }

    JSGlobalContextRelease (jsctx);
    g_task_return_pointer (task, result_utf8, g_free);
    g_object_unref (task);
}

gchar *
uzbl_js_run_finish (GObject       *source,
                    GAsyncResult  *result,
                    GError       **error)
{
    UZBL_UNUSED (source);
    GTask *task = G_TASK (result);
    return g_task_propagate_pointer (task, error);
}
