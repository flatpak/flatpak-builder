/* builder-source-svn.c
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

#include "builder-source-svn.h"
#include "builder-utils.h"
#include "builder-flatpak-utils.h"

struct BuilderSourceSvn
{
  BuilderSource parent;

  char         *url;
  char         *revision;
  char         *orig_revision;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourceSvnClass;

G_DEFINE_TYPE (BuilderSourceSvn, builder_source_svn, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_URL,
  PROP_REVISION,
  LAST_PROP
};

static gboolean
svn (GFile   *dir,
     char   **output,
     GError **error,
     ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (dir, output, 0, error, "svn", ap);
  va_end (ap);

  return res;
}

static gboolean
cp (GError **error,
     ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (NULL, NULL, 0, error, "cp", ap);
  va_end (ap);

  return res;
}

static gboolean
cp_dir (GFile *src_dir, GFile *dst_dir, GError **error)
{
  g_autofree char *src_path = g_strconcat (flatpak_file_get_path_cached (src_dir), "/", NULL);
  g_autofree char *dst_path = g_strconcat (flatpak_file_get_path_cached (dst_dir), "/", NULL);

  if (!cp (error, "-aT", src_path, dst_path, NULL))
    return FALSE;

  return TRUE;
}

static void
builder_source_svn_finalize (GObject *object)
{
  BuilderSourceSvn *self = (BuilderSourceSvn *) object;

  g_free (self->url);
  g_free (self->revision);
  g_free (self->orig_revision);

  G_OBJECT_CLASS (builder_source_svn_parent_class)->finalize (object);
}

static void
builder_source_svn_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BuilderSourceSvn *self = BUILDER_SOURCE_SVN (object);

  switch (prop_id)
    {
    case PROP_URL:
      g_value_set_string (value, self->url);
      break;

    case PROP_REVISION:
      g_value_set_string (value, self->revision);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_svn_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BuilderSourceSvn *self = BUILDER_SOURCE_SVN (object);

  switch (prop_id)
    {
    case PROP_URL:
      g_free (self->url);
      self->url = g_value_dup_string (value);
      break;

    case PROP_REVISION:
      g_free (self->revision);
      self->revision = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GFile *
get_mirror_dir (BuilderSourceSvn *self, BuilderContext *context, const char *revision)
{
  g_autoptr(GFile) svn_dir = NULL;
  g_autofree char *uri_filename = NULL;
  g_autofree char *filename = NULL;
  g_autofree char *svn_dir_path = NULL;

  svn_dir = g_file_get_child (builder_context_get_state_dir (context),
                              "svn");

  svn_dir_path = g_file_get_path (svn_dir);
  g_mkdir_with_parents (svn_dir_path, 0755);

  uri_filename = builder_uri_to_filename (self->url);
  if (revision)
    filename = g_strconcat (uri_filename, "__r", revision, NULL);
  else
    filename = g_strdup (uri_filename);

  return g_file_get_child (svn_dir, filename);
}

static char *
get_current_revision (BuilderSourceSvn *self, BuilderContext *context, GError **error)
{
  g_autoptr(GFile) mirror_dir = NULL;
  char *output = NULL;

  mirror_dir = get_mirror_dir (self, context, self->revision);

  if (!svn (mirror_dir, &output, error,
            "info", "--non-interactive", "--show-item", "revision", NULL))
    return NULL;

  /* Trim trailing whitespace */
  g_strchomp (output);

  return output;
}

static gboolean
builder_source_svn_download (BuilderSource  *source,
                             gboolean        update_vcs,
                             BuilderContext *context,
                             GError        **error)
{
  BuilderSourceSvn *self = BUILDER_SOURCE_SVN (source);
  g_autoptr(GFile) parent = NULL;
  g_autofree char *filename = NULL;

  g_autoptr(GFile) mirror_dir = NULL;

  if (self->url == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "URL not specified");
      return FALSE;
    }

  mirror_dir = get_mirror_dir (self, context, self->revision);
  parent = g_file_get_parent (mirror_dir);
  filename = g_file_get_basename (mirror_dir);

  if (!g_file_query_exists (mirror_dir, NULL))
    {
      g_autofree char *mirror_path = g_file_get_path (mirror_dir);
      g_autofree char *path_tmp = g_strconcat (mirror_path, ".clone_XXXXXX", NULL);
      g_autofree char *filename_tmp = NULL;
      g_autoptr(GFile) mirror_dir_tmp = NULL;
      g_autoptr(GFile) cached_svn_dir = NULL;

      if (g_mkdtemp_full (path_tmp, 0755) == NULL)
        return flatpak_fail (error, "Can't create temporary directory");

      mirror_dir_tmp = g_file_new_for_path (path_tmp);
      filename_tmp = g_file_get_basename (mirror_dir_tmp);

      cached_svn_dir = builder_context_find_in_sources_dirs (context, "svn", filename, NULL);
      if (cached_svn_dir != NULL)
        {
          if (!cp_dir (cached_svn_dir, mirror_dir_tmp, error))
            return FALSE;

          if (update_vcs)
            {
              g_print ("Updating svn repo %s\n", self->url);

              if (!svn (parent, NULL, error,
                        "update", "--non-interactive",
                        "-r", self->revision ? self->revision : "HEAD",
                        filename_tmp, NULL))
                return FALSE;
            }
        }
      else
        {
          g_print ("Getting svn repo %s\n", self->url);

          if (!svn (parent, NULL, error,
                    "checkout", "--non-interactive",
                    "-r", self->revision ? self->revision : "HEAD",
                    self->url,  filename_tmp, NULL))
            return FALSE;
        }


      if (!g_file_move (mirror_dir_tmp, mirror_dir, 0, NULL, NULL, NULL, error))
        return FALSE;
    }
  else if (update_vcs)
    {
      g_print ("Updating svn repo %s\n", self->url);

      if (!svn (parent, NULL, error,
                "update", "--non-interactive",
                "-r", self->revision ? self->revision : "HEAD",
                filename, NULL))
        return FALSE;
    }

  return TRUE;
}

