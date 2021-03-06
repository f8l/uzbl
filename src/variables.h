#ifndef UZBL_VARIABLES_H
#define UZBL_VARIABLES_H

#include <glib.h>
#include <gio/gio.h>

gboolean
uzbl_variables_is_valid (const gchar *name);

gboolean
uzbl_variables_set (const gchar *name, gchar *val);
gboolean
uzbl_variables_toggle (const gchar *name, GArray *values);

gchar *
uzbl_variables_expand (const gchar *str);
void
uzbl_variables_expand_async (const gchar         *str,
                             GAsyncReadyCallback  callback,
                             gpointer             data);
gchar*
uzbl_variables_expand_finish (GObject       *source,
                              GAsyncResult  *res,
                              GError       **error);

gchar *
uzbl_variables_get_string (const gchar *name);
int
uzbl_variables_get_int (const gchar *name);
unsigned long long
uzbl_variables_get_ull (const gchar *name);
gdouble
uzbl_variables_get_double (const gchar *name);

void
uzbl_variables_dump ();
void
uzbl_variables_dump_events ();

#endif
