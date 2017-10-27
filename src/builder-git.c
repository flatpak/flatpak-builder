/* builder-git.c
 *
 * Copyright (C) 2015 Red Hat, Inc
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statfs.h>

#include "builder-utils.h"

#include "builder-git.h"
#include "builder-utils.h"
#include "builder-flatpak-utils.h"

static gboolean
git (GFile   *dir,
     char   **output,
     GSubprocessFlags flags,
     GError **error,
     ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (dir, output, flags, error, "git", ap);
  va_end (ap);

  return res;
}

static GHashTable *
git_ls_remote (GFile *repo_dir,
               const char *remote,
               GError **error)
{
  char *output = NULL;
  g_autoptr(GHashTable) refs = NULL;
  g_auto(GStrv) lines = NULL;
  int i;

  if (!git (repo_dir, &output, 0, error,
            "ls-remote", remote, NULL))
    return NULL;

  refs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  lines = g_strsplit (output, "\n", -1);
  for (i = 0; lines[i] != NULL; i++)
    {
      g_autofree char **line = g_strsplit (lines[i], "\t", 2);
      if (line[0] != NULL && line[1] != NULL)
        g_hash_table_insert (refs, line[1], line[0]);
    }

  return g_steal_pointer (&refs);
}

static char *
lookup_full_ref (GHashTable *refs, const char *ref)
{
  int i;
  const char *prefixes[] = {
    "",
    "refs/",
    "refs/tags/",
    "refs/heads/"
  };
  GHashTableIter iter;
  gpointer key, value;

  for (i = 0; i < G_N_ELEMENTS(prefixes); i++)
    {
      g_autofree char *full_ref = g_strconcat (prefixes[i], ref, NULL);
      if (g_hash_table_contains (refs, full_ref))
        return g_steal_pointer (&full_ref);
    }

  g_hash_table_iter_init (&iter, refs);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *key_ref = key;
      const char *commit = value;

      if (g_ascii_strncasecmp (commit, ref, strlen (ref)) == 0)
        {
          char *full_ref = g_strdup (key_ref);
          if (g_str_has_suffix (full_ref, "^{}"))
            full_ref[strlen (full_ref) - 3] = 0;
          return full_ref;
        }
    }

  return NULL;
}

static GFile *
git_get_mirror_dir (const char     *url_or_path,
                    BuilderContext *context)
{
  g_autoptr(GFile) git_dir = NULL;
  g_autofree char *filename = NULL;
  g_autofree char *git_dir_path = NULL;

  git_dir = g_file_get_child (builder_context_get_state_dir (context),
                              "git");

  git_dir_path = g_file_get_path (git_dir);
  g_mkdir_with_parents (git_dir_path, 0755);

  /* Technically a path isn't a uri but if it's absolute it should still be unique. */
  filename = builder_uri_to_filename (url_or_path);
  return g_file_get_child (git_dir, filename);
}

static char *
git_get_current_commit (GFile          *repo_dir,
                        const char     *branch,
                        gboolean        ensure_commit,
                        BuilderContext *context,
                        GError        **error)
{
  char *output = NULL;
  g_autofree char *arg = NULL;

  if (ensure_commit)
    arg = g_strconcat (branch, "^{commit}", NULL);
  else
    arg = g_strdup (branch);

  if (!git (repo_dir, &output, 0, error,
            "rev-parse", arg, NULL))
    return NULL;

  /* Trim trailing whitespace */
  g_strchomp (output);

  return output;
}

char *
builder_git_get_current_commit (const char     *repo_location,
                                const char     *branch,
                                gboolean        ensure_commit,
                                BuilderContext *context,
                                GError        **error)
{
  g_autoptr(GFile) mirror_dir = NULL;

  mirror_dir = git_get_mirror_dir (repo_location, context);
  return git_get_current_commit (mirror_dir, branch, ensure_commit, context, error);
}

static char *
make_absolute (const char *orig_parent, const char *orig_relpath, GError **error)
{
  g_autofree char *parent = g_strdup (orig_parent);
  const char *relpath = orig_relpath;
  char *start;
  char *parent_path;

  if (!g_str_has_prefix (relpath, "../"))
    return g_strdup (orig_relpath);

  if (parent[strlen (parent) - 1] == '/')
    parent[strlen (parent) - 1] = 0;

  if ((start = strstr (parent, "://")))
    start = start + 3;
  else
    start = parent;

  parent_path = strchr (start, '/');
  if (parent_path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid uri or path %s", orig_parent);
      return NULL;
    }

  while (g_str_has_prefix (relpath, "../"))
    {
      char *last_slash = strrchr (parent_path, '/');
      if (last_slash == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid relative path %s for uri or path %s", orig_relpath, orig_parent);
          return NULL;
        }
      relpath += 3;
      *last_slash = 0;
    }

  return g_strconcat (parent, "/", relpath, NULL);
}

