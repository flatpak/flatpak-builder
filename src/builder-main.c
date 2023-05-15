/*
 * Copyright © 2015 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include "libglnx.h"

#include "builder-flatpak-utils.h"
#include "builder-manifest.h"
#include "builder-utils.h"
#include "builder-git.h"

static gboolean opt_verbose;
static gboolean opt_version;
static gboolean opt_run;
static gboolean opt_disable_cache;
static gboolean opt_disable_tests;
static gboolean opt_disable_rofiles;
static gboolean opt_download_only;
static gboolean opt_no_shallow_clone;
static gboolean opt_bundle_sources;
static gboolean opt_build_only;
static gboolean opt_finish_only;
static gboolean opt_export_only;
static gboolean opt_show_deps;
static gboolean opt_show_manifest;
static gboolean opt_disable_download;
static gboolean opt_disable_updates;
static gboolean opt_ccache;
static gboolean opt_require_changes;
static gboolean opt_keep_build_dirs;
static gboolean opt_delete_build_dirs;
static gboolean opt_force_clean;
static gboolean opt_allow_missing_runtimes;
static gboolean opt_sandboxed;
static gboolean opt_rebuild_on_sdk_change;
static gboolean opt_skip_if_unchanged;
static gboolean opt_install;
static char *opt_state_dir;
static char *opt_from_git;
static char *opt_from_git_branch;
static char *opt_stop_at;
static char *opt_build_shell;
static char *opt_arch;
static char *opt_default_branch;
static char *opt_repo;
static char *opt_subject;
static char *opt_body;
static char *opt_collection_id = NULL;
static int opt_token_type = -1;
static char *opt_gpg_homedir;
static char **opt_key_ids;
static char **opt_sources_dirs;
static char **opt_sources_urls;
static char **opt_add_tags;
static char **opt_remove_tags;
static int opt_jobs;
static char *opt_mirror_screenshots_url;
static char **opt_install_deps_from;
static gboolean opt_install_deps_only;
static gboolean opt_user;
static char *opt_installation;
static gboolean opt_log_session_bus;
static gboolean opt_log_system_bus;
static gboolean opt_yes;
static gint64 opt_source_date_epoch = -1;

static GOptionEntry entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "version", 0, 0, G_OPTION_ARG_NONE, &opt_version, "Print version information and exit", NULL },
  { "arch", 0, 0, G_OPTION_ARG_STRING, &opt_arch, "Architecture to build for (must be host compatible)", "ARCH" },
  { "default-branch", 0, 0, G_OPTION_ARG_STRING, &opt_default_branch, "Change the default branch", "BRANCH" },
  { "add-tag", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_add_tags, "Add a tag to the build", "TAG"},
  { "remove-tag", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_remove_tags, "Remove a tag from the build", "TAG"},
  { "run", 0, 0, G_OPTION_ARG_NONE, &opt_run, "Run a command in the build directory (see --run --help)", NULL },
  { "ccache", 0, 0, G_OPTION_ARG_NONE, &opt_ccache, "Use ccache", NULL },
  { "disable-cache", 0, 0, G_OPTION_ARG_NONE, &opt_disable_cache, "Disable cache lookups", NULL },
  { "disable-tests", 0, 0, G_OPTION_ARG_NONE, &opt_disable_tests, "Don't run tests", NULL },
  { "disable-rofiles-fuse", 0, 0, G_OPTION_ARG_NONE, &opt_disable_rofiles, "Disable rofiles-fuse use", NULL },
  { "disable-download", 0, 0, G_OPTION_ARG_NONE, &opt_disable_download, "Don't download any new sources", NULL },
  { "disable-updates", 0, 0, G_OPTION_ARG_NONE, &opt_disable_updates, "Only download missing sources, never update to latest vcs version", NULL },
  { "download-only", 0, 0, G_OPTION_ARG_NONE, &opt_download_only, "Only download sources, don't build", NULL },
  { "bundle-sources", 0, 0, G_OPTION_ARG_NONE, &opt_bundle_sources, "Bundle module sources as runtime", NULL },
  { "extra-sources", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_sources_dirs, "Add a directory of sources specified by SOURCE-DIR, multiple uses of this option possible", "SOURCE-DIR"},
  { "extra-sources-url", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_sources_urls, "Add a url of sources specified by SOURCE-URL multiple uses of this option possible", "SOURCE-URL"},
  { "build-only", 0, 0, G_OPTION_ARG_NONE, &opt_build_only, "Stop after build, don't run clean and finish phases", NULL },
  { "finish-only", 0, 0, G_OPTION_ARG_NONE, &opt_finish_only, "Only run clean and finish and export phases", NULL },
  { "export-only", 0, 0, G_OPTION_ARG_NONE, &opt_export_only, "Only run export phase", NULL },
  { "allow-missing-runtimes", 0, 0, G_OPTION_ARG_NONE, &opt_allow_missing_runtimes, "Don't fail if runtime and sdk missing", NULL },
  { "show-deps", 0, 0, G_OPTION_ARG_NONE, &opt_show_deps, "List the dependencies of the json file (see --show-deps --help)", NULL },
  { "show-manifest", 0, 0, G_OPTION_ARG_NONE, &opt_show_manifest, "Print out the manifest file in standard json format (see --show-manifest --help)", NULL },
  { "require-changes", 0, 0, G_OPTION_ARG_NONE, &opt_require_changes, "Don't create app dir or export if no changes", NULL },
  { "keep-build-dirs", 0, 0, G_OPTION_ARG_NONE, &opt_keep_build_dirs, "Don't remove build directories after install", NULL },
  { "delete-build-dirs", 0, 0, G_OPTION_ARG_NONE, &opt_delete_build_dirs, "Always remove build directories, even after build failure", NULL },
  { "repo", 0, 0, G_OPTION_ARG_STRING, &opt_repo, "Repo to export into", "DIR"},
  { "subject", 's', 0, G_OPTION_ARG_STRING, &opt_subject, "One line subject (passed to build-export)", "SUBJECT" },
  { "body", 'b', 0, G_OPTION_ARG_STRING, &opt_body, "Full description (passed to build-export)", "BODY" },
  { "collection-id", 0, 0, G_OPTION_ARG_STRING, &opt_collection_id, "Collection ID (passed to build-export)", "COLLECTION-ID" },
  { "token-type", 0, 0, G_OPTION_ARG_INT, &opt_token_type, "Set type of token needed to install this commit (passed to build-export)", "VAL" },
  { "gpg-sign", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_key_ids, "GPG Key ID to sign the commit with", "KEY-ID"},
  { "gpg-homedir", 0, 0, G_OPTION_ARG_STRING, &opt_gpg_homedir, "GPG Homedir to use when looking for keyrings", "HOMEDIR"},
  { "force-clean", 0, 0, G_OPTION_ARG_NONE, &opt_force_clean, "Erase previous contents of DIRECTORY", NULL },
  { "sandbox", 0, 0, G_OPTION_ARG_NONE, &opt_sandboxed, "Enforce sandboxing, disabling build-args", NULL },
  { "stop-at", 0, 0, G_OPTION_ARG_STRING, &opt_stop_at, "Stop building at this module (implies --build-only)", "MODULENAME"},
  { "jobs", 0, 0, G_OPTION_ARG_INT, &opt_jobs, "Number of parallel jobs to build (default=NCPU)", "JOBS"},
  { "rebuild-on-sdk-change", 0, 0, G_OPTION_ARG_NONE, &opt_rebuild_on_sdk_change, "Rebuild if sdk changes", NULL },
  { "skip-if-unchanged", 0, 0, G_OPTION_ARG_NONE, &opt_skip_if_unchanged, "Don't do anything if the json didn't change", NULL },
  { "build-shell", 0, 0, G_OPTION_ARG_STRING, &opt_build_shell, "Extract and prepare sources for module, then start build shell", "MODULENAME"},
  { "from-git", 0, 0, G_OPTION_ARG_STRING, &opt_from_git, "Get input files from git repo", "URL"},
  { "from-git-branch", 0, 0, G_OPTION_ARG_STRING, &opt_from_git_branch, "Branch to use in --from-git", "BRANCH"},
  { "mirror-screenshots-url", 0, 0, G_OPTION_ARG_STRING, &opt_mirror_screenshots_url, "Download and rewrite screenshots to match this url", "URL"},
  { "install", 0, 0, G_OPTION_ARG_NONE, &opt_install, "Install if build succeeds", NULL},
  { "install-deps-from", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_install_deps_from, "Install build dependencies from this remote", "REMOTE"},
  { "install-deps-only", 0, 0, G_OPTION_ARG_NONE, &opt_install_deps_only, "Stop after installing dependencies"},
  { "user", 0, 0, G_OPTION_ARG_NONE, &opt_user, "Install dependencies in user installations", NULL },
  { "system", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_user, "Install dependencies in system-wide installations (default)", NULL },
  { "installation", 0, 0, G_OPTION_ARG_STRING, &opt_installation, "Install dependencies in a specific system-wide installation", "NAME" },
  { "state-dir", 0, 0, G_OPTION_ARG_FILENAME, &opt_state_dir, "Use this directory for state instead of .flatpak-builder", "PATH" },
  { "assumeyes", 'y', 0, G_OPTION_ARG_NONE, &opt_yes, N_("Automatically answer yes for all questions"), NULL },
  { "no-shallow-clone", 0, 0, G_OPTION_ARG_NONE, &opt_no_shallow_clone, "Don't use shallow clones when mirroring git repos", NULL },
  { "override-source-date-epoch", 0, 0, G_OPTION_ARG_INT64, &opt_source_date_epoch, "Use this timestamp to perform the build, instead of the last modification time of the manifest.", NULL },
  { NULL }
};

static GOptionEntry run_entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "arch", 0, 0, G_OPTION_ARG_STRING, &opt_arch, "Architecture to build for (must be host compatible)", "ARCH" },
  { "run", 0, 0, G_OPTION_ARG_NONE, &opt_run, "Run a command in the build directory", NULL },
  { "log-session-bus", 0, 0, G_OPTION_ARG_NONE, &opt_log_session_bus, N_("Log session bus calls"), NULL },
  { "log-system-bus", 0, 0, G_OPTION_ARG_NONE, &opt_log_system_bus, N_("Log system bus calls"), NULL },
  { "ccache", 0, 0, G_OPTION_ARG_NONE, &opt_ccache, "Use ccache", NULL },
  { NULL }
};

static GOptionEntry show_deps_entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "show-deps", 0, 0, G_OPTION_ARG_NONE, &opt_show_deps, "List the dependencies of the json file", NULL },
  { NULL }
};

static GOptionEntry show_manifest_entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Print debug information during command processing", NULL },
  { "show-manifest", 0, 0, G_OPTION_ARG_NONE, &opt_show_manifest, "Print out the manifest file in standard json format", NULL },
  { NULL }
};


static void
message_handler (const gchar   *log_domain,
                 GLogLevelFlags log_level,
                 const gchar   *message,
                 gpointer       user_data)
{
  /* Make this look like normal console output */
  if (log_level & G_LOG_LEVEL_DEBUG)
    g_printerr ("FB: %s\n", message);
  else
    g_printerr ("%s: %s\n", g_get_prgname (), message);
}

