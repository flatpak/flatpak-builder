/* builder-source-extra_data.c
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
#include "builder-source-extra-data.h"

struct BuilderSourceExtraData
{
  BuilderSource parent;

  char         *filename;
  char         *url;
  char         *sha256;
  guint64       size;
  guint64       installed_size;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourceExtraDataClass;

G_DEFINE_TYPE (BuilderSourceExtraData, builder_source_extra_data, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_FILENAME,
  PROP_URL,
  PROP_SHA256,
  PROP_SIZE,
  PROP_INSTALLED_SIZE,
  LAST_PROP
};

static void
builder_source_extra_data_finalize (GObject *object)
{
  BuilderSourceExtraData *self = (BuilderSourceExtraData *) object;

  g_free (self->filename);
  g_free (self->url);
  g_free (self->sha256);

  G_OBJECT_CLASS (builder_source_extra_data_parent_class)->finalize (object);
}

static void
builder_source_extra_data_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  BuilderSourceExtraData *self = BUILDER_SOURCE_EXTRA_DATA (object);

  switch (prop_id)
    {
    case PROP_FILENAME:
      g_value_set_string (value, self->filename);
      break;

    case PROP_URL:
      g_value_set_string (value, self->url);
      break;

    case PROP_SHA256:
      g_value_set_string (value, self->sha256);
      break;

    case PROP_SIZE:
      g_value_set_uint64 (value, self->size);
      break;

    case PROP_INSTALLED_SIZE:
      g_value_set_uint64 (value, self->installed_size);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_extra_data_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  BuilderSourceExtraData *self = BUILDER_SOURCE_EXTRA_DATA (object);

  switch (prop_id)
    {
    case PROP_FILENAME:
      g_free (self->filename);
      self->filename = g_value_dup_string (value);
      break;

    case PROP_URL:
      g_free (self->url);
      self->url = g_value_dup_string (value);
      break;

    case PROP_SHA256:
      g_free (self->sha256);
      self->sha256 = g_value_dup_string (value);
      break;

    case PROP_SIZE:
      self->size = g_value_get_uint64 (value);
      break;

    case PROP_INSTALLED_SIZE:
      self->installed_size = g_value_get_uint64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
builder_source_extra_data_download (BuilderSource  *source,
                                    gboolean        update_vcs,
                                    BuilderContext *context,
                                    GError        **error)
{
  BuilderSourceExtraData *self = BUILDER_SOURCE_EXTRA_DATA (source);

  if (self->filename == NULL)
    return flatpak_fail (error, "No filename specified for extra data source");

  if (self->url == NULL)
    return flatpak_fail (error, "No url specified for extra data source");

  if (self->sha256 == NULL)
    return flatpak_fail (error, "No sha256 specified for extra data source");

  if (self->size == 0)
    return flatpak_fail (error, "No size specified for extra data source");

  return TRUE;
}


static gboolean
builder_source_extra_data_extract (BuilderSource  *source,
                                   GFile          *dest,
                                   GFile          *source_dir,
                                   BuilderOptions *build_options,
                                   BuilderContext *context,
                                   GError        **error)
{
  return TRUE;
}

static gboolean
builder_source_extra_data_bundle (BuilderSource  *source,
                                  BuilderContext *context,
                                  GError        **error)
{
  return TRUE;
}

static void
builder_source_extra_data_checksum (BuilderSource  *source,
                                    BuilderCache   *cache,
                                    BuilderContext *context)
{
  BuilderSourceExtraData *self = BUILDER_SOURCE_EXTRA_DATA (source);

  builder_cache_checksum_str (cache, self->filename);
  builder_cache_checksum_str (cache, self->url);
  builder_cache_checksum_str (cache, self->sha256);
  builder_cache_checksum_uint64 (cache, self->size);
  builder_cache_checksum_uint64 (cache, self->installed_size);
}

static void
builder_source_extra_data_finish (BuilderSource  *source,
                                  GPtrArray      *args,
                                  BuilderContext *context)
{
  BuilderSourceExtraData *self = BUILDER_SOURCE_EXTRA_DATA (source);
  char *arg;
  g_autofree char *installed_size = NULL;

  if (self->installed_size != 0)
    installed_size = g_strdup_printf ("%" G_GUINT64_FORMAT, self->installed_size);
  else
    installed_size = g_strdup ("");

  arg = g_strdup_printf ("--extra-data=%s:%s:%"G_GUINT64_FORMAT":%s:%s",
                         self->filename,
                         self->sha256,
                         self->size,
                         installed_size,
                         self->url);

  g_ptr_array_add (args, arg);
}

static void
builder_source_extra_data_class_init (BuilderSourceExtraDataClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_extra_data_finalize;
  object_class->get_property = builder_source_extra_data_get_property;
  object_class->set_property = builder_source_extra_data_set_property;

  source_class->download = builder_source_extra_data_download;
  source_class->extract = builder_source_extra_data_extract;
  source_class->bundle = builder_source_extra_data_bundle;
  source_class->checksum = builder_source_extra_data_checksum;
  source_class->finish = builder_source_extra_data_finish;

  g_object_class_install_property (object_class,
                                   PROP_URL,
                                   g_param_spec_string ("url",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_FILENAME,
                                   g_param_spec_string ("filename",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SHA256,
                                   g_param_spec_string ("sha256",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SIZE,
                                   g_param_spec_uint64 ("size",
                                                        "",
                                                        "",
                                                        0, G_MAXUINT64,
                                                        0,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_INSTALLED_SIZE,
                                   g_param_spec_uint64 ("installed-size",
                                                        "",
                                                        "",
                                                        0, G_MAXUINT64,
                                                        0,
                                                        G_PARAM_READWRITE));
}

static void
builder_source_extra_data_init (BuilderSourceExtraData *self)
{
}