static gboolean
git_mirror_submodules (const char     *repo_location,
                       const char     *destination_path,
                       gboolean        shallow,
                       gboolean        update,
                       GFile          *mirror_dir,
                       gboolean        disable_fsck,
                       const char     *revision,
                       BuilderContext *context,
                       GError        **error)
{
  g_autoptr(GKeyFile) key_file = g_key_file_new ();
  g_autofree gchar *rev_parse_output = NULL;
  g_autofree gchar *submodule_data = NULL;
  g_autofree gchar **submodules = NULL;
  g_autofree gchar *gitmodules = g_strconcat (revision, ":.gitmodules", NULL);
  gsize num_submodules;

  if (!git (mirror_dir, &rev_parse_output, 0, NULL, "rev-parse", "--verify", "--quiet", gitmodules, NULL))
    return TRUE;

  if (git (mirror_dir, &submodule_data, 0, NULL, "show", gitmodules, NULL))
    {
      if (!g_key_file_load_from_data (key_file, submodule_data, -1,
                                      G_KEY_FILE_NONE, error))
        return FALSE;

      submodules = g_key_file_get_groups (key_file, &num_submodules);

      int i;
      for (i = 0; i < num_submodules; i++)
        {
          g_autofree gchar *submodule = NULL;
          g_autofree gchar *path = NULL;
          g_autofree gchar *relative_url = NULL;
          g_autofree gchar *absolute_url = NULL;
          g_autofree gchar *ls_tree = NULL;
          g_auto(GStrv) lines = NULL;
          g_auto(GStrv) words = NULL;

          submodule = submodules[i];

          if (!g_str_has_prefix (submodule, "submodule \""))
            continue;

          path = g_key_file_get_string (key_file, submodule, "path", error);
          if (path == NULL)
            return FALSE;

          relative_url = g_key_file_get_string (key_file, submodule, "url", error);
          /* Remove any trailing whitespace */
          g_strchomp (relative_url);
          absolute_url = make_absolute (repo_location, relative_url, error);
          if (absolute_url == NULL)
            return FALSE;

          if (!git (mirror_dir, &ls_tree, 0, error, "ls-tree", revision, path, NULL))
            return FALSE;

          lines = g_strsplit (g_strstrip (ls_tree), "\n", 0);
          if (g_strv_length (lines) != 1)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Not a gitlink tree: %s", path);
              return FALSE;
            }

          words = g_strsplit_set (lines[0], " \t", 4);

          if (g_strcmp0 (words[0], "160000") != 0)
            continue;

          g_debug ("mirror submodule %s at revision %s\n", absolute_url, words[2]);
          if (shallow)
            {
              if (!builder_git_shallow_mirror_ref (absolute_url, destination_path, TRUE, words[2], context, error))
                return FALSE;
            }
          else
            {
              if (!builder_git_mirror_repo (absolute_url, destination_path, update, TRUE, disable_fsck, words[2], context, error))
                return FALSE;
            }
        }
    }

  return TRUE;
}

