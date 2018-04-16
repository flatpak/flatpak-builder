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

#include <json-glib/json-glib.h>

struct BuilderSdkConfig {
  GObject       parent;

  char         *libdir;
  char         *cppflags;
  char         *cflags;
  char         *cxxflags;
  char         *ldflags;
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

  G_OBJECT_CLASS (builder_sdk_config_parent_class)->finalize (object);
}

enum {
  PROP_0,
  PROP_LIBDIR,
  PROP_CPPFLAGS,
  PROP_CFLAGS,
  PROP_CXXFLAGS,
  PROP_LDFLAGS,
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

BuilderSdkConfig *
builder_sdk_config_from_file (GFile    *file,
                              GError  **error)
{
  g_autofree gchar     *config_contents = NULL;
  gsize                 config_size;

  if (!g_file_load_contents (file, NULL, &config_contents, &config_size, NULL, error))
    return NULL;

  return (BuilderSdkConfig*) json_gobject_from_data (BUILDER_TYPE_SDK_CONFIG,
                                                     config_contents,
                                                     config_size,
                                                     error);
}