static gboolean
builder_source_svn_extract (BuilderSource  *source,
                            GFile          *dest,
                            GFile          *source_dir,
                            BuilderOptions *build_options,
                            BuilderContext *context,
                            GError        **error)
{
  BuilderSourceSvn *self = BUILDER_SOURCE_SVN (source);
  g_autoptr(GFile) mirror_dir = NULL;
  g_autofree char *dest_path = NULL;

  mirror_dir = get_mirror_dir (self, context, self->revision);

  dest_path = g_file_get_path (dest);
  g_mkdir_with_parents (dest_path, 0755);

  if (!cp_dir (mirror_dir, dest, error))
    return FALSE;

  return TRUE;
}

static gboolean
builder_source_svn_bundle (BuilderSource  *source,
                           BuilderContext *context,
                           GError        **error)
{
  BuilderSourceSvn *self = BUILDER_SOURCE_SVN (source);
  g_autoptr(GFile) mirror_dir = NULL;
  g_autoptr(GFile) dest_dir = NULL;
  g_autofree char *base_name = NULL;

  mirror_dir = get_mirror_dir (self, context, self->orig_revision);

  if (mirror_dir == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't locate repo with URL '%s'", self->url);
      return FALSE;
    }

  base_name = g_file_get_basename (mirror_dir);

  dest_dir = flatpak_build_file (builder_context_get_app_dir (context),
                                 "sources/svn", base_name,
                                 NULL);

  if (!flatpak_mkdir_p (dest_dir, NULL, error))
    return FALSE;

  if (!cp_dir (mirror_dir, dest_dir, error))
    return FALSE;

  if (self->orig_revision == NULL)
    {
      g_autoptr(GFile) alt_mirror_dir = get_mirror_dir (self, context, self->revision);
      g_autofree char *alt_base_name = g_file_get_basename (alt_mirror_dir);
      g_autoptr(GFile) alt_dest_dir = flatpak_build_file (builder_context_get_app_dir (context),
                                                          "sources/svn", alt_base_name,
                                                          NULL);
      g_print ("alt_mirror_dir: %s (basename %s)\n", g_file_get_path (alt_mirror_dir), base_name);
      if (!g_file_query_exists (alt_dest_dir, NULL) &&
          !g_file_make_symbolic_link (alt_dest_dir, base_name, NULL, error))
        return FALSE;
    }

  return TRUE;
}

static void
builder_source_svn_checksum (BuilderSource  *source,
                             BuilderCache   *cache,
                             BuilderContext *context)
{
  BuilderSourceSvn *self = BUILDER_SOURCE_SVN (source);
  g_autofree char *current_revision = NULL;

  g_autoptr(GError) error = NULL;

  builder_cache_checksum_str (cache, self->url);
  builder_cache_checksum_str (cache, self->revision);

  current_revision = get_current_revision (self, context, &error);
  if (current_revision)
    builder_cache_checksum_str (cache, current_revision);
  else if (error)
    g_warning ("Failed to get current svn revision: %s", error->message);
}

static gboolean
builder_source_svn_update (BuilderSource  *source,
                           BuilderContext *context,
                           GError        **error)
{
  BuilderSourceSvn *self = BUILDER_SOURCE_SVN (source);
  char *current_revision;

  self->orig_revision = g_strdup (self->revision);

  current_revision = get_current_revision (self, context, NULL);
  if (current_revision)
    {
      g_free (self->revision);
      self->revision = current_revision;
    }

  return TRUE;
}

static void
builder_source_svn_class_init (BuilderSourceSvnClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_svn_finalize;
  object_class->get_property = builder_source_svn_get_property;
  object_class->set_property = builder_source_svn_set_property;

  source_class->download = builder_source_svn_download;
  source_class->extract = builder_source_svn_extract;
  source_class->bundle = builder_source_svn_bundle;
  source_class->update = builder_source_svn_update;
  source_class->checksum = builder_source_svn_checksum;

  g_object_class_install_property (object_class,
                                   PROP_URL,
                                   g_param_spec_string ("url",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_REVISION,
                                   g_param_spec_string ("revision",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
}

static void
builder_source_svn_init (BuilderSourceSvn *self)
{
}