/* This mirrors the repo given by repo_location in a local
   directory. It tries to mirror only "ref", in a shallow way.
   However, this only works if ref is a tag or branch, or
   a commit id that is currently at the tip of a remote ref.
   If it is just a random commit id then we're forced to do
   a deep fetch of the entire remote repo.
*/
gboolean
builder_git_mirror_repo (const char     *repo_location,
                         const char     *destination_path,
                         gboolean        update,
                         gboolean        mirror_submodules,
                         gboolean        disable_fsck,
                         const char     *ref,
                         BuilderContext *context,
                         GError        **error)
{
  g_autoptr(GFile) cache_mirror_dir = NULL;
  g_autoptr(GFile) mirror_dir = NULL;
  g_autofree char *current_commit = NULL;
  g_autoptr(GHashTable) refs = NULL;
  gboolean already_exists = FALSE;

  cache_mirror_dir = git_get_mirror_dir (repo_location, context);

  if (destination_path != NULL)
    {
      g_autofree char *file_name = g_file_get_basename (cache_mirror_dir);
      g_autofree char *destination_file_path = g_build_filename (destination_path,
                                                                 file_name,
                                                                 NULL);
      mirror_dir = g_file_new_for_path (destination_file_path);
    }
  else
    mirror_dir = g_object_ref (cache_mirror_dir);

  if (!g_file_query_exists (mirror_dir, NULL))
    {
      if (!git (NULL, NULL, 0, error,
                "init", "--bare",
                (char *)flatpak_file_get_path_cached (mirror_dir), NULL))
        return FALSE;

      if (!git (mirror_dir, NULL, 0, error,
                "remote", "add", "--mirror=fetch", "origin",
                repo_location, NULL))
        return FALSE;
    }

  if (git (mirror_dir, NULL, G_SUBPROCESS_FLAGS_STDERR_SILENCE, NULL,
           "cat-file", "-e", ref, NULL))
    already_exists = TRUE;

  if (update || !already_exists)
    {
      g_autofree char *full_ref = NULL;
      g_autoptr(GFile) cached_git_dir = NULL;
      g_autofree char *origin = NULL;
      g_autoptr(GFile) alternates = NULL;
      g_autofree char *filename = g_file_get_basename (mirror_dir);

      /* If we're doing a regular download, look for cache sources */
      if (destination_path == NULL)
        cached_git_dir = builder_context_find_in_sources_dirs (context, "git", filename, NULL);
      else
        cached_git_dir = g_object_ref (cache_mirror_dir);

      /* If we're not updating, only pull from cache to avoid network i/o */
      if (!update && cached_git_dir)
        origin = g_file_get_uri (cached_git_dir);
      else
        origin = g_strdup ("origin");

      refs = git_ls_remote (mirror_dir, origin, error);
      if (refs == NULL)
        return FALSE;

      if (update && cached_git_dir)
        {
          const char *data = flatpak_file_get_path_cached (cached_git_dir);
          /* If we're updating, use the cache as a source of git objects */
          alternates = g_file_resolve_relative_path (mirror_dir, "objects/info/alternates");
          if (!g_file_replace_contents (alternates,
                                        data, strlen (data),
                                        NULL, FALSE,
                                        G_FILE_CREATE_REPLACE_DESTINATION,
                                        NULL, NULL, error))
            return FALSE;
        }

      full_ref = lookup_full_ref (refs, ref);
      if (full_ref)
        {
          g_autofree char *full_ref_mapping = g_strdup_printf ("+%s:%s", full_ref, full_ref);

          if (!git (mirror_dir, NULL, 0, error,
                    "config", "transfer.fsckObjects", disable_fsck ? "0" : "1", NULL))
            return FALSE;

          g_print ("Fetching git repo %s, ref %s\n", repo_location, full_ref);
          if (!git (mirror_dir, NULL, 0, error,
                    "fetch", "-p", "--no-recurse-submodules", "--no-tags", "--depth=1", "-f",
                    origin, full_ref_mapping, NULL))
            return FALSE;

	  /* It turns out that older versions of git (at least 2.7.4)
	   * cannot check out a commit unless a real tag/branch points
	   * to it, which is not the case for e.g. gitbug pull requests.
	   * So, to make this work we fake a branch for these cases.
	   * See https://github.com/flatpak/flatpak/issues/1133
	   */
	  if (!g_str_has_prefix (full_ref, "refs/heads") &&
	      !g_str_has_prefix (full_ref, "refs/tags"))
	    {
	      g_autofree char *fake_ref = g_strdup_printf ("refs/heads/flatpak-builder-internal/%s", full_ref);

	      if (!git (mirror_dir, NULL, 0, NULL,
			"update-ref", fake_ref, full_ref, NULL))
		return FALSE;
	    }
        }
      else if (!already_exists)
        /* We don't fetch everything if it already exists, because
           since it failed to resolve to full_ref it is a commit id
           which can't change and thus need no updates */
        {
          g_print ("Fetching full git repo %s\n", repo_location);
          if (!git (mirror_dir, NULL, 0, error,
                    "fetch", "-p", "--no-recurse-submodules", "--tags", origin, NULL))
            return FALSE;
        }

      if (alternates)
        {
          /* Ensure we copy the files from the cache, to be safe if the extra source changes */
          if (!git (mirror_dir, NULL, 0, error,
                    "repack", "-a", "-d", NULL))
            return FALSE;

          g_file_delete (alternates, NULL, NULL);
        }
    }

  if (mirror_submodules)
    {
      current_commit = git_get_current_commit (mirror_dir, ref, FALSE, context, error);
      if (current_commit == NULL)
        return FALSE;

      if (!git_mirror_submodules (repo_location, destination_path, FALSE, update,
                                  mirror_dir, disable_fsck, current_commit, context, error))
        return FALSE;
    }

  return TRUE;
}

