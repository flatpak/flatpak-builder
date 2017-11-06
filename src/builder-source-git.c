/* builder-source-git.c
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

#include "builder-source-git.h"
#include "builder-git.h"
#include "builder-utils.h"
#include "builder-flatpak-utils.h"

struct BuilderSourceGit
{
  BuilderSource parent;

  char         *url;
  char         *path;
  char         *branch;
  char         *tag;
  char         *commit;
  char         *orig_ref;
  gboolean      disable_fsckobjects;
  gboolean      disable_shallow_clone;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourceGitClass;

G_DEFINE_TYPE (BuilderSourceGit, builder_source_git, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_URL,
  PROP_PATH,
  PROP_BRANCH,
  PROP_TAG,
  PROP_COMMIT,
  PROP_DISABLE_FSCKOBJECTS,
  PROP_DISABLE_SHALLOW_CLONE,
  LAST_PROP
};

static void
builder_source_git_finalize (GObject *object)
{
  BuilderSourceGit *self = (BuilderSourceGit *) object;

  g_free (self->url);
  g_free (self->path);
  g_free (self->branch);
  g_free (self->tag);
  g_free (self->commit);
  g_free (self->orig_ref);

  G_OBJECT_CLASS (builder_source_git_parent_class)->finalize (object);
}

static void
builder_source_git_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BuilderSourceGit *self = BUILDER_SOURCE_GIT (object);

  switch (prop_id)
    {
    case PROP_URL:
      g_value_set_string (value, self->url);
      break;

    case PROP_PATH:
      g_value_set_string (value, self->path);
      break;

    case PROP_BRANCH:
      g_value_set_string (value, self->branch);
      break;

    case PROP_TAG:
      g_value_set_string (value, self->tag);
      break;

    case PROP_COMMIT:
      g_value_set_string (value, self->commit);
      break;

    case PROP_DISABLE_FSCKOBJECTS:
      g_value_set_boolean (value, self->disable_fsckobjects);
      break;

    case PROP_DISABLE_SHALLOW_CLONE:
      g_value_set_boolean (value, self->disable_shallow_clone);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_git_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BuilderSourceGit *self = BUILDER_SOURCE_GIT (object);

  switch (prop_id)
    {
    case PROP_URL:
      g_free (self->url);
      self->url = g_value_dup_string (value);
      break;

    case PROP_PATH:
      g_free (self->path);
      self->path = g_value_dup_string (value);
      break;

    case PROP_BRANCH:
      g_free (self->branch);
      self->branch = g_value_dup_string (value);
      break;

    case PROP_TAG:
      g_free (self->tag);
      self->tag = g_value_dup_string (value);
      break;

    case PROP_COMMIT:
      g_free (self->commit);
      self->commit = g_value_dup_string (value);
      break;

    case PROP_DISABLE_FSCKOBJECTS:
      self->disable_fsckobjects = g_value_get_boolean (value);
      break;

    case PROP_DISABLE_SHALLOW_CLONE:
      self->disable_shallow_clone = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static const char *
get_branch (BuilderSourceGit *self)
{
  if (self->branch)
    return self->branch;
  else if (self->tag)
    return self->tag;
  else if (self->commit)
    return self->commit;
  else
    return "master";
}

static char *
get_url_or_path (BuilderSourceGit *self,
                 BuilderContext   *context,
                 GError          **error)
{
  g_autoptr(GFile) repo = NULL;
  GFile *base_dir = BUILDER_SOURCE (self)->base_dir;

  if (self->url == NULL && self->path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No URL or path specified");
      return NULL;
    }

  if (self->url)
    {
      g_autofree char *scheme = NULL;
      scheme = g_uri_parse_scheme (self->url);
      if (scheme == NULL)
        {
          repo = g_file_resolve_relative_path (base_dir, self->url);
          return g_file_get_uri (repo);
        }

      return g_strdup (self->url);
    }

  repo = g_file_resolve_relative_path (base_dir,
                                       self->path);
  return g_file_get_path (repo);
}

static gboolean
builder_source_git_download (BuilderSource  *source,
                             gboolean        update_vcs,
                             BuilderContext *context,
                             GError        **error)
{
  BuilderSourceGit *self = BUILDER_SOURCE_GIT (source);
  g_autofree char *location = NULL;
  FlatpakGitMirrorFlags flags;

  location = get_url_or_path (self, context, error);
  if (location == NULL)
    return FALSE;

  if (self->tag != NULL && self->branch != NULL)
    return flatpak_fail (error, "Both tag (%s) and branch (%s) specified for git source", self->tag, self->branch);

  flags = FLATPAK_GIT_MIRROR_FLAGS_MIRROR_SUBMODULES;
  if (update_vcs)
    flags |= FLATPAK_GIT_MIRROR_FLAGS_UPDATE;
  if (self->disable_fsckobjects)
    flags |= FLATPAK_GIT_MIRROR_FLAGS_DISABLE_FSCK;
  if (self->disable_shallow_clone)
    flags |= FLATPAK_GIT_MIRROR_FLAGS_DISABLE_SHALLOW;
  if (builder_context_get_bundle_sources (context))
    flags |= FLATPAK_GIT_MIRROR_FLAGS_WILL_FETCH_FROM;

  if (!builder_git_mirror_repo (location, NULL, flags,
                                get_branch (self),
                                context,
                                error))
    return FALSE;

  if (self->commit != NULL && (self->branch != NULL || self->tag != NULL))
    {
      /* We want to support the commit being both a tag object and the real commit object that it points too */
      g_autofree char *current_commit = builder_git_get_current_commit (location, get_branch (self), FALSE, context, error);
      g_autofree char *current_commit2 = builder_git_get_current_commit (location, get_branch (self), TRUE, context, error);
      if (current_commit == NULL || current_commit2 == NULL)
        return FALSE;
      if (strcmp (current_commit, self->commit) != 0 && strcmp (current_commit2, self->commit) != 0)
        return flatpak_fail (error, "Git commit for branch %s is %s, but expected %s", self->branch, current_commit2, self->commit);
    }

  return TRUE;
}