static int
usage (GOptionContext *context, const char *message)
{
  g_autofree gchar *help = g_option_context_get_help (context, TRUE, NULL);

  g_printerr ("%s\n", message);
  g_printerr ("%s", help);
  return 1;
}

static const char skip_arg[] = "skip";

static gboolean
do_export (BuilderContext *build_context,
           GError        **error,
           gboolean        runtime,
           const gchar    *location,
           const gchar    *directory,
           char          **exclude_dirs,
           const gchar    *branch,
           const gchar    *collection_id,
           gint32          token_type,
           ...)
{
  va_list ap;
  const char *arg;
  int i;

  g_autoptr(GPtrArray) args = NULL;

  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("flatpak"));
  g_ptr_array_add (args, g_strdup ("build-export"));

  g_ptr_array_add (args, g_strdup_printf ("--arch=%s", builder_context_get_arch (build_context)));

  if (runtime)
    g_ptr_array_add (args, g_strdup ("--runtime"));

  if (opt_subject)
    g_ptr_array_add (args, g_strdup_printf ("--subject=%s", opt_subject));

  if (opt_body)
    g_ptr_array_add (args, g_strdup_printf ("--body=%s", opt_body));

  if (opt_gpg_homedir)
    g_ptr_array_add (args, g_strdup_printf ("--gpg-homedir=%s", opt_gpg_homedir));

  for (i = 0; opt_key_ids != NULL && opt_key_ids[i] != NULL; i++)
    g_ptr_array_add (args, g_strdup_printf ("--gpg-sign=%s", opt_key_ids[i]));

  if (collection_id)
    g_ptr_array_add (args, g_strdup_printf ("--collection-id=%s", collection_id));

  if (token_type >= 0)
    g_ptr_array_add (args, g_strdup_printf ("--token-type=%d", token_type));

  /* Additional flags. */
  va_start (ap, token_type);
  while ((arg = va_arg (ap, const gchar *)))
    if (arg != skip_arg)
      g_ptr_array_add (args, g_strdup ((gchar *) arg));
  va_end (ap);

  if (exclude_dirs)
    {
      for (i = 0; exclude_dirs[i] != NULL; i++)
        g_ptr_array_add (args, g_strdup_printf ("--exclude=/%s/*", exclude_dirs[i]));
    }

  /* Mandatory positional arguments. */
  g_ptr_array_add (args, g_strdup (location));
  g_ptr_array_add (args, g_strdup (directory));
  g_ptr_array_add (args, g_strdup (branch));

  g_ptr_array_add (args, NULL);

  return flatpak_spawnv (NULL, NULL, G_SUBPROCESS_FLAGS_NONE, error,
                         (const gchar * const *) args->pdata, NULL);
}