/* In contrast with builder_git_mirror_repo this always does a shallow
   mirror. However, it only works for sources that are local, because
   it handles the case builder_git_mirror_repo fails at by creating refs
   in the source repo. */
gboolean
builder_git_shallow_mirror_ref (const char     *repo_location,
                                const char     *destination_path,
                                gboolean        mirror_submodules,
                                const char     *ref,
                                BuilderContext *context,
                                GError        **error)
{
  g_autoptr(GFile) cache_mirror_dir = NULL;
  g_autoptr(GFile) mirror_dir = NULL;
  g_autofree char *current_commit = NULL;
  g_autofree char *file_name = NULL;
  g_autofree char *destination_file_path = NULL;
  g_autofree char *full_ref = NULL;
  g_autofree char *full_ref_colon_full_ref = NULL;

  cache_mirror_dir = git_get_mirror_dir (repo_location, context);

  file_name = g_file_get_basename (cache_mirror_dir);
  destination_file_path = g_build_filename (destination_path,
                                            file_name,
                                            NULL);
  mirror_dir = g_file_new_for_path (destination_file_path);

  if (!g_file_query_exists (mirror_dir, NULL))
    {
      if (!git (NULL, NULL, 0, error,
                "init", "--bare", destination_file_path, NULL))
        return FALSE;
      if (!git (mirror_dir, NULL, 0, error,
                "remote", "add", "--mirror=fetch", "origin",
                (char *)flatpak_file_get_path_cached (cache_mirror_dir), NULL))
        return FALSE;
    }

  if (!git (cache_mirror_dir, &full_ref, 0, NULL,
            "rev-parse", "--symbolic-full-name", ref, NULL))
    return FALSE;

  /* Trim trailing whitespace */
  g_strchomp (full_ref);

  if (*full_ref == 0)
    {
      g_free (full_ref);
      /* We can't pull the commit id, so we create a ref we can pull */
      full_ref = g_strdup_printf ("refs/heads/flatpak-builder-internal/commit/%s", ref);
      if (!git (cache_mirror_dir, NULL, 0, NULL,
                "update-ref", full_ref, ref, NULL))
        return FALSE;
    }

  full_ref_colon_full_ref = g_strdup_printf ("%s:%s", full_ref, full_ref);
  if (!git (mirror_dir, NULL, 0, error,
            "fetch", "--depth", "1", "origin", full_ref_colon_full_ref, NULL))
    return FALSE;

  if (mirror_submodules)
    {
      current_commit = git_get_current_commit (mirror_dir, ref, FALSE, context, error);
      if (current_commit == NULL)
        return FALSE;

      if (!git_mirror_submodules (repo_location, destination_path, TRUE, FALSE,
                                  mirror_dir, TRUE, current_commit, context, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
git_extract_submodule (const char     *repo_location,
                       GFile          *checkout_dir,
                       const char     *revision,
                       BuilderContext *context,
                       GError        **error)
{
  g_autoptr(GKeyFile) key_file = g_key_file_new ();
  g_autofree gchar *rev_parse_output = NULL;
  g_autofree gchar *submodule_data = NULL;
  g_autofree gchar **submodules = NULL;
  g_autofree gchar *gitmodules = g_strconcat (revision, ":.gitmodules", NULL);
  gsize num_submodules;

  if (!git (checkout_dir, &rev_parse_output, 0, NULL, "rev-parse", "--verify", "--quiet", gitmodules, NULL))
    return TRUE;

  if (git (checkout_dir, &submodule_data, 0, NULL, "show", gitmodules, NULL))
    {
      if (!g_key_file_load_from_data (key_file, submodule_data, -1,
                                      G_KEY_FILE_NONE, error))
        return FALSE;

      submodules = g_key_file_get_groups (key_file, &num_submodules);

      int i;
      for (i = 0; i < num_submodules; i++)
        {
          g_autofree gchar *submodule = NULL;
          g_autofree gchar *name = NULL;
          g_autofree gchar *update_method = NULL;
          g_autofree gchar *path = NULL;
          g_autofree gchar *relative_url = NULL;
          g_autofree gchar *absolute_url = NULL;
          g_autofree gchar *ls_tree = NULL;
          g_auto(GStrv) lines = NULL;
          g_auto(GStrv) words = NULL;
          g_autoptr(GFile) mirror_dir = NULL;
          g_autoptr(GFile) child_dir = NULL;
          g_autofree gchar *mirror_dir_as_url = NULL;
          g_autofree gchar *option = NULL;
          gsize len;

          submodule = submodules[i];
          len = strlen (submodule);

          if (!g_str_has_prefix (submodule, "submodule \""))
            continue;

          name = g_strndup (submodule + 11, len - 12);

          /* Skip any submodules that are disabled (have the update method set to "none")
             Only check if the command succeeds. If it fails, the update method is not set. */
          update_method = g_key_file_get_string (key_file, submodule, "update", NULL);
          if (g_strcmp0 (update_method, "none") == 0)
            continue;

          path = g_key_file_get_string (key_file, submodule, "path", error);
          if (path == NULL)
            return FALSE;

          relative_url = g_key_file_get_string (key_file, submodule, "url", error);
          absolute_url = make_absolute (repo_location, relative_url, error);
          if (absolute_url == NULL)
            return FALSE;

          if (!git (checkout_dir, &ls_tree, 0, error, "ls-tree", revision, path, NULL))
            return FALSE;

          lines = g_strsplit (g_strstrip (ls_tree), "\n", 0);
          if (g_strv_length (lines) != 1)
            {
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Not a gitlink tree: %s", path);
              return FALSE;
            }

          words = g_strsplit_set (lines[0], " \t", 4);

          if (g_strcmp0 (words[0], "160000") != 0)
            continue;

          mirror_dir = git_get_mirror_dir (absolute_url, context);
          mirror_dir_as_url = g_file_get_uri (mirror_dir);
          option = g_strdup_printf ("submodule.%s.url", name);

          if (!git (checkout_dir, NULL, 0, error,
                    "config", option, mirror_dir_as_url, NULL))
            return FALSE;

          if (!git (checkout_dir, NULL, 0, error,
                    "submodule", "update", "--init", path, NULL))
            return FALSE;

          child_dir = g_file_resolve_relative_path (checkout_dir, path);

          if (!git_extract_submodule (absolute_url, child_dir, words[2], context, error))
            return FALSE;
        }
    }

  return TRUE;
}

gboolean
builder_git_checkout_dir (const char     *repo_location,
                          const char     *branch,
                          const char     *dir,
                          GFile          *dest,
                          BuilderContext *context,
                          GError        **error)
{
  g_autoptr(GFile) mirror_dir = NULL;
  g_autofree char *mirror_dir_path = NULL;
  g_autofree char *dest_path = NULL;

  mirror_dir = git_get_mirror_dir (repo_location, context);

  mirror_dir_path = g_file_get_path (mirror_dir);
  dest_path = g_file_get_path (dest);

  if (!git (NULL, NULL, 0, error,
            "clone", "-n", mirror_dir_path, dest_path, NULL))
    return FALSE;

  if (!git (dest, NULL, 0, error,
            "checkout", branch, "--", dir ? dir : ".", NULL))
    return FALSE;

  return TRUE;
}

gboolean
builder_git_checkout (const char     *repo_location,
                      const char     *branch,
                      GFile          *dest,
                      BuilderContext *context,
                      GError        **error)
{
  g_autoptr(GFile) mirror_dir = NULL;
  g_autofree char *mirror_dir_path = NULL;
  g_autofree char *dest_path = NULL;
  g_autofree char *dest_path_git = NULL;

  mirror_dir = git_get_mirror_dir (repo_location, context);

  mirror_dir_path = g_file_get_path (mirror_dir);
  dest_path = g_file_get_path (dest);
  dest_path_git = g_build_filename (dest_path, ".git", NULL);

  /* We need to clone with --mirror so that we get all refs, including non-branch/tags */
  if (!git (NULL, NULL, 0, error,
            "clone",
            "--mirror",
            mirror_dir_path, dest_path_git, NULL))
    return FALSE;

  /* Then we need to convert to regular */
  if (!git (dest, NULL, 0, error,
            "config", "--bool", "core.bare", "false", NULL))
    return FALSE;

  if (!git (dest, NULL, 0, error,
            "checkout", branch, NULL))
    return FALSE;

  if (!git_extract_submodule (repo_location, dest, branch, context, error))
    return FALSE;

  if (!git (dest, NULL, 0, error,
            "config", "--local", "remote.origin.url",
            repo_location, NULL))
    return FALSE;

  return TRUE;
}
