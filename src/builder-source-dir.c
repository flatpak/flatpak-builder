/* builder-source-dir.c
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

#include "builder-flatpak-utils.h"

#include "builder-utils.h"
#include "builder-source-dir.h"

struct BuilderSourceDir
{
  BuilderSource parent;

  char         *path;
  char        **skip;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourceDirClass;

G_DEFINE_TYPE (BuilderSourceDir, builder_source_dir, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_PATH,
  PROP_SKIP,
  LAST_PROP
};

static void
builder_source_dir_finalize (GObject *object)
{
  BuilderSourceDir *self = (BuilderSourceDir *) object;

  g_free (self->path);
  g_strfreev (self->skip);

  G_OBJECT_CLASS (builder_source_dir_parent_class)->finalize (object);
}

static void
builder_source_dir_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BuilderSourceDir *self = BUILDER_SOURCE_DIR (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, self->path);
      break;

    case PROP_SKIP:
      g_value_set_boxed (value, self->skip);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_dir_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BuilderSourceDir *self = BUILDER_SOURCE_DIR (object);
  gchar **tmp;

  switch (prop_id)
    {
    case PROP_PATH:
      g_free (self->path);
      self->path = g_value_dup_string (value);
      break;

    case PROP_SKIP:
      tmp = self->skip;
      self->skip = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GFile *
get_source_file (BuilderSourceDir *self,
                 BuilderContext    *context,
                 GError           **error)
{
  GFile *base_dir = BUILDER_SOURCE (self)->base_dir;

  if (self->path != NULL && self->path[0] != 0)
    {
      g_autoptr(GFile) file = NULL;
      file = g_file_resolve_relative_path (base_dir, self->path);

      if (!builder_context_ensure_file_sandboxed (context, file, error))
        {
          g_prefix_error (error, "Unable to get source file '%s': ", self->path);
          return NULL;
        }

      return g_steal_pointer (&file);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "source dir path not specified");
  return NULL;
}

static gboolean
builder_source_dir_show_deps (BuilderSource  *source,
                              GError        **error)
{
  BuilderSourceDir *self = BUILDER_SOURCE_DIR (source);

  if (self->path && self->path[0] != 0)
    g_print ("%s\n", self->path);

  return TRUE;
}

static gboolean
builder_source_dir_download (BuilderSource  *source,
                             gboolean        update_vcs,
                             BuilderContext *context,
                             GError        **error)
{
  BuilderSourceDir *self = BUILDER_SOURCE_DIR (source);
  g_autoptr(GFile) file = NULL;

  file = get_source_file (self, context, error);
  if (file == NULL)
    return FALSE;

  if (g_file_query_file_type (file, G_FILE_QUERY_INFO_NONE, NULL) != G_FILE_TYPE_DIRECTORY)
    return flatpak_fail (error, "Can't find directory at %s", self->path);

  return TRUE;
}

static GPtrArray *
builder_source_dir_get_skip (BuilderSource  *source,
                             BuilderContext *context)
{
  BuilderSourceDir *self = BUILDER_SOURCE_DIR (source);
  g_autoptr(GPtrArray) skip = g_ptr_array_new_with_free_func (g_object_unref);
  int i;

  g_ptr_array_add (skip, g_object_ref (builder_context_get_app_dir_raw (context)));
  g_ptr_array_add (skip, g_object_ref (builder_context_get_state_dir (context)));

  if (self->skip)
    {
      g_autoptr(GFile) source_dir = get_source_file (self, context, NULL);

      for (i = 0; source_dir != NULL && self->skip[i] != NULL; i++)
        {
          GFile *f = g_file_resolve_relative_path (source_dir, self->skip[i]);
          if (f)
            g_ptr_array_add (skip, f);
        }
    }

  return g_steal_pointer (&skip);
}

static gboolean
builder_source_dir_extract (BuilderSource  *source,
                            GFile          *dest,
                            GFile          *source_dir,
                            BuilderOptions *build_options,
                            BuilderContext *context,
                            GError        **error)
{
  BuilderSourceDir *self = BUILDER_SOURCE_DIR (source);
  g_autoptr(GFile) src = NULL;
  g_autoptr(GPtrArray) skip = NULL;

  src = get_source_file (self, context, error);
  if (src == NULL)
    return FALSE;

  skip = builder_source_dir_get_skip (source, context);
  if (!flatpak_cp_a (src, dest, source_dir,
                     FLATPAK_CP_FLAGS_MERGE|FLATPAK_CP_FLAGS_NO_CHOWN,
                     skip, NULL, error))
    return FALSE;

  return TRUE;
}

static gboolean
builder_source_dir_bundle (BuilderSource  *source,
                            BuilderContext *context,
                            GError        **error)
{
  BuilderSourceDir *self = BUILDER_SOURCE_DIR (source);
  g_autoptr(GFile) src = NULL;
  g_autoptr(GFile) dest = NULL;
  GFile *manifest_base_dir = builder_context_get_base_dir (context);
  g_autofree char *rel_path = NULL;
  g_autoptr(GPtrArray) skip = NULL;

  src = get_source_file (self, context, error);
  if (src == NULL)
    return FALSE;

  rel_path = g_file_get_relative_path (manifest_base_dir, src);
  if (rel_path == NULL)
    {
      g_warning ("Local file %s is outside manifest tree, not bundling", flatpak_file_get_path_cached (src));
      return TRUE;
    }

  dest = flatpak_build_file (builder_context_get_app_dir (context),
                             "sources/manifest", rel_path, NULL);

  skip = builder_source_dir_get_skip (source, context);
  g_mkdir_with_parents (flatpak_file_get_path_cached (dest), 0755);
  if (!flatpak_cp_a (src, dest, NULL,
                     FLATPAK_CP_FLAGS_MERGE|FLATPAK_CP_FLAGS_NO_CHOWN,
                     skip, NULL, error))
    return FALSE;

  return TRUE;
}

static gboolean
builder_source_dir_update (BuilderSource  *source,
                            BuilderContext *context,
                            GError        **error)
{
  return TRUE;
}

static void
builder_source_dir_checksum (BuilderSource  *source,
                              BuilderCache   *cache,
                              BuilderContext *context)
{
  /* We can't realistically checksum a directory, so always rebuild */
  builder_cache_checksum_random (cache);
}

static void
builder_source_dir_class_init (BuilderSourceDirClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_dir_finalize;
  object_class->get_property = builder_source_dir_get_property;
  object_class->set_property = builder_source_dir_set_property;

  source_class->show_deps = builder_source_dir_show_deps;
  source_class->download = builder_source_dir_download;
  source_class->extract = builder_source_dir_extract;
  source_class->bundle = builder_source_dir_bundle;
  source_class->update = builder_source_dir_update;
  source_class->checksum = builder_source_dir_checksum;

  g_object_class_install_property (object_class,
                                   PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SKIP,
                                   g_param_spec_boxed ("skip",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
}

static void
builder_source_dir_init (BuilderSourceDir *self)
{
}