static gboolean
do_install (BuilderContext *build_context,
            const gchar    *repodir,
            const gchar    *id,
            const gchar    *branch,
            GError        **error)
{
  g_autofree char *ref = NULL;

  g_autoptr(GPtrArray) args = NULL;

  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("flatpak"));
  g_ptr_array_add (args, g_strdup ("install"));

  if (opt_user)
    g_ptr_array_add (args, g_strdup ("--user"));
  else if (opt_installation)
    g_ptr_array_add (args, g_strdup_printf ("--installation=%s", opt_installation));
  else
    g_ptr_array_add (args, g_strdup ("--system"));

  g_ptr_array_add (args, g_strdup ("-y"));
  if (flatpak_version_check (1, 2, 0))
    g_ptr_array_add (args, g_strdup ("--noninteractive"));
  g_ptr_array_add (args, g_strdup ("--reinstall"));

  ref = flatpak_build_untyped_ref (id, branch,
                                   builder_context_get_arch (build_context));

  g_ptr_array_add (args, g_strdup (repodir));
  g_ptr_array_add (args, g_strdup (ref));

  g_ptr_array_add (args, NULL);

  return flatpak_spawnv (NULL, NULL, G_SUBPROCESS_FLAGS_NONE, error,
                         (const gchar * const *) args->pdata, NULL);
}

static gboolean
git (char   **output,
     GError **error,
     ...)
{
  gboolean res;
  va_list ap;

  if (output != NULL)
    *output = NULL;

  va_start (ap, error);
  res = flatpak_spawn (NULL, output, 0, error, "git", ap);
  va_end (ap);

  if (output != NULL &&
      (*output != NULL && *output[0] == '\0'))
    {
      g_free (*output);
      *output = NULL;
    }

  return res;
}

static char *
trim_linefeed (char *str)
{
  guint len;

  g_return_val_if_fail (str != NULL, NULL);

  len = strlen (str);
  str[len] = '\0';

  return str;
}

static void
git_init_email (void)
{
  char *user, *email;

  /* Have an email for author and committer */
  if (!git (&email, NULL, "config", "--get", "user.email", NULL) ||
      email == NULL)
    email = g_strdup ("flatpak-builder-commit@flatpak.org");
  else
    email = trim_linefeed (email);
  g_setenv ("GIT_AUTHOR_EMAIL", email, FALSE);
  g_setenv ("GIT_COMMITTER_EMAIL", email, FALSE);
  g_free (email);

  /* Have a "real name" for author and committer */
  if (!git (&user, NULL, "config", "--get", "user.name", NULL) ||
      user == NULL)
    user = g_strdup ("Flatpak git committer");
  else
    user = trim_linefeed (user);
  g_setenv ("GIT_AUTHOR_NAME", user, FALSE);
  g_setenv ("GIT_COMMITTER_NAME", user, FALSE);
  g_free (user);
}

