/*
 * Copyright © 2018 Codethink Limited
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
 *       Valentin David <valentin.david@codethink.co.uk>
 */

#include "builder-sdk-config.h"
#include "builder-utils.h"

#include <json-glib/json-glib.h>

struct BuilderSdkConfig {
  GObject       parent;

  char         *libdir;
  char         *cppflags;
  char         *cflags;
  char         *cxxflags;
  char         *ldflags;
  char         *rustflags;
};

typedef struct
{
  GObjectClass parent_class;
} BuilderSdkConfigClass;

G_DEFINE_TYPE (BuilderSdkConfig, builder_sdk_config, G_TYPE_OBJECT)

static void
builder_sdk_config_finalize (GObject *object)
{
  BuilderSdkConfig *self = (BuilderSdkConfig *) object;

  g_free (self->libdir);
  g_free (self->cppflags);
  g_free (self->cflags);
  g_free (self->cxxflags);
  g_free (self->ldflags);
  g_free (self->rustflags);

  G_OBJECT_CLASS (builder_sdk_config_parent_class)->finalize (object);
}

enum {
  PROP_0,
  PROP_LIBDIR,
  PROP_CPPFLAGS,
  PROP_CFLAGS,
  PROP_CXXFLAGS,
  PROP_LDFLAGS,
  PROP_RUSTFLAGS,
  LAST_PROP
};

static void
builder_sdk_config_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  BuilderSdkConfig *self = BUILDER_SDK_CONFIG (object);

  switch (prop_id)
    {
    case PROP_LIBDIR:
      g_value_set_string (value, self->libdir);
      break;

    case PROP_CPPFLAGS:
      g_value_set_string (value, self->cppflags);
      break;

    case PROP_CFLAGS:
      g_value_set_string (value, self->cflags);
      break;

    case PROP_CXXFLAGS:
      g_value_set_string (value, self->cxxflags);
      break;

    case PROP_LDFLAGS:
      g_value_set_string (value, self->ldflags);
      break;

    case PROP_RUSTFLAGS:
      g_value_set_string (value, self->rustflags);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_sdk_config_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  BuilderSdkConfig *self = BUILDER_SDK_CONFIG (object);

  switch (prop_id)
    {
    case PROP_LIBDIR:
      g_free (self->libdir);
      self->libdir = g_value_dup_string(value);
      break ;

    case PROP_CPPFLAGS:
      g_free (self->cppflags);
      self->cppflags = g_value_dup_string(value);
      break ;

    case PROP_CFLAGS:
      g_free (self->cflags);
      self->cflags = g_value_dup_string(value);
      break ;

    case PROP_CXXFLAGS:
      g_free (self->cxxflags);
      self->cxxflags = g_value_dup_string(value);
      break ;

    case PROP_LDFLAGS:
      g_free (self->ldflags);
      self->ldflags = g_value_dup_string(value);
      break ;

    case PROP_RUSTFLAGS:
      g_free (self->rustflags);
      self->rustflags = g_value_dup_string(value);
      break ;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_sdk_config_class_init (BuilderSdkConfigClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = builder_sdk_config_finalize;
  object_class->get_property = builder_sdk_config_get_property;
  object_class->set_property = builder_sdk_config_set_property;

  g_object_class_install_property (object_class,
                                   PROP_LIBDIR,
                                   g_param_spec_string ("libdir",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CPPFLAGS,
                                   g_param_spec_string ("cppflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CFLAGS,
                                   g_param_spec_string ("cflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CXXFLAGS,
                                   g_param_spec_string ("cxxflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_LDFLAGS,
                                   g_param_spec_string ("ldflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RUSTFLAGS,
                                   g_param_spec_string ("rustflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
}

static void
builder_sdk_config_init (BuilderSdkConfig *self)
{
}


const char *
builder_sdk_config_get_libdir (BuilderSdkConfig *self)
{
  return self->libdir;
}

const char *
builder_sdk_config_get_cppflags (BuilderSdkConfig *self)
{
  return self->cppflags;
}

const char *
builder_sdk_config_get_cflags (BuilderSdkConfig *self)
{
  return self->cflags;
}

const char *
builder_sdk_config_get_cxxflags (BuilderSdkConfig *self)
{
  return self->cxxflags;
}

const char *
builder_sdk_config_get_ldflags (BuilderSdkConfig *self)
{
  return self->ldflags;
}

const char *
builder_sdk_config_get_rustflags (BuilderSdkConfig *self)
{
  return self->rustflags;
}

BuilderSdkConfig *
builder_sdk_config_from_fd (int      fd,
                            GError **error)
{
  g_autoptr(GBytes) bytes = NULL;
  gsize size;
  const char *data;

  bytes = builder_read_fd (fd, FALSE, error);
  if (bytes == NULL)
    return NULL;

  data = g_bytes_get_data (bytes, &size);
  return (BuilderSdkConfig *) json_gobject_from_data (BUILDER_TYPE_SDK_CONFIG,
                                                      data,
                                                      size,
                                                      error);
}