static gboolean
builder_source_git_extract (BuilderSource  *source,
                            GFile          *dest,
                            BuilderOptions *build_options,
                            BuilderContext *context,
                            GError        **error)
{
  BuilderSourceGit *self = BUILDER_SOURCE_GIT (source);
  g_autofree char *location = NULL;

  location = get_url_or_path (self, context, error);
  if (location == NULL)
    return FALSE;

  if (!builder_git_checkout (location, get_branch (self),
                             dest, context, error))
    return FALSE;

  return TRUE;
}

static gboolean
builder_source_git_bundle (BuilderSource  *source,
                           BuilderContext *context,
                           GError        **error)
{
  BuilderSourceGit *self = BUILDER_SOURCE_GIT (source);

  g_autofree char *location = NULL;
  g_autoptr(GFile) mirror_dir = NULL;

  location = get_url_or_path (self, context, error);

  g_print ("builder_source_git_bundle %s\n", location);

  if (location == NULL)
    return FALSE;

  mirror_dir = flatpak_build_file (builder_context_get_app_dir (context),
                                   "sources/git",
                                   NULL);
  if (!flatpak_mkdir_p (mirror_dir, NULL, error))
    return FALSE;

  if (!builder_git_shallow_mirror_ref (location,
                                       flatpak_file_get_path_cached (mirror_dir),
                                       self->orig_ref,
                                       context,
                                       error))
    return FALSE;

  return TRUE;
}

static void
builder_source_git_checksum (BuilderSource  *source,
                             BuilderCache   *cache,
                             BuilderContext *context)
{
  BuilderSourceGit *self = BUILDER_SOURCE_GIT (source);
  g_autofree char *current_commit = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *location = NULL;

  builder_cache_checksum_str (cache, self->url);
  builder_cache_checksum_str (cache, self->path);
  builder_cache_checksum_str (cache, self->branch);
  builder_cache_checksum_str (cache, self->commit);
  builder_cache_checksum_boolean (cache, self->disable_fsckobjects);
  /* We don't checksum disable_shallow_clone, because it doesn't have
     any effect on the resultant build */

  location = get_url_or_path (self, context, &error);
  if (location != NULL)
    {
      current_commit = builder_git_get_current_commit (location,get_branch (self), FALSE, context, &error);
      if (current_commit)
        builder_cache_checksum_str (cache, current_commit);
      else if (error)
        g_warning ("Failed to get current git checksum: %s", error->message);
    }
  else
    {
      g_warning ("No url or path");
    }
}

static gboolean
builder_source_git_update (BuilderSource  *source,
                           BuilderContext *context,
                           GError        **error)
{
  BuilderSourceGit *self = BUILDER_SOURCE_GIT (source);
  char *current_commit;
  g_autofree char *location = NULL;

  location = get_url_or_path (self, context, error);
  if (location == NULL)
    return FALSE;

  self->orig_ref = g_strdup (get_branch (self));
  current_commit = builder_git_get_current_commit (location, self->orig_ref, FALSE, context, NULL);
  if (current_commit)
    {
      g_free (self->branch);
      self->branch = NULL;
      g_free (self->tag);
      self->tag = NULL;
      g_free (self->commit);
      self->commit = current_commit;
    }

  return TRUE;
}

static void
builder_source_git_class_init (BuilderSourceGitClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_git_finalize;
  object_class->get_property = builder_source_git_get_property;
  object_class->set_property = builder_source_git_set_property;

  source_class->download = builder_source_git_download;
  source_class->extract = builder_source_git_extract;
  source_class->bundle = builder_source_git_bundle;
  source_class->update = builder_source_git_update;
  source_class->checksum = builder_source_git_checksum;

  g_object_class_install_property (object_class,
                                   PROP_URL,
                                   g_param_spec_string ("url",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BRANCH,
                                   g_param_spec_string ("branch",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_TAG,
                                   g_param_spec_string ("tag",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_COMMIT,
                                   g_param_spec_string ("commit",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_DISABLE_FSCKOBJECTS,
                                   g_param_spec_boolean ("disable-fsckobjects",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_DISABLE_SHALLOW_CLONE,
                                   g_param_spec_boolean ("disable-shallow-clone",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
}

static void
builder_source_git_init (BuilderSourceGit *self)
{
}
