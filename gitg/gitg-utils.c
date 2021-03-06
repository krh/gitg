#include <string.h>
#include <glib.h>
#include <gconf/gconf-client.h>

#include "gitg-utils.h"

inline static guint8
atoh(gchar c)
{
	if (c >= 'a')
		return c - 'a' + 10;
	if (c >= 'A')
		return c - 'A' + 10;
	
	return c - '0';
}

void
gitg_utils_sha1_to_hash(gchar const *sha, gchar *hash)
{
	int i;

	for (i = 0; i < 20; ++i)
	{
		gchar h = atoh(*(sha++)) << 4;
		hash[i] = h | atoh(*(sha++));
	}
}

void
gitg_utils_hash_to_sha1(gchar const *hash, gchar *sha)
{
	char const *repr = "0123456789abcdef";
	int i;
	int pos = 0;

	for (i = 0; i < 20; ++i)
	{
		sha[pos++] = repr[(hash[i] >> 4) & 0x0f];
		sha[pos++] = repr[(hash[i] & 0x0f)];
	}
}

gchar *
gitg_utils_hash_to_sha1_new(gchar const *hash)
{
	gchar *ret = g_new(gchar, 41);
	gitg_utils_hash_to_sha1(hash, ret);
	
	ret[40] = '\0';
	return ret;
}

gchar *
gitg_utils_sha1_to_hash_new(gchar const *sha1)
{
	gchar *ret = g_new(gchar, 20);
	gitg_utils_sha1_to_hash(sha1, ret);
	
	return ret;
}

static gchar *
find_dot_git(gchar *path)
{
	while (strcmp(path, ".") != 0 && strcmp(path, "/") != 0)
	{
		gchar *res = g_build_filename(path, ".git", NULL);
		
		if (g_file_test(res, G_FILE_TEST_IS_DIR))
		{
			g_free(res);
			return path;
		}
		
		gchar *tmp = g_path_get_dirname(path);
		g_free(path);
		path = tmp;
		
		g_free(res);
	}
	
	return NULL;
}

gchar *
gitg_utils_find_git(gchar const *path)
{
	gchar const *find = G_DIR_SEPARATOR_S ".git";
	gchar *dir;
	
	if (strstr(path, find) == path + strlen(path) - strlen(find))
		dir = g_strndup(path, strlen(path) - strlen(find));
	else
		dir = g_strdup(path);
	
	return find_dot_git(dir);
}

gchar *
gitg_utils_dot_git_path(gchar const *path)
{
	gchar const *find = G_DIR_SEPARATOR_S ".git";
	
	if (strstr(path, find) == path + strlen(path) - strlen(find))
		return g_strdup(path);
	else
		return g_build_filename(path, ".git", NULL);
}

static void
append_escape(GString *gstr, gchar const *item)
{
	gchar *escape = g_shell_quote(item);
	
	g_string_append_printf(gstr, " %s", escape);
}

gboolean 
gitg_utils_export_files(GitgRepository *repository, GitgRevision *revision,
gchar const *todir, gchar * const *paths)
{	
	GString *gstr = g_string_new("sh -c \"git --git-dir");
	
	// Append the git path
	gchar *gitpath = gitg_utils_dot_git_path(gitg_repository_get_path(repository));
	append_escape(gstr, gitpath);
	g_free(gitpath);

	// Append the revision
	gchar *sha = gitg_revision_get_sha1(revision);
	g_string_append_printf(gstr, " archive --format=tar %s", sha);
	g_free(sha);
	
	// Append the files
	while (*paths)
	{
		append_escape(gstr, *paths);
		paths++;
	}

	g_string_append(gstr, " | tar -xC");
	append_escape(gstr, todir);
	g_string_append(gstr, "\"");
	
	GError *error = NULL;
	gint status;

	gboolean ret = g_spawn_command_line_sync(gstr->str, NULL, NULL, &status, &error);
	
	if (!ret)
	{
		g_warning("Export failed:\n%s\n%s", gstr->str, error->message);
		g_error_free(error);
	}

	g_string_free(gstr, TRUE);
	return ret;
}

gchar *
convert_fallback(gchar const *text, gssize size, gchar const *fallback)
{
	gchar *res;
	gsize read, written;
	GString *str = g_string_new("");
	
	while ((res = g_convert(text, size, "UTF-8", "ASCII", &read, &written, NULL))
			== NULL) {
		res = g_convert(text, read, "UTF-8", "ASCII", NULL, NULL, NULL);
		str = g_string_append(str, res);
		
		str = g_string_append(str, fallback);
		text = text + read + 1;
		size = size - read;
	}
	
	str = g_string_append(str, res);
	g_free(res);
	
	res = str->str;
	g_string_free(str, FALSE);
	return res;
}

gchar *
gitg_utils_convert_utf8(gchar const *str, gssize size)
{
	static gchar *encodings[] = {
		"ISO-8859-15",
		"ASCII"
	};
	
	if (g_utf8_validate(str, size, NULL))
		return g_strndup(str, size);
	
	int i;
	for (i = 0; i < sizeof(encodings) / sizeof(gchar *); ++i)
	{
		gsize read;
		gsize written;

		gchar *ret = g_convert(str, size, "UTF-8", encodings[i], &read, &written, NULL);
		
		if (ret)
			return ret;
	}
	
	return convert_fallback(str, size, "?");
}

guint
gitg_utils_hash_hash(gconstpointer v)
{
	/* 31 bit hash function, copied from g_str_hash */
	const signed char *p = v;
	guint32 h = *p;
	int i;
	
	for (i = 1; i < 20; ++i)
		h = (h << 5) - h + p[i];

	return h;
}

