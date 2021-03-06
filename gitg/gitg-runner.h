#ifndef __GITG_RUNNER_H__
#define __GITG_RUNNER_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define GITG_TYPE_RUNNER			(gitg_runner_get_type ())
#define GITG_RUNNER(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GITG_TYPE_RUNNER, GitgRunner))
#define GITG_RUNNER_CONST(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GITG_TYPE_RUNNER, GitgRunner const))
#define GITG_RUNNER_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GITG_TYPE_RUNNER, GitgRunnerClass))
#define GITG_IS_RUNNER(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GITG_TYPE_RUNNER))
#define GITG_IS_RUNNER_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GITG_TYPE_RUNNER))
#define GITG_RUNNER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GITG_TYPE_RUNNER, GitgRunnerClass))

typedef struct _GitgRunner			GitgRunner;
typedef struct _GitgRunnerClass		GitgRunnerClass;
typedef struct _GitgRunnerPrivate	GitgRunnerPrivate;

typedef enum
{
	GITG_RUNNER_ERROR_EXIT
} GitgRunnerError;

struct _GitgRunner {
	GObject parent;
	
	GitgRunnerPrivate *priv;
};

struct _GitgRunnerClass {
	GObjectClass parent_class;
	
	/* signals */
	void (* begin_loading) (GitgRunner *runner);
	void (* update) (GitgRunner *runner, gchar **buffer);
	void (* end_loading) (GitgRunner *runner);
};

GType gitg_runner_get_type (void) G_GNUC_CONST;
GitgRunner *gitg_runner_new(guint buffer_size);
GitgRunner *gitg_runner_new_synchronized(guint buffer_size);

guint gitg_runner_get_buffer_size(GitgRunner *runner);

gboolean gitg_runner_run_stream(GitgRunner *runner, GInputStream *stream, GError **error);

gboolean gitg_runner_run_with_arguments(GitgRunner *runner, gchar const **argv, gchar const *wd, gchar const *input, GError **error);
gboolean gitg_runner_run_working_directory(GitgRunner *runner, gchar const **argv, gchar const *wd, GError **error);
gboolean gitg_runner_run(GitgRunner *runner, gchar const **argv, GError **error);
gboolean gitg_runner_running(GitgRunner *runner);

gint gitg_runner_get_exit_status(GitgRunner *runner);
void gitg_runner_cancel(GitgRunner *runner);

GQuark gitg_runner_error_quark();

G_END_DECLS

#endif /* __GITG_RUNNER_H__ */