int
main (int    argc,
      char **argv)
{
  g_autofree const char *old_env = NULL;

  g_autoptr(GError) error = NULL;
  g_autoptr(BuilderManifest) manifest = NULL;
  g_autoptr(GOptionContext) context = NULL;
  const char *app_dir_path = NULL, *manifest_rel_path;
  g_autofree gchar *manifest_contents = NULL;
  g_autofree gchar *manifest_sha256 = NULL;
  g_autofree gchar *old_manifest_sha256 = NULL;
  g_autoptr(BuilderContext) build_context = NULL;
  g_autoptr(GFile) base_dir = NULL;
  g_autoptr(GFile) manifest_file = NULL;
  g_autoptr(GFile) app_dir = NULL;
  g_autoptr(BuilderCache) cache = NULL;
  g_autofree char *cache_branch = NULL;
  g_autofree char *escaped_cache_branch = NULL;
  g_autoptr(GFileEnumerator) dir_enum = NULL;
  g_autoptr(GFileEnumerator) dir_enum2 = NULL;
  g_autofree char *cwd = NULL;
  g_autoptr(GFile) cwd_dir = NULL;
  GFileInfo *next = NULL;
  const char *platform_id = NULL;
  g_autofree char **orig_argv = NULL;
  gboolean is_run = FALSE;
  gboolean is_show_deps = FALSE;
  gboolean is_show_manifest = FALSE;
  gboolean app_dir_is_empty = FALSE;
  gboolean prune_unused_stages = FALSE;
  g_autoptr(FlatpakContext) arg_context = NULL;
  g_autoptr(FlatpakTempDir) cleanup_manifest_dir = NULL;
  g_autofree char *manifest_basename = NULL;
  g_autoptr(GFile) export_repo = NULL;
  int i, first_non_arg, orig_argc;
  int argnr;
  char *p;
  struct stat statbuf;
  gsize manifest_length;

  setlocale (LC_ALL, "");

  g_log_set_handler (NULL, G_LOG_LEVEL_MESSAGE, message_handler, NULL);

  g_set_prgname (argv[0]);

  /* avoid gvfs (http://bugzilla.gnome.org/show_bug.cgi?id=526454) */
  old_env = g_strdup (g_getenv ("GIO_USE_VFS"));
  g_setenv ("GIO_USE_VFS", "local", TRUE);
  g_vfs_get_default ();
  if (old_env)
    g_setenv ("GIO_USE_VFS", old_env, TRUE);
  else
    g_unsetenv ("GIO_USE_VFS");

  orig_argv = g_memdup2 (argv, sizeof (char *) * argc);
  orig_argc = argc;

  first_non_arg = 1;
  for (i = 1; i < argc; i++)
    {
      if (argv[i][0] != '-')
        break;
      first_non_arg = i + 1;
      if (strcmp (argv[i], "--run") == 0)
        is_run = TRUE;
      if (strcmp (argv[i], "--show-deps") == 0)
        is_show_deps = TRUE;
      if (strcmp (argv[i], "--show-manifest") == 0)
        is_show_manifest = TRUE;
    }

  if (is_run)
    {
      context = g_option_context_new ("DIRECTORY MANIFEST COMMAND [args] - Run command in build sandbox");
      g_option_context_add_main_entries (context, run_entries, NULL);
      arg_context = flatpak_context_new ();
      g_option_context_add_group (context, flatpak_context_get_options (arg_context));

      /* We drop the post-command part from the args, these go with the command in the sandbox */
      argc = MIN (first_non_arg + 3, argc);
    }
  else if (is_show_deps)
    {
      context = g_option_context_new ("MANIFEST - Show manifest dependencies");
      g_option_context_add_main_entries (context, show_deps_entries, NULL);
    }
  else if (is_show_manifest)
    {
      context = g_option_context_new ("MANIFEST - Show manifest");
      g_option_context_add_main_entries (context, show_manifest_entries, NULL);
    }
  else
    {
      context = g_option_context_new ("DIRECTORY MANIFEST - Build manifest");
      g_option_context_add_main_entries (context, entries, NULL);
    }

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("Option parsing failed: %s\n", error->message);
      return 1;
    }

  if (opt_version)
    {
      g_print ("%s\n", PACKAGE_STRING);
      exit (EXIT_SUCCESS);
    }

  if (opt_verbose)
    g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, message_handler, NULL);

  argnr = 1;

  if (!is_show_deps && !is_show_manifest)
    {
      if (argc == argnr)
        return usage (context, "DIRECTORY must be specified");
      app_dir_path = argv[argnr++];
    }

  if (argc == argnr)
    return usage (context, "MANIFEST must be specified");
  manifest_rel_path = argv[argnr++];
  manifest_basename = g_path_get_basename (manifest_rel_path);

  if (opt_collection_id != NULL &&
      !ostree_validate_collection_id (opt_collection_id, &error))
    {
      g_printerr ("‘%s’ is not a valid collection ID: %s", opt_collection_id, error->message);
      return 1;
    }

  if (opt_token_type < -1
#if G_MAXINT > 0x7fffffff
      || opt_token_type > G_MAXINT32
#endif
      )
    {
      g_printerr ("--token-type value must be a 32 bit integer >= 0\n");
      return 1;
    }

  if (app_dir_path)
    app_dir = g_file_new_for_path (app_dir_path);
  cwd = g_get_current_dir ();
  cwd_dir = g_file_new_for_path (cwd);

  build_context = builder_context_new (cwd_dir, app_dir, opt_state_dir);

  builder_context_set_use_rofiles (build_context, !opt_disable_rofiles);
  builder_context_set_run_tests (build_context, !opt_disable_tests);
  builder_context_set_no_shallow_clone (build_context, opt_no_shallow_clone);
  builder_context_set_keep_build_dirs (build_context, opt_keep_build_dirs);
  builder_context_set_delete_build_dirs (build_context, opt_delete_build_dirs);
  builder_context_set_sandboxed (build_context, opt_sandboxed);
  builder_context_set_jobs (build_context, opt_jobs);
  builder_context_set_rebuild_on_sdk_change (build_context, opt_rebuild_on_sdk_change);
  builder_context_set_bundle_sources (build_context, opt_bundle_sources);
  builder_context_set_opt_export_only (build_context, opt_export_only);
  builder_context_set_opt_mirror_screenshots_url (build_context, opt_mirror_screenshots_url);

  git_init_email ();

  if (opt_sources_dirs)
    {
      g_autoptr(GPtrArray) sources_dirs = NULL;
      sources_dirs = g_ptr_array_new_with_free_func (g_object_unref);
      for (i = 0; opt_sources_dirs != NULL && opt_sources_dirs[i] != NULL; i++)
        {
          GFile *file = g_file_new_for_commandline_arg (opt_sources_dirs[i]);
          g_ptr_array_add (sources_dirs, file);
        }
      builder_context_set_sources_dirs (build_context, sources_dirs);
    }

  if (opt_sources_urls)
    {
      g_autoptr(GPtrArray) sources_urls = NULL;
      sources_urls = g_ptr_array_new_with_free_func ((GDestroyNotify) g_uri_unref);
      for (i = 0; opt_sources_urls[i] != NULL; i++)
        {
          if (!g_str_has_suffix (opt_sources_urls[i], "/"))
            {
              g_autofree gchar *tmp = opt_sources_urls[i];
              opt_sources_urls[i] = g_strdup_printf ("%s/", tmp);
            }

          GUri *uri = g_uri_parse (opt_sources_urls[i], CONTEXT_HTTP_URI_FLAGS, &error);
          if (uri == NULL)
            {
              g_printerr ("Invalid URL '%s'", error->message);
              return 1;
            }
          g_ptr_array_add (sources_urls, uri);
        }
      builder_context_set_sources_urls (build_context, sources_urls);
    }

  if (opt_arch)
    builder_context_set_arch (build_context, opt_arch);

  if (opt_stop_at)
    {
      opt_build_only = TRUE;
      builder_context_set_stop_at (build_context, opt_stop_at);
    }

  if (!builder_context_set_enable_ccache (build_context, opt_ccache, &error))
    {
      g_printerr ("Can't initialize ccache use: %s\n", error->message);
      return 1;
  }

  if (opt_from_git)
    {
      g_autofree char *manifest_dirname = g_path_get_dirname (manifest_rel_path);
      g_autofree char *default_branch_name = builder_git_get_default_branch (opt_from_git);
      const char *git_branch = opt_from_git_branch ? opt_from_git_branch : default_branch_name;
      g_autoptr(GFile) build_subdir = NULL;

      build_subdir = builder_context_allocate_build_subdir (build_context, manifest_basename, &error);
      if (build_subdir == NULL)
        {
          g_printerr ("Can't check out manifest repo: %s\n", error->message);
          return 1;
        }

      cleanup_manifest_dir = g_object_ref (build_subdir);

      int mirror_flags = FLATPAK_GIT_MIRROR_FLAGS_MIRROR_SUBMODULES;

      if (opt_no_shallow_clone)
        {
          mirror_flags |= FLATPAK_GIT_MIRROR_FLAGS_DISABLE_SHALLOW;
        }

      if (opt_disable_updates)
        {
          mirror_flags |= FLATPAK_GIT_MIRROR_FLAGS_UPDATE;
        }

      if (!builder_git_mirror_repo (opt_from_git,
                                    NULL,
                                    mirror_flags,
                                    git_branch, build_context, &error))
        {
          g_printerr ("Can't clone manifest repo: %s\n", error->message);
          return 1;
        }

      if (!builder_git_checkout (opt_from_git,
                                 git_branch,
                                 build_subdir,
                                 build_context,
                                 mirror_flags,
                                 &error))
        {
          g_printerr ("Can't check out manifest repo: %s\n", error->message);
          return 1;
        }

      manifest_file = g_file_get_child (build_subdir, manifest_rel_path);
      base_dir = g_file_resolve_relative_path (build_subdir, manifest_dirname);
    }
  else
    {
      manifest_file = g_file_new_for_path (manifest_rel_path);
      base_dir = g_file_get_parent (manifest_file);
    }

  builder_context_set_base_dir (build_context, base_dir);
  if (!g_file_get_contents (flatpak_file_get_path_cached (manifest_file), &manifest_contents, &manifest_length, &error))
    {
      g_printerr ("Can't load '%s': %s\n", manifest_rel_path, error->message);
      return 1;
    }

  if (manifest_length == 0)
    {
      g_printerr ("Empty manifest file: '%s'\n", manifest_rel_path);
      return 1;
    }

  if (opt_source_date_epoch > -1)
    builder_context_set_source_date_epoch (build_context, opt_source_date_epoch);
  else if (stat (flatpak_file_get_path_cached (manifest_file), &statbuf) == 0)
    builder_context_set_source_date_epoch (build_context, (gint64)statbuf.st_mtime);

  manifest_sha256 = g_compute_checksum_for_string (G_CHECKSUM_SHA256, manifest_contents, -1);

  if (opt_skip_if_unchanged)
    {
      old_manifest_sha256 = builder_context_get_checksum_for (build_context, manifest_basename);
      if (old_manifest_sha256 != NULL && strcmp (manifest_sha256, old_manifest_sha256) == 0)
        {
          g_print ("No changes to manifest, skipping\n");
          return 42;
        }
    }

  /* Can't push this as user data to the demarshalling :/ */
  builder_manifest_set_demarshal_base_dir (builder_context_get_base_dir (build_context));

  manifest = (BuilderManifest *) builder_gobject_from_data (BUILDER_TYPE_MANIFEST, manifest_rel_path,
                                                            manifest_contents, &error);

  builder_manifest_set_demarshal_base_dir (NULL);

  if (manifest == NULL)
    {
      g_printerr ("Can't parse '%s': %s\n", manifest_rel_path, error->message);
      return 1;
    }

  if (opt_remove_tags)
    builder_manifest_remove_tags (manifest, (const char **)opt_remove_tags);

  if (opt_add_tags)
    builder_manifest_add_tags (manifest, (const char **)opt_add_tags);

  if (opt_default_branch)
    builder_context_set_default_branch (build_context, opt_default_branch);

  if (opt_collection_id)
    builder_manifest_set_default_collection_id (manifest, opt_collection_id);

  if (opt_token_type >= 0)
    builder_manifest_set_default_token_type (manifest, (gint32)opt_token_type);

  if (is_run && argc == 3)
    return usage (context, "Program to run must be specified");

  if (opt_show_deps && !is_show_deps)
    return usage (context, "Can't use --show-deps after a non-option");

  if (opt_run && !is_run)
    return usage (context, "Can't use --run after a non-option");

  if (is_show_deps)
    {
      if (!builder_manifest_show_deps (manifest, build_context, &error))
        {
          g_printerr ("Error calculating deps: %s\n", error->message);
          return 1;
        }

      return 0;
    }

  if (is_show_manifest)
    {
      g_autofree char *json = builder_manifest_serialize (manifest);
      g_print ("%s\n", json);
      return 0;
    }

  if (opt_install_deps_from != NULL)
    {
      if (!builder_manifest_install_deps (manifest, build_context, opt_install_deps_from, opt_user, opt_installation,
                                          opt_yes, &error))
        {
          g_printerr ("Error installing deps: %s\n", error->message);
          return 1;
        }
      if (opt_install_deps_only)
        return 0;
    }

  app_dir_is_empty = !g_file_query_exists (app_dir, NULL) ||
                     directory_is_empty (app_dir_path);

  if (is_run)
    {
      g_assert (opt_run);

      if (app_dir_is_empty)
        {
          g_printerr ("App dir '%s' is empty or doesn't exist.\n", app_dir_path);
          return 1;
        }

      if (!builder_manifest_run (manifest, build_context, arg_context,
                                 orig_argv + first_non_arg + 2,
                                 orig_argc - first_non_arg - 2,
                                 opt_log_session_bus, opt_log_system_bus,
                                 &error))
        {
          g_printerr ("Error running %s: %s\n", argv[3], error->message);
          return 1;
        }

      return 0;
    }

  g_assert (!opt_run);
  g_assert (!opt_show_deps);

  if (opt_export_only || opt_finish_only || opt_build_shell)
    {
      if (app_dir_is_empty)
        {
          g_printerr ("App dir '%s' is empty or doesn't exist.\n", app_dir_path);
          return 1;
        }
    }
  else
    {
      if (!app_dir_is_empty)
        {
          if (opt_force_clean)
            {
              g_print ("Emptying app dir '%s'\n", app_dir_path);
              if (!flatpak_rm_rf (app_dir, NULL, &error))
                {
                  g_printerr ("Couldn't empty app dir '%s': %s\n",
                              app_dir_path, error->message);
                  return 1;
                }
            }
          else
            {
              g_printerr ("App dir '%s' is not empty. Please delete "
                          "the existing contents or use --force-clean.\n", app_dir_path);
              return 1;
            }
        }
    }

  /* Verify that cache and build dir is on same filesystem */
  if (!opt_download_only)
    {
      g_autofree char *state_path = g_file_get_path (builder_context_get_state_dir (build_context));
      g_autoptr(GFile) app_parent = g_file_get_parent (builder_context_get_app_dir (build_context));
      g_autofree char *app_parent_path = g_file_get_path (app_parent);
      struct stat buf1, buf2;

      if (stat (app_parent_path, &buf1) == 0 && stat (state_path, &buf2) == 0 &&
          buf1.st_dev != buf2.st_dev)
        {
          g_printerr ("The state dir (%s) is not on the same filesystem as the target dir (%s)\n",
                      state_path, app_parent_path);
          return 1;
        }
    }

  if (!builder_context_set_checksum_for (build_context, manifest_basename, manifest_sha256, &error))
    {
      g_printerr ("Failed to set checksum for ‘%s’: %s\n", manifest_basename, error->message);
      return 1;
    }

  if (!builder_manifest_start (manifest, opt_download_only, opt_allow_missing_runtimes, build_context, &error))
    {
      g_printerr ("Failed to init: %s\n", error->message);
      return 1;
    }

  if (!opt_finish_only &&
      !opt_export_only &&
      !opt_disable_download &&
      !builder_manifest_download (manifest, !opt_disable_updates, opt_build_shell, build_context, &error))
    {
      g_printerr ("Failed to download sources: %s\n", error->message);
      return 1;
    }

  if (opt_download_only)
    return 0;

  if (opt_build_shell)
    {
      if (!builder_manifest_build_shell (manifest, build_context, opt_build_shell, &error))
        {
          g_printerr ("Failed to setup module: %s\n", error->message);
          return 1;
        }

      return 0;
    }

  if (opt_state_dir)
    {
      /* If the state dir can be shared we need to use a global identifier for the key */
      g_autofree char *manifest_path = g_file_get_path (manifest_file);
      cache_branch = g_strconcat (builder_context_get_arch (build_context), "-", manifest_path + 1, NULL);
    }
  else
    cache_branch = g_strconcat (builder_context_get_arch (build_context), "-", manifest_basename, NULL);

  escaped_cache_branch = g_uri_escape_string (cache_branch, "", TRUE);
  for (p = escaped_cache_branch; *p; p++)
    {
      if (*p == '%')
        *p = '_';
    }

  cache = builder_cache_new (build_context, app_dir, escaped_cache_branch);
  if (!builder_cache_open (cache, &error))
    {
      g_printerr ("Error opening cache: %s\n", error->message);
      return 1;
    }

  if (opt_disable_cache) /* This disables *lookups*, but we still build the cache */
    builder_cache_disable_lookups (cache);

  builder_manifest_checksum (manifest, cache, build_context);

  if (!opt_finish_only && !opt_export_only)
    {
      if (!builder_cache_lookup (cache, "init"))
        {
          g_autofree char *body =
            g_strdup_printf ("Initialized %s\n",
                             builder_manifest_get_id (manifest));
          if (!builder_manifest_init_app_dir (manifest, cache, build_context, &error))
            {
              g_printerr ("Error: %s\n", error->message);
              return 1;
            }

          if (!builder_cache_commit (cache, body, &error))
            {
              g_printerr ("Error: %s\n", error->message);
              return 1;
            }
        }

      if (!builder_manifest_build (manifest, cache, build_context, &error))
        {
          g_printerr ("Error: %s\n", error->message);
          return 1;
        }
    }

  if (!opt_build_only && !opt_export_only)
    {
      if (!builder_manifest_cleanup (manifest, cache, build_context, &error))
        {
          g_printerr ("Error: %s\n", error->message);
          return 1;
        }

      if (!builder_manifest_finish (manifest, cache, build_context, &error))
        {
          g_printerr ("Error: %s\n", error->message);
          return 1;
        }

      if (!builder_manifest_create_platform (manifest, cache, build_context, &error))
        {
          g_printerr ("Error: %s\n", error->message);
          return 1;
        }

      if (builder_context_get_bundle_sources (build_context) &&
          !builder_manifest_bundle_sources (manifest, manifest_contents, cache, build_context, &error))
        {
          g_printerr ("Error: %s\n", error->message);
          return 1;
        }
    }

  if (!opt_require_changes && !opt_export_only)
    builder_cache_ensure_checkout (cache);

  if (!opt_build_only &&
      (opt_repo || opt_install) &&
      (opt_export_only || builder_cache_has_checkout (cache)))
    {
      g_autoptr(GFile) debuginfo_metadata = NULL;
      g_autoptr(GFile) sourcesinfo_metadata = NULL;
      g_auto(GStrv) exclude_dirs = builder_manifest_get_exclude_dirs (manifest);
      GList *l;

      if (opt_repo)
        export_repo = g_file_new_for_path (opt_repo);
      else if (opt_install)
        export_repo = g_object_ref (builder_context_get_cache_dir (build_context));

      g_print ("Exporting %s to repo\n", builder_manifest_get_id (manifest));
      builder_set_term_title (_("Exporting to repository"));

      if (!do_export (build_context, &error,
                      FALSE,
                      flatpak_file_get_path_cached (export_repo),
                      app_dir_path, exclude_dirs, builder_manifest_get_branch (manifest, build_context),
                      builder_manifest_get_collection_id (manifest),
                      builder_manifest_get_token_type (manifest),
                      "--exclude=/lib/debug/*",
                      "--include=/lib/debug/app",
                      builder_context_get_separate_locales (build_context) ? "--exclude=/share/runtime/locale/*/*" : skip_arg,
                      NULL))
        {
          g_printerr ("Export failed: %s\n", error->message);
          return 1;
        }

      /* Export regular locale extensions */
      dir_enum = g_file_enumerate_children (app_dir, "standard::name,standard::type",
                                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                            NULL, NULL);
      while (dir_enum != NULL &&
             (next = g_file_enumerator_next_file (dir_enum, NULL, NULL)))
        {
          g_autoptr(GFileInfo) child_info = next;
          const char *name = g_file_info_get_name (child_info);
          g_autofree char *metadata_arg = NULL;
          g_autofree char *files_arg = NULL;
          g_autofree char *locale_id = builder_manifest_get_locale_id (manifest);

          if (strcmp (name, "metadata.locale") == 0)
            g_print ("Exporting %s to repo\n", locale_id);
          else
            continue;

          metadata_arg = g_strdup_printf ("--metadata=%s", name);
          files_arg = g_strconcat (builder_context_get_build_runtime (build_context) ? "--files=usr" : "--files=files",
                                   "/share/runtime/locale/", NULL);
          if (!do_export (build_context, &error, TRUE,
                          flatpak_file_get_path_cached (export_repo),
                          app_dir_path, NULL, builder_manifest_get_branch (manifest, build_context),
                          builder_manifest_get_collection_id (manifest),
                          builder_manifest_get_token_type (manifest),
                          metadata_arg,
                          files_arg,
                          NULL))
            {
              g_printerr ("Export failed: %s\n", error->message);
              return 1;
            }
        }

      /* Export debug extensions */
      debuginfo_metadata = g_file_get_child (app_dir, "metadata.debuginfo");
      if (g_file_query_exists (debuginfo_metadata, NULL))
        {
          g_autofree char *debug_id = builder_manifest_get_debug_id (manifest);
          g_print ("Exporting %s to repo\n", debug_id);

          if (!do_export (build_context, &error, TRUE,
                          flatpak_file_get_path_cached (export_repo),
                          app_dir_path, NULL, builder_manifest_get_branch (manifest, build_context),
                          builder_manifest_get_collection_id (manifest),
                          builder_manifest_get_token_type (manifest),
                          "--metadata=metadata.debuginfo",
                          builder_context_get_build_runtime (build_context) ? "--files=usr/lib/debug" : "--files=files/lib/debug",
                          NULL))
            {
              g_printerr ("Export failed: %s\n", error->message);
              return 1;
            }
        }

      for (l = builder_manifest_get_add_extensions (manifest); l != NULL; l = l->next)
        {
          BuilderExtension *e = l->data;
          const char *extension_id = NULL;
          g_autofree char *metadata_arg = NULL;
          g_autofree char *files_arg = NULL;

          if (!builder_extension_is_bundled (e))
            continue;

          extension_id = builder_extension_get_name (e);
          g_print ("Exporting %s to repo\n", extension_id);

          metadata_arg = g_strdup_printf ("--metadata=metadata.%s", extension_id);
          files_arg = g_strdup_printf ("--files=%s/%s",
                                       builder_context_get_build_runtime (build_context) ? "usr" : "files",
                                       builder_extension_get_directory (e));

          const char *extension_branch = builder_extension_get_version (e);
          if (extension_branch == NULL)
            extension_branch = builder_manifest_get_branch (manifest, build_context);

          if (!do_export (build_context, &error, TRUE,
                          flatpak_file_get_path_cached (export_repo),
                          app_dir_path, NULL, extension_branch,
                          builder_manifest_get_collection_id (manifest),
                          builder_manifest_get_token_type (manifest),
                          metadata_arg, files_arg,
                          NULL))
            {
              g_printerr ("Export failed: %s\n", error->message);
              return 1;
            }
        }

      /* Export sources extensions */
      sourcesinfo_metadata = g_file_get_child (app_dir, "metadata.sources");
      if (g_file_query_exists (sourcesinfo_metadata, NULL))
        {
          g_autofree char *sources_id = builder_manifest_get_sources_id (manifest);
          g_print ("Exporting %s to repo\n", sources_id);

          if (!do_export (build_context, &error, TRUE,
                          flatpak_file_get_path_cached (export_repo),
                          app_dir_path, NULL, builder_manifest_get_branch (manifest, build_context),
                          builder_manifest_get_collection_id (manifest),
                          builder_manifest_get_token_type (manifest),
                          "--metadata=metadata.sources",
                          "--files=sources",
                          NULL))
            {
              g_printerr ("Export failed: %s\n", error->message);
              return 1;
            }
        }

      /* Export platform */
      platform_id = builder_manifest_get_id_platform (manifest);
      if (builder_context_get_build_runtime (build_context) &&
          platform_id != NULL)
        {
          g_print ("Exporting %s to repo\n", platform_id);

          if (!do_export (build_context, &error, TRUE,
                          flatpak_file_get_path_cached (export_repo),
                          app_dir_path, NULL, builder_manifest_get_branch (manifest, build_context),
                          builder_manifest_get_collection_id (manifest),
                          builder_manifest_get_token_type (manifest),
                          "--metadata=metadata.platform",
                          "--files=platform",
                          builder_context_get_separate_locales (build_context) ? "--exclude=/share/runtime/locale/*/*" : skip_arg,
                          NULL))
            {
              g_printerr ("Export failed: %s\n", error->message);
              return 1;
            }
        }

      /* Export platform locales */
      dir_enum2 = g_file_enumerate_children (app_dir, "standard::name,standard::type",
                                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                             NULL, NULL);
      while (dir_enum2 != NULL &&
             (next = g_file_enumerator_next_file (dir_enum2, NULL, NULL)))
        {
          g_autoptr(GFileInfo) child_info = next;
          const char *name = g_file_info_get_name (child_info);
          g_autofree char *metadata_arg = NULL;
          g_autofree char *files_arg = NULL;
          g_autofree char *locale_id = builder_manifest_get_locale_id_platform (manifest);

          if (strcmp (name, "metadata.platform.locale") == 0)
            g_print ("Exporting %s to repo\n", locale_id);
          else
            continue;

          metadata_arg = g_strdup_printf ("--metadata=%s", name);
          files_arg = g_strconcat ("--files=platform/share/runtime/locale/", NULL);
          if (!do_export (build_context, &error, TRUE,
                          flatpak_file_get_path_cached (export_repo),
                          app_dir_path, NULL, builder_manifest_get_branch (manifest, build_context),
                          builder_manifest_get_collection_id (manifest),
                          builder_manifest_get_token_type (manifest),
                          metadata_arg,
                          files_arg,
                          NULL))
            {
              g_printerr ("Export failed: %s\n", error->message);
              return 1;
            }
        }
    }

  if (opt_install)
    {
      /* We may end here with a NULL export repo if --require-changes was
         passed and there were no changes, do nothing in that case */
      if (export_repo == NULL)
        g_printerr ("NOTE: No export due to --require-changes, ignoring --install\n");
      else if (!do_install (build_context, flatpak_file_get_path_cached (export_repo),
                            builder_manifest_get_id (manifest),
                            builder_manifest_get_branch (manifest, build_context),
                            &error))
        {
          g_printerr ("Install failed: %s\n", error->message);
          return 1;
        }
    }

  if (!opt_finish_only && !opt_export_only)
    prune_unused_stages = TRUE;

  if (!builder_gc (cache, prune_unused_stages, &error))
    {
      g_warning ("Failed to GC build cache: %s", error->message);
      g_clear_error (&error);
    }

  return 0;
}