gboolean 
gitg_utils_hash_equal(gconstpointer a, gconstpointer b)
{
	return memcmp(a, b, 20) == 0;
}

gint
gitg_utils_null_length(gconstpointer *ptr)
{
	gint ret = 0;
	
	while (*ptr++)
		++ret;
	
	return ret;
}

gchar *
gitg_utils_get_content_type(GFile *file)
{
	GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
	
	if (!info || !g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE))
		return NULL;
	
	gchar *content_type = g_strdup(g_file_info_get_content_type(info));
	g_object_unref(info);

	return content_type;
}

gboolean
gitg_utils_can_display_content_type(gchar const *content_type)
{
	return g_content_type_is_a(content_type, "text/plain") || 
		   g_content_type_equals(content_type, "application/octet-stream");
}

GtkSourceLanguage *
gitg_utils_get_language(gchar const *content_type)
{
	if (!gitg_utils_can_display_content_type(content_type))
		return NULL;
	
	gchar *mime = g_content_type_get_mime_type(content_type);
	GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default();
	
	gchar const * const *ids = gtk_source_language_manager_get_language_ids(manager);
	gchar const *ptr;
	GtkSourceLanguage *ret;
	
	while ((ptr = *ids++))
	{
		ret = gtk_source_language_manager_get_language(manager, ptr);
		gchar **mime_types = gtk_source_language_get_mime_types(ret);
		gchar **types = mime_types;
		gchar *m;
		
		if (types)
		{
			while ((m = *types++))
			{
				if (strcmp(mime, m) == 0)
				{
					g_free(mime);
					g_strfreev(mime_types);
					return ret;
				}
			}
		
			g_strfreev(mime_types);
		}

		ret = NULL;
	}
	
	g_free(mime);
	return NULL;
}

gint 
gitg_utils_sort_names(gchar const *s1, gchar const *s2)
{
	if (s1 == NULL)
		return -1;
	
	if (s2 == NULL)
		return 1;

	gchar *c1 = s1 ? g_utf8_casefold(s1, -1) : NULL;
	gchar *c2 = s2 ? g_utf8_casefold(s2, -1) : NULL;
	
	gint ret = g_utf8_collate(c1, c2);
	
	g_free(c1);
	g_free(c2);
	
	return ret;
}

gchar *
gitg_utils_guess_content_type(GtkTextBuffer *buffer)
{
	GtkTextIter start;
	GtkTextIter end;
	
	gtk_text_buffer_get_start_iter(buffer, &start);
	end = start;
	
	gtk_text_iter_forward_chars(&end, 256);
	gchar *data = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

	gchar *content_type = g_content_type_guess(NULL, data, strlen(data), NULL);
	g_free(data);
	
	return content_type;
}

/* Copied from gedit-utils.c */
void
gitg_utils_menu_position_under_widget (GtkMenu  *menu,
					gint     *x,
					gint     *y,
					gboolean *push_in,
					gpointer  user_data)
{
	GtkWidget *w = GTK_WIDGET (user_data);
	GtkRequisition requisition;

	gdk_window_get_origin (w->window, x, y);
	gtk_widget_size_request (GTK_WIDGET (menu), &requisition);

	if (gtk_widget_get_direction (w) == GTK_TEXT_DIR_RTL)
	{
		*x += w->allocation.x + w->allocation.width - requisition.width;
	}
	else
	{
		*x += w->allocation.x;
	}

	*y += w->allocation.y + w->allocation.height;

	*push_in = TRUE;
}

void
gitg_utils_menu_position_under_tree_view (GtkMenu  *menu,
					   gint     *x,
					   gint     *y,
					   gboolean *push_in,
					   gpointer  user_data)
{
	GtkTreeView *tree = GTK_TREE_VIEW (user_data);
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	
	model = gtk_tree_view_get_model (tree);
	g_return_if_fail (model != NULL);

	selection = gtk_tree_view_get_selection (tree);
	g_return_if_fail (selection != NULL);

	if (gtk_tree_selection_get_selected (selection, NULL, &iter))
	{
		GtkTreePath *path;
		GdkRectangle rect;

		gdk_window_get_origin (GTK_WIDGET (tree)->window, x, y);
			
		path = gtk_tree_model_get_path (model, &iter);
		gtk_tree_view_get_cell_area (tree, path,
					     gtk_tree_view_get_column (tree, 0), /* FIXME 0 for RTL ? */
					     &rect);
		gtk_tree_path_free (path);
		
		*x += rect.x;
		*y += rect.y + rect.height;
		
		if (gtk_widget_get_direction (GTK_WIDGET (tree)) == GTK_TEXT_DIR_RTL)
		{
			GtkRequisition requisition;
			gtk_widget_size_request (GTK_WIDGET (menu), &requisition);
			*x += rect.width - requisition.width;
		}
	}
	else
	{
		/* no selection -> regular "under widget" positioning */
		gitg_utils_menu_position_under_widget (menu,
							x, y, push_in,
							tree);
	}
}

gchar *
gitg_utils_get_monospace_font_name()
{
	GConfClient *client = gconf_client_get_default();
	gchar *name = gconf_client_get_string(client, "/desktop/gnome/interface/monospace_font_name", NULL);
	
	g_object_unref(client);
	
	return name;
}

void 
gitg_utils_set_monospace_font(GtkWidget *widget)
{
	gchar *name = gitg_utils_get_monospace_font_name();
	
	if (name)
	{
		PangoFontDescription *description = pango_font_description_from_string(name);
		
		if (description)
		{
			gtk_widget_modify_font(widget, description);
			pango_font_description_free(description);
		}
	}
	
	g_free(name);
}
