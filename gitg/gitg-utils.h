#ifndef __GITG_UTILS_H__
#define __GITG_UTILS_H__

#include <glib.h>
#include <gtksourceview/gtksourcelanguagemanager.h>
#include <gio/gio.h>

#include "gitg-repository.h"
#include "gitg-revision.h"

void gitg_utils_sha1_to_hash(gchar const *sha, gchar *hash);
void gitg_utils_hash_to_sha1(gchar const *hash, gchar *sha);

gchar *gitg_utils_sha1_to_hash_new(gchar const *sha);
gchar *gitg_utils_hash_to_sha1_new(gchar const *hash);

gchar *gitg_utils_find_git(gchar const *path);
gchar *gitg_utils_dot_git_path(gchar const *path);

gboolean gitg_utils_export_files(GitgRepository *repository, GitgRevision *revision,
gchar const *todir, gchar * const *paths);

gchar *gitg_utils_convert_utf8(gchar const *str, gssize size);

guint gitg_utils_hash_hash(gconstpointer v);
gboolean gitg_utils_hash_equal(gconstpointer a, gconstpointer b);
gint gitg_utils_null_length(gconstpointer *ptr);

gchar *gitg_utils_get_content_type(GFile *file);
GtkSourceLanguage *gitg_utils_get_language(gchar const *content_type);
gboolean gitg_utils_can_display_content_type(gchar const *content_type);
gchar *gitg_utils_guess_content_type(GtkTextBuffer *buffer);

gint gitg_utils_sort_names(gchar const *s1, gchar const *s2);

void gitg_utils_menu_position_under_widget(GtkMenu *menu, gint *x, gint *y,	gboolean *push_in, gpointer user_data);
void gitg_utils_menu_position_under_tree_view(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data);

gchar *gitg_utils_get_monospace_font_name();
void gitg_utils_set_monospace_font(GtkWidget *widget);
					   
#endif /* __GITG_UTILS_H__ */
