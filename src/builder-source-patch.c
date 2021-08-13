/* builder-source-patch.c
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
#include "builder-source-patch.h"

struct BuilderSourcePatch
{
  BuilderSource parent;

  char         *path;
  char        **paths;
  guint         strip_components;
  gboolean      use_git;
  gboolean      use_git_am;
  char        **options;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourcePatchClass;

G_DEFINE_TYPE (BuilderSourcePatch, builder_source_patch, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_PATH,
  PROP_PATHS,
  PROP_STRIP_COMPONENTS,
  PROP_USE_GIT,
  PROP_OPTIONS,
  PROP_USE_GIT_AM,
  LAST_PROP
};

static void
builder_source_patch_finalize (GObject *object)
{
  BuilderSourcePatch *self = (BuilderSourcePatch *) object;

  g_free (self->path);
  g_strfreev (self->paths);
  g_strfreev (self->options);

  G_OBJECT_CLASS (builder_source_patch_parent_class)->finalize (object);
}

static void
builder_source_patch_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  BuilderSourcePatch *self = BUILDER_SOURCE_PATCH (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, self->path);
      break;

    case PROP_PATHS:
      g_value_set_boxed (value, self->paths);
      break;

    case PROP_STRIP_COMPONENTS:
      g_value_set_uint (value, self->strip_components);
      break;

    case PROP_USE_GIT:
      g_value_set_boolean (value, self->use_git);
      break;

    case PROP_OPTIONS:
      g_value_set_boxed (value, self->options);
      break;

    case PROP_USE_GIT_AM:
      g_value_set_boolean (value, self->use_git_am);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_patch_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  BuilderSourcePatch *self = BUILDER_SOURCE_PATCH (object);
  gchar **tmp;

  switch (prop_id)
    {
    case PROP_PATH:
      g_free (self->path);
      self->path = g_value_dup_string (value);
      break;

    case PROP_PATHS:
      tmp = self->paths;
      self->paths = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_STRIP_COMPONENTS:
      self->strip_components = g_value_get_uint (value);
      break;

    case PROP_USE_GIT:
      self->use_git = g_value_get_boolean (value);
      break;

    case PROP_OPTIONS:
      tmp = self->options;
      self->options = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_USE_GIT_AM:
      self->use_git_am = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GPtrArray *
get_source_files (BuilderSourcePatch *self,
                 BuilderContext     *context,
                 GError            **error)
{
  g_autoptr(GPtrArray) res = g_ptr_array_new_with_free_func (g_object_unref);
  GFile *base_dir = BUILDER_SOURCE (self)->base_dir;
  int i;

  if ((self->path != NULL && self->path[0] != 0))
    g_ptr_array_add (res, g_file_resolve_relative_path (base_dir, self->path));

  if (self->paths != NULL)
    {
      for (i = 0; self->paths[i] != NULL; i++)
        g_ptr_array_add (res, g_file_resolve_relative_path (base_dir, self->paths[i]));
    }

  if (res->len == 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "path not specified");
      return NULL;
    }

  return g_steal_pointer (&res);
}

static gboolean
builder_source_patch_show_deps (BuilderSource  *source,
                                GError        **error)
{
  BuilderSourcePatch *self = BUILDER_SOURCE_PATCH (source);
  int i;

  if (self->path && self->path[0] != 0)
    g_print ("%s\n", self->path);

  for (i = 0; self->paths != NULL && self->paths[i] != NULL; i++)
    g_print ("%s\n", self->paths[i]);

  return TRUE;
}

static gboolean
builder_source_patch_download (BuilderSource  *source,
                               gboolean        update_vcs,
                               BuilderContext *context,
                               GError        **error)
{
  BuilderSourcePatch *self = BUILDER_SOURCE_PATCH (source);
  g_autoptr(GPtrArray) srcs = NULL;
  int i;

  srcs = get_source_files (self, context, error);
  if (srcs == NULL)
    return FALSE;

  for (i = 0; i < srcs->len; i++)
    {
      GFile *src = g_ptr_array_index (srcs, i);
      if (!g_file_query_exists (src, NULL))
        {
          GFile *base_dir = BUILDER_SOURCE (self)->base_dir;
          g_autofree char *path = g_file_get_relative_path (base_dir, src);
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't find file at %s", path);
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
patch (GFile      *dir,
       gboolean    use_git,
       gboolean    use_git_am,
       const char *patch_path,
       char      **extra_options,
       GError    **error,
       ...)
{
  gboolean res;
  GPtrArray *args;
  const gchar *arg;
  va_list ap;
  int i;

  va_start(ap, error);

  args = g_ptr_array_new ();
  if (use_git) {
    g_ptr_array_add (args, "git");
    g_ptr_array_add (args, "apply");
    g_ptr_array_add (args, "-v");
  } else if (use_git_am) {
    g_ptr_array_add (args, "git");
    g_ptr_array_add (args, "am");
    g_ptr_array_add (args, "--keep-cr");
  } else {
    g_ptr_array_add (args, "patch");
  }
  for (i = 0; extra_options != NULL && extra_options[i] != NULL; i++)
    g_ptr_array_add (args, (gchar *) extra_options[i]);
  while ((arg = va_arg (ap, const gchar *)))
    g_ptr_array_add (args, (gchar *) arg);
  if (use_git || use_git_am) {
    g_ptr_array_add (args, (char *) patch_path);
  } else {
    g_ptr_array_add (args, "-i");
    g_ptr_array_add (args, (char *) patch_path);
  }
  g_ptr_array_add (args, NULL);

  res = flatpak_spawnv (dir, NULL, 0, error, (const char **) args->pdata, NULL);

  g_ptr_array_free (args, TRUE);

  va_end (ap);

  return res;
}

static gboolean
builder_source_patch_extract (BuilderSource  *source,
                              GFile          *dest,
                              GFile          *source_dir,
                              BuilderOptions *build_options,
                              BuilderContext *context,
                              GError        **error)
{
  BuilderSourcePatch *self = BUILDER_SOURCE_PATCH (source);
  g_autofree char *strip_components = NULL;
  g_autoptr(GPtrArray) srcs = NULL;
  int i;

  if (self->use_git && self->use_git_am)
    {
      const char *path = self->path;
      if (path == NULL && self->paths != NULL)
        path = self->paths[0];
      if (path == NULL)
        path = "<unset>";

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Patch '%s' cannot be applied: Both 'use-git' and 'use-git-am' are set. Only one can be set at a time",
                   path);
      return FALSE;
    }

  srcs = get_source_files (self, context, error);
  if (srcs == NULL)
    return FALSE;

  strip_components = g_strdup_printf ("-p%u", self->strip_components);

  for (i = 0; i < srcs->len; i++)
    {
      GFile *patchfile = g_ptr_array_index (srcs, i);
      g_autofree char *basename = g_file_get_basename (patchfile);
      g_autofree char *patch_path = g_file_get_path (patchfile);

      g_print ("Applying patch %s\n", basename);
      if (!patch (dest, self->use_git, self->use_git_am, patch_path, self->options, error, strip_components, NULL))
        return FALSE;
    }

  return TRUE;
}

static gboolean
builder_source_patch_bundle (BuilderSource  *source,
                             BuilderContext *context,
                             GError        **error)
{
  BuilderSourcePatch *self = BUILDER_SOURCE_PATCH (source);
  GFile *manifest_base_dir = builder_context_get_base_dir (context);
  g_autoptr(GPtrArray) srcs = NULL;
  int i;

  srcs = get_source_files (self, context, error);
  if (srcs == NULL)
    return FALSE;

  for (i = 0; i < srcs->len; i++)
    {
      GFile *src = g_ptr_array_index (srcs, i);
      g_autofree char *rel_path = NULL;
      g_autoptr(GFile) destination_file = NULL;
      g_autoptr(GFile) destination_dir = NULL;

      rel_path = g_file_get_relative_path (manifest_base_dir, src);
      if (rel_path == NULL)
        {
          g_warning ("Patch %s is outside manifest tree, not bundling", flatpak_file_get_path_cached (src));
          return TRUE;
        }

      destination_file = flatpak_build_file (builder_context_get_app_dir (context),
                                             "sources/manifest", rel_path, NULL);

      destination_dir = g_file_get_parent (destination_file);
      if (!flatpak_mkdir_p (destination_dir, NULL, error))
        return FALSE;

      if (!g_file_copy (src, destination_file,
                        G_FILE_COPY_OVERWRITE,
                        NULL,
                        NULL, NULL,
                        error))
        return FALSE;
    }

  return TRUE;
}

static void
builder_source_patch_checksum (BuilderSource  *source,
                               BuilderCache   *cache,
                               BuilderContext *context)
{
  BuilderSourcePatch *self = BUILDER_SOURCE_PATCH (source);
  g_autoptr(GPtrArray) srcs = NULL;
  int i;

  srcs = get_source_files (self, context, NULL);

  for (i = 0; srcs != NULL && i < srcs->len; i++)
    {
      GFile *src = g_ptr_array_index (srcs, i);
      g_autofree char *data = NULL;
      gsize len;

      if (g_file_load_contents (src, NULL, &data, &len, NULL, NULL))
        builder_cache_checksum_data (cache, (guchar *) data, len);
    }

  builder_cache_checksum_str (cache, self->path);
  builder_cache_checksum_compat_strv (cache, self->paths);
  builder_cache_checksum_uint32 (cache, self->strip_components);
  builder_cache_checksum_strv (cache, self->options);
}

static void
builder_source_patch_class_init (BuilderSourcePatchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_patch_finalize;
  object_class->get_property = builder_source_patch_get_property;
  object_class->set_property = builder_source_patch_set_property;

  source_class->show_deps = builder_source_patch_show_deps;
  source_class->download = builder_source_patch_download;
  source_class->extract = builder_source_patch_extract;
  source_class->bundle = builder_source_patch_bundle;
  source_class->checksum = builder_source_patch_checksum;

  g_object_class_install_property (object_class,
                                   PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PATHS,
                                   g_param_spec_boxed ("paths",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_STRIP_COMPONENTS,
                                   g_param_spec_uint ("strip-components",
                                                      "",
                                                      "",
                                                      0, G_MAXUINT,
                                                      1,
                                                      G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_USE_GIT,
                                   g_param_spec_boolean ("use-git",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_OPTIONS,
                                   g_param_spec_boxed ("options",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_USE_GIT_AM,
                                   g_param_spec_boolean ("use-git-am",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
}

static void
builder_source_patch_init (BuilderSourcePatch *self)
{
  self->strip_components = 1;
}
