/* builder-options.c
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
#include <string.h>
#include <stdlib.h>
#include <sys/statfs.h>

#include "builder-options.h"
#include "builder-context.h"
#include "builder-utils.h"

struct BuilderOptions
{
  GObject     parent;

  gboolean    strip;
  gboolean    no_debuginfo;
  gboolean    no_debuginfo_compression;
  char       *cflags;
  gboolean    cflags_override;
  char       *cppflags;
  gboolean    cppflags_override;
  char       *cxxflags;
  gboolean    cxxflags_override;
  char       *ldflags;
  gboolean    ldflags_override;
  char       *append_path;
  char       *prepend_path;
  char       *append_ld_library_path;
  char       *prepend_ld_library_path;
  char       *append_pkg_config_path;
  char       *prepend_pkg_config_path;
  char       *prefix;
  char       *libdir;
  char      **env;
  char      **build_args;
  char      **test_args;
  char      **config_opts;
  char      **make_args;
  char      **make_install_args;
  GHashTable *arch;
};

typedef struct
{
  GObjectClass parent_class;
} BuilderOptionsClass;

static void serializable_iface_init (JsonSerializableIface *serializable_iface);

G_DEFINE_TYPE_WITH_CODE (BuilderOptions, builder_options, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (JSON_TYPE_SERIALIZABLE, serializable_iface_init));

enum {
  PROP_0,
  PROP_CFLAGS,
  PROP_CFLAGS_OVERRIDE,
  PROP_CPPFLAGS,
  PROP_CPPFLAGS_OVERRIDE,
  PROP_CXXFLAGS,
  PROP_CXXFLAGS_OVERRIDE,
  PROP_LDFLAGS,
  PROP_LDFLAGS_OVERRIDE,
  PROP_PREFIX,
  PROP_LIBDIR,
  PROP_ENV,
  PROP_STRIP,
  PROP_NO_DEBUGINFO,
  PROP_NO_DEBUGINFO_COMPRESSION,
  PROP_ARCH,
  PROP_BUILD_ARGS,
  PROP_TEST_ARGS,
  PROP_CONFIG_OPTS,
  PROP_MAKE_ARGS,
  PROP_MAKE_INSTALL_ARGS,
  PROP_APPEND_PATH,
  PROP_PREPEND_PATH,
  PROP_APPEND_LD_LIBRARY_PATH,
  PROP_PREPEND_LD_LIBRARY_PATH,
  PROP_APPEND_PKG_CONFIG_PATH,
  PROP_PREPEND_PKG_CONFIG_PATH,
  LAST_PROP
};


static void
builder_options_finalize (GObject *object)
{
  BuilderOptions *self = (BuilderOptions *) object;

  g_free (self->cflags);
  g_free (self->cxxflags);
  g_free (self->cppflags);
  g_free (self->ldflags);
  g_free (self->append_path);
  g_free (self->prepend_path);
  g_free (self->append_ld_library_path);
  g_free (self->prepend_ld_library_path);
  g_free (self->append_pkg_config_path);
  g_free (self->prepend_pkg_config_path);
  g_free (self->prefix);
  g_free (self->libdir);
  g_strfreev (self->env);
  g_strfreev (self->build_args);
  g_strfreev (self->test_args);
  g_strfreev (self->config_opts);
  g_strfreev (self->make_args);
  g_strfreev (self->make_install_args);
  g_hash_table_destroy (self->arch);

  G_OBJECT_CLASS (builder_options_parent_class)->finalize (object);
}

static void
builder_options_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  BuilderOptions *self = BUILDER_OPTIONS (object);

  switch (prop_id)
    {
    case PROP_CFLAGS:
      g_value_set_string (value, self->cflags);
      break;

    case PROP_CFLAGS_OVERRIDE:
      g_value_set_boolean (value, self->cflags_override);
      break;

    case PROP_CPPFLAGS:
      g_value_set_string (value, self->cppflags);
      break;

    case PROP_CPPFLAGS_OVERRIDE:
      g_value_set_boolean (value, self->cppflags_override);
      break;

    case PROP_CXXFLAGS:
      g_value_set_string (value, self->cxxflags);
      break;

    case PROP_CXXFLAGS_OVERRIDE:
      g_value_set_boolean (value, self->cxxflags_override);
      break;

    case PROP_LDFLAGS:
      g_value_set_string (value, self->ldflags);
      break;

    case PROP_LDFLAGS_OVERRIDE:
      g_value_set_boolean (value, self->ldflags_override);
      break;

    case PROP_APPEND_PATH:
      g_value_set_string (value, self->append_path);
      break;

    case PROP_PREPEND_PATH:
      g_value_set_string (value, self->prepend_path);
      break;

    case PROP_APPEND_LD_LIBRARY_PATH:
      g_value_set_string (value, self->append_ld_library_path);
      break;

    case PROP_PREPEND_LD_LIBRARY_PATH:
      g_value_set_string (value, self->prepend_ld_library_path);
      break;

    case PROP_APPEND_PKG_CONFIG_PATH:
      g_value_set_string (value, self->append_pkg_config_path);
      break;

    case PROP_PREPEND_PKG_CONFIG_PATH:
      g_value_set_string (value, self->prepend_pkg_config_path);
      break;

    case PROP_PREFIX:
      g_value_set_string (value, self->prefix);
      break;

    case PROP_LIBDIR:
      g_value_set_string (value, self->libdir);
      break;

    case PROP_ENV:
      g_value_set_boxed (value, self->env);
      break;

    case PROP_ARCH:
      g_value_set_boxed (value, self->arch);
      break;

    case PROP_BUILD_ARGS:
      g_value_set_boxed (value, self->build_args);
      break;

    case PROP_TEST_ARGS:
      g_value_set_boxed (value, self->test_args);
      break;

    case PROP_CONFIG_OPTS:
      g_value_set_boxed (value, self->config_opts);
      break;

    case PROP_MAKE_ARGS:
      g_value_set_boxed (value, self->make_args);
      break;

    case PROP_MAKE_INSTALL_ARGS:
      g_value_set_boxed (value, self->make_install_args);
      break;

    case PROP_STRIP:
      g_value_set_boolean (value, self->strip);
      break;

    case PROP_NO_DEBUGINFO:
      g_value_set_boolean (value, self->no_debuginfo);
      break;

    case PROP_NO_DEBUGINFO_COMPRESSION:
      g_value_set_boolean (value, self->no_debuginfo_compression);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_options_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  BuilderOptions *self = BUILDER_OPTIONS (object);
  gchar **tmp;

  switch (prop_id)
    {
    case PROP_CFLAGS:
      g_clear_pointer (&self->cflags, g_free);
      self->cflags = g_value_dup_string (value);
      break;

    case PROP_CFLAGS_OVERRIDE:
      self->cflags_override = g_value_get_boolean (value);
      break;

    case PROP_CXXFLAGS:
      g_clear_pointer (&self->cxxflags, g_free);
      self->cxxflags = g_value_dup_string (value);
      break;

    case PROP_CXXFLAGS_OVERRIDE:
      self->cxxflags_override = g_value_get_boolean (value);
      break;

    case PROP_CPPFLAGS:
      g_clear_pointer (&self->cppflags, g_free);
      self->cppflags = g_value_dup_string (value);
      break;

    case PROP_CPPFLAGS_OVERRIDE:
      self->cppflags_override = g_value_get_boolean (value);
      break;

    case PROP_LDFLAGS:
      g_clear_pointer (&self->ldflags, g_free);
      self->ldflags = g_value_dup_string (value);
      break;

    case PROP_LDFLAGS_OVERRIDE:
      self->ldflags_override = g_value_get_boolean (value);
      break;

    case PROP_APPEND_PATH:
      g_clear_pointer (&self->append_path, g_free);
      self->append_path = g_value_dup_string (value);
      break;

    case PROP_PREPEND_PATH:
      g_clear_pointer (&self->prepend_path, g_free);
      self->prepend_path = g_value_dup_string (value);
      break;

    case PROP_APPEND_LD_LIBRARY_PATH:
      g_clear_pointer (&self->append_ld_library_path, g_free);
      self->append_ld_library_path = g_value_dup_string (value);
      break;

    case PROP_PREPEND_LD_LIBRARY_PATH:
      g_clear_pointer (&self->prepend_ld_library_path, g_free);
      self->prepend_ld_library_path = g_value_dup_string (value);
      break;

    case PROP_APPEND_PKG_CONFIG_PATH:
      g_clear_pointer (&self->append_pkg_config_path, g_free);
      self->append_pkg_config_path = g_value_dup_string (value);
      break;

    case PROP_PREPEND_PKG_CONFIG_PATH:
      g_clear_pointer (&self->prepend_pkg_config_path, g_free);
      self->prepend_pkg_config_path = g_value_dup_string (value);
      break;

    case PROP_PREFIX:
      g_clear_pointer (&self->prefix, g_free);
      self->prefix = g_value_dup_string (value);
      break;

    case PROP_LIBDIR:
      g_clear_pointer (&self->libdir, g_free);
      self->libdir = g_value_dup_string (value);
      break;

    case PROP_ENV:
      tmp = self->env;
      self->env = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_ARCH:
      g_hash_table_destroy (self->arch);
      /* NOTE: This takes ownership of the hash table! */
      self->arch = g_value_dup_boxed (value);
      break;

    case PROP_BUILD_ARGS:
      tmp = self->build_args;
      self->build_args = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_TEST_ARGS:
      tmp = self->test_args;
      self->test_args = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_CONFIG_OPTS:
      tmp = self->config_opts;
      self->config_opts = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_MAKE_ARGS:
      tmp = self->make_args;
      self->make_args = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_MAKE_INSTALL_ARGS:
      tmp = self->make_install_args;
      self->make_install_args = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_STRIP:
      self->strip = g_value_get_boolean (value);
      break;

    case PROP_NO_DEBUGINFO:
      self->no_debuginfo = g_value_get_boolean (value);
      break;

    case PROP_NO_DEBUGINFO_COMPRESSION:
      self->no_debuginfo_compression = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_options_class_init (BuilderOptionsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = builder_options_finalize;
  object_class->get_property = builder_options_get_property;
  object_class->set_property = builder_options_set_property;

  g_object_class_install_property (object_class,
                                   PROP_CFLAGS,
                                   g_param_spec_string ("cflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CFLAGS_OVERRIDE,
                                   g_param_spec_boolean ("cflags-override",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CXXFLAGS,
                                   g_param_spec_string ("cxxflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CXXFLAGS_OVERRIDE,
                                   g_param_spec_boolean ("cxxflags-override",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CPPFLAGS,
                                   g_param_spec_string ("cppflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CPPFLAGS_OVERRIDE,
                                   g_param_spec_boolean ("cppflags-override",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_LDFLAGS,
                                   g_param_spec_string ("ldflags",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_LDFLAGS_OVERRIDE,
                                   g_param_spec_boolean ("ldflags-override",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_APPEND_PATH,
                                   g_param_spec_string ("append-path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PREPEND_PATH,
                                   g_param_spec_string ("prepend-path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_APPEND_LD_LIBRARY_PATH,
                                   g_param_spec_string ("append-ld-library-path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PREPEND_LD_LIBRARY_PATH,
                                   g_param_spec_string ("prepend-ld-library-path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_APPEND_PKG_CONFIG_PATH,
                                   g_param_spec_string ("append-pkg-config-path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PREPEND_PKG_CONFIG_PATH,
                                   g_param_spec_string ("prepend-pkg-config-path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PREFIX,
                                   g_param_spec_string ("prefix",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_LIBDIR,
                                   g_param_spec_string ("libdir",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_ENV,
                                   g_param_spec_boxed ("env",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_ARCH,
                                   g_param_spec_boxed ("arch",
                                                       "",
                                                       "",
                                                       G_TYPE_HASH_TABLE,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BUILD_ARGS,
                                   g_param_spec_boxed ("build-args",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_TEST_ARGS,
                                   g_param_spec_boxed ("test-args",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CONFIG_OPTS,
                                   g_param_spec_boxed ("config-opts",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_MAKE_ARGS,
                                   g_param_spec_boxed ("make-args",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_MAKE_INSTALL_ARGS,
                                   g_param_spec_boxed ("make-install-args",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_STRIP,
                                   g_param_spec_boolean ("strip",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_NO_DEBUGINFO,
                                   g_param_spec_boolean ("no-debuginfo",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_NO_DEBUGINFO_COMPRESSION,
                                   g_param_spec_boolean ("no-debuginfo-compression",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));

}

static void
builder_options_init (BuilderOptions *self)
{
  self->arch = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static JsonNode *
builder_options_serialize_property (JsonSerializable *serializable,
                                    const gchar      *property_name,
                                    const GValue     *value,
                                    GParamSpec       *pspec)
{
  if (strcmp (property_name, "arch") == 0)
    {
      BuilderOptions *self = BUILDER_OPTIONS (serializable);
      JsonNode *retval = NULL;

      if (self->arch && g_hash_table_size (self->arch) > 0)
        {
          JsonObject *object;
          GHashTableIter iter;
          gpointer key, value;

          object = json_object_new ();

          g_hash_table_iter_init (&iter, self->arch);
          while (g_hash_table_iter_next (&iter, &key, &value))
            {
              JsonNode *child = json_gobject_serialize (value);
              json_object_set_member (object, (char *) key, child);
            }

          retval = json_node_init_object (json_node_alloc (), object);
          json_object_unref (object);
        }

      return retval;
    }
  else if (strcmp (property_name, "env") == 0)
    {
      BuilderOptions *self = BUILDER_OPTIONS (serializable);
      JsonNode *retval = NULL;

      if (self->env && g_strv_length (self->env) > 0)
        {
          JsonObject *object;
          int i;

          object = json_object_new ();

          for (i = 0; self->env[i] != NULL; i++)
            {
              JsonNode *str = json_node_new (JSON_NODE_VALUE);
              const char *equal;
              g_autofree char *member = NULL;

              equal = strchr (self->env[i], '=');
              if (equal)
                {
                  json_node_set_string (str, equal + 1);
                  member = g_strndup (self->env[i], equal - self->env[i]);
                }
              else
                {
                  json_node_set_string (str, "");
                  member = g_strdup (self->env[i]);
                }

              json_object_set_member (object, member, str);
            }

          retval = json_node_init_object (json_node_alloc (), object);
          json_object_unref (object);
        }

      return retval;
    }
  else
    {
      return json_serializable_default_serialize_property (serializable,
                                                           property_name,
                                                           value,
                                                           pspec);
    }
}

static gboolean
builder_options_deserialize_property (JsonSerializable *serializable,
                                      const gchar      *property_name,
                                      GValue           *value,
                                      GParamSpec       *pspec,
                                      JsonNode         *property_node)
{
  if (strcmp (property_name, "arch") == 0)
    {
      if (JSON_NODE_TYPE (property_node) == JSON_NODE_NULL)
        {
          g_value_set_boxed (value, NULL);
          return TRUE;
        }
      else if (JSON_NODE_TYPE (property_node) == JSON_NODE_OBJECT)
        {
          JsonObject *object = json_node_get_object (property_node);
          g_autoptr(GHashTable) hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
          g_autoptr(GList) members = NULL;
          GList *l;

          members = json_object_get_members (object);
          for (l = members; l != NULL; l = l->next)
            {
              const char *member_name = l->data;
              JsonNode *val;
              GObject *option;

              val = json_object_get_member (object, member_name);
              option = json_gobject_deserialize (BUILDER_TYPE_OPTIONS, val);
              if (option == NULL)
                return FALSE;

              g_hash_table_insert (hash, g_strdup (member_name), option);
            }

          g_value_set_boxed (value, hash);
          return TRUE;
        }

      return FALSE;
    }
  else if (strcmp (property_name, "env") == 0)
    {
      if (JSON_NODE_TYPE (property_node) == JSON_NODE_NULL)
        {
          g_value_set_boxed (value, NULL);
          return TRUE;
        }
      else if (JSON_NODE_TYPE (property_node) == JSON_NODE_OBJECT)
        {
          JsonObject *object = json_node_get_object (property_node);
          g_autoptr(GPtrArray) env = g_ptr_array_new_with_free_func (g_free);
          g_autoptr(GList) members = NULL;
          GList *l;

          members = json_object_get_members (object);
          for (l = members; l != NULL; l = l->next)
            {
              const char *member_name = l->data;
              JsonNode *val;
              const char *val_str;

              val = json_object_get_member (object, member_name);
              val_str = json_node_get_string (val);
              if (val_str == NULL)
                return FALSE;

              g_ptr_array_add (env, g_strdup_printf ("%s=%s", member_name, val_str));
            }

          g_ptr_array_add (env, NULL);
          g_value_take_boxed (value, g_ptr_array_free (g_steal_pointer (&env), FALSE));
          return TRUE;
        }

      return FALSE;
    }
  else
    {
      return json_serializable_default_deserialize_property (serializable,
                                                             property_name,
                                                             value,
                                                             pspec, property_node);
    }
}

static void
serializable_iface_init (JsonSerializableIface *serializable_iface)
{
  serializable_iface->serialize_property = builder_options_serialize_property;
  serializable_iface->deserialize_property = builder_options_deserialize_property;
  serializable_iface->find_property = builder_serializable_find_property_with_error;
}

static GList *
get_arched_options (BuilderOptions *self, BuilderContext *context)
{
  GList *options = NULL;
  const char *arch = builder_context_get_arch (context);
  BuilderOptions *arch_options;

  options = g_list_prepend (options, self);

  arch_options = g_hash_table_lookup (self->arch, arch);
  if (arch_options)
    options = g_list_prepend (options, arch_options);

  return options;
}

static GList *
get_all_options (BuilderOptions *self, BuilderContext *context)
{
  GList *options = NULL;
  BuilderOptions *global_options = builder_context_get_options (context);

  if (self)
    options = get_arched_options (self, context);

  if (global_options && global_options != self)
    options = g_list_concat (options,  get_arched_options (global_options, context));

  return options;
}

static const char *
builder_options_get_flags (BuilderOptions *self,
                           BuilderContext *context,
                           size_t          field_offset,
                           size_t          override_field_offset,
                           const char     *sdk_flags)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;
  GString *flags = NULL;

  if (sdk_flags && sdk_flags[0])
    {
      flags = g_string_new (sdk_flags);
    }

  /* Last command flag wins, so reverse order */
  options = g_list_reverse (options);

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      const char *flag = G_STRUCT_MEMBER (const char *, o, field_offset);
      gboolean override = G_STRUCT_MEMBER (gboolean, o, override_field_offset);

      if (override && flags)
        g_string_truncate (flags, 0);

      if (flag)
        {
          if (flags == NULL)
            flags = g_string_new ("");

          if (flags->len > 0)
            g_string_append_c (flags, ' ');

          g_string_append (flags, flag);
        }
    }

  if (flags)
    return g_string_free (flags, FALSE);

  return NULL;
}

static const char *
get_sdk_flags (BuilderOptions *self, BuilderContext *context, const char *(*method)(BuilderSdkConfig *self))
{
  BuilderSdkConfig *sdk_config = builder_context_get_sdk_config (context);
  if (sdk_config)
    return (*method) (sdk_config);
  return NULL;
}

const char *
builder_options_get_cflags (BuilderOptions *self, BuilderContext *context)
{
  return builder_options_get_flags (self, context, G_STRUCT_OFFSET (BuilderOptions, cflags),
                                    G_STRUCT_OFFSET (BuilderOptions, cflags_override),
                                    get_sdk_flags (self, context, builder_sdk_config_get_cflags));
}

const char *
builder_options_get_cxxflags (BuilderOptions *self, BuilderContext *context)
{
  return builder_options_get_flags (self, context, G_STRUCT_OFFSET (BuilderOptions, cxxflags),
                                    G_STRUCT_OFFSET (BuilderOptions, cxxflags_override),
                                    get_sdk_flags (self, context, builder_sdk_config_get_cxxflags));
}

const char *
builder_options_get_cppflags (BuilderOptions *self, BuilderContext *context)
{
  return builder_options_get_flags (self, context, G_STRUCT_OFFSET (BuilderOptions, cppflags),
                                    G_STRUCT_OFFSET (BuilderOptions, cppflags_override),
                                    get_sdk_flags (self, context, builder_sdk_config_get_cppflags));
}

const char *
builder_options_get_ldflags (BuilderOptions *self, BuilderContext *context)
{
  return builder_options_get_flags (self, context, G_STRUCT_OFFSET (BuilderOptions, ldflags),
                                    G_STRUCT_OFFSET (BuilderOptions, ldflags_override),
                                    get_sdk_flags (self, context, builder_sdk_config_get_ldflags));
}

static char *
builder_options_get_appended_path (BuilderOptions *self, BuilderContext *context, const char *initial_value, size_t append_field_offset, size_t prepend_field_offset)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;
  GString *path_list = NULL;

  if (initial_value)
    path_list = g_string_new (initial_value);

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      const char *append = G_STRUCT_MEMBER (const char *, o, append_field_offset);
      const char *prepend = G_STRUCT_MEMBER (const char *, o, prepend_field_offset);

      if (append)
        {
          if (path_list == NULL)
            path_list = g_string_new ("");

          if (path_list->len > 0)
            g_string_append_c (path_list, ':');

          g_string_append (path_list, append);
        }

      if (prepend)
        {
          if (path_list == NULL)
            path_list = g_string_new ("");

          if (path_list->len > 0)
            g_string_prepend_c (path_list, ':');

          g_string_prepend (path_list, prepend);
        }
    }

  if (path_list)
    return g_string_free (path_list, FALSE);

  return NULL;
}

static char **
builder_options_update_ld_path (BuilderOptions *self, BuilderContext *context, char **envp)
{
  g_autofree char *path = NULL;
  const char *old = NULL;

  old = g_environ_getenv (envp, "LD_LIBRARY_PATH");
  if (old == NULL)
    old = "/app/lib";

  path = builder_options_get_appended_path (self, context, old,
                                            G_STRUCT_OFFSET (BuilderOptions, append_ld_library_path),
                                            G_STRUCT_OFFSET (BuilderOptions, prepend_ld_library_path));
  if (path)
    envp = g_environ_setenv (envp, "LD_LIBRARY_PATH", path, TRUE);

  return envp;
}

static char **
builder_options_update_pkg_config_path (BuilderOptions *self, BuilderContext *context, char **envp)
{
  g_autofree char *path = NULL;
  const char *old = NULL;

  old = g_environ_getenv (envp, "PKG_CONFIG_PATH");
  if (old == NULL)
    old = "/app/lib/pkgconfig:/app/share/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig";

  path = builder_options_get_appended_path (self, context, old,
                                            G_STRUCT_OFFSET (BuilderOptions, append_pkg_config_path),
                                            G_STRUCT_OFFSET (BuilderOptions, prepend_pkg_config_path));
  if (path)
    envp = g_environ_setenv (envp, "PKG_CONFIG_PATH", path, TRUE);

  return envp;
}

static char **
builder_options_update_path (BuilderOptions *self, BuilderContext *context, char **envp)
{
  g_autofree char *path = NULL;
  path = builder_options_get_appended_path (self, context,
                                            g_environ_getenv (envp, "PATH"),
                                            G_STRUCT_OFFSET (BuilderOptions, append_path),
                                            G_STRUCT_OFFSET (BuilderOptions, prepend_path));
  if (path)
    envp = g_environ_setenv (envp, "PATH", path, TRUE);
  return envp;
}

const char *
builder_options_get_prefix (BuilderOptions *self, BuilderContext *context)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      if (o->prefix)
        return o->prefix;
    }

  if (builder_context_get_build_runtime (context))
    return "/usr";

  return "/app";
}

const char *
builder_options_get_libdir (BuilderOptions *self, BuilderContext *context)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      if (o->libdir)
        return o->libdir;
    }

  if (builder_context_get_build_runtime (context))
    return get_sdk_flags (self, context, builder_sdk_config_get_libdir);

  return NULL;
}

gboolean
builder_options_get_strip (BuilderOptions *self, BuilderContext *context)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      if (o->strip)
        return TRUE;
    }

  return FALSE;
}

gboolean
builder_options_get_no_debuginfo (BuilderOptions *self, BuilderContext *context)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      if (o->no_debuginfo)
        return TRUE;
    }

  return FALSE;
}

gboolean
builder_options_get_no_debuginfo_compression (BuilderOptions *self, BuilderContext *context)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      if (o->no_debuginfo_compression)
        return TRUE;
    }

  return FALSE;
}

char **
builder_options_get_env (BuilderOptions *self, BuilderContext *context)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;
  int i;
  char **envp = NULL;
  const char *cflags, *cppflags, *cxxflags, *ldflags;

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;

      if (o->env)
        {
          for (i = 0; o->env[i] != NULL; i++)
            {
              const char *line = o->env[i];
              const char *eq = strchr (line, '=');
              const char *value = "";
              g_autofree char *key = NULL;

              if (eq)
                {
                  key = g_strndup (line, eq - line);
                  value = eq + 1;
                }
              else
                {
                  key = g_strdup (key);
                }

              envp = g_environ_setenv (envp, key, value, FALSE);
            }
        }
    }

  envp = builder_context_extend_env (context, envp);

  cflags = builder_options_get_cflags (self, context);
  if (cflags)
    envp = g_environ_setenv (envp, "CFLAGS", cflags, FALSE);

  cppflags = builder_options_get_cppflags (self, context);
  if (cppflags)
    envp = g_environ_setenv (envp, "CPPFLAGS", cppflags, FALSE);

  cxxflags = builder_options_get_cxxflags (self, context);
  if (cxxflags)
    envp = g_environ_setenv (envp, "CXXFLAGS", cxxflags, FALSE);

  ldflags = builder_options_get_ldflags (self, context);
  if (ldflags)
    envp = g_environ_setenv (envp, "LDFLAGS", ldflags, FALSE);

  envp = builder_options_update_path (self, context, envp);
  envp = builder_options_update_ld_path (self, context, envp);
  envp = builder_options_update_pkg_config_path (self, context, envp);

  return envp;
}

char **
builder_options_get_build_args (BuilderOptions *self,
                                BuilderContext *context,
                                GError **error)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;
  int i;
  g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func (g_free);

  /* Last argument wins, so reverse the list for per-module to win */
  options = g_list_reverse (options);

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;

      if (o->build_args)
        {
          for (i = 0; o->build_args[i] != NULL; i++)
            g_ptr_array_add (array, g_strdup (o->build_args[i]));
        }
    }

  if (builder_context_get_sandboxed (context))
    {
      if (array->len > 0)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't specify build-args in sandboxed build");
          return NULL;
        }
      /* If, for whatever reason, the app has network access in the
         metadata, explicitly neuter that if we're building
         sandboxed */
      g_ptr_array_add (array, g_strdup ("--unshare=network"));
    }

  g_ptr_array_add (array, NULL);

  return (char **) g_ptr_array_free (g_steal_pointer (&array), FALSE);
}

char **
builder_options_get_test_args (BuilderOptions *self,
                               BuilderContext *context,
                               GError **error)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;
  int i;
  g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func (g_free);

  /* Last argument wins, so reverse the list for per-module to win */
  options = g_list_reverse (options);

  /* Always run tests readonly */
  g_ptr_array_add (array, g_strdup ("--readonly"));

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;

      if (o->test_args)
        {
          for (i = 0; o->test_args[i] != NULL; i++)
            g_ptr_array_add (array, g_strdup (o->test_args[i]));
        }
    }

  if (array->len > 0 && builder_context_get_sandboxed (context))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't specify test-args in sandboxed build");
      return NULL;
    }

  g_ptr_array_add (array, NULL);

  return (char **) g_ptr_array_free (g_steal_pointer (&array), FALSE);
}

static char **
builder_options_get_strv (BuilderOptions *self,
                          BuilderContext *context,
                          char          **base,
                          size_t          field_offset)
{
  g_autoptr(GList) options = get_all_options (self, context);
  GList *l;
  int i;
  g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func (g_free);

  /* Last argument wins, so reverse the list for per-module to win */
  options = g_list_reverse (options);

  /* Start by adding the base values */
  if (base)
    {
      for (i = 0; base[i] != NULL; i++)
        g_ptr_array_add (array, g_strdup (base[i]));
    }

  for (l = options; l != NULL; l = l->next)
    {
      BuilderOptions *o = l->data;
      char **strv = G_STRUCT_MEMBER (char **, o, field_offset);

      if (strv)
        {
          for (i = 0; strv[i] != NULL; i++)
            g_ptr_array_add (array, g_strdup (strv[i]));
        }
    }

  g_ptr_array_add (array, NULL);

  return (char **) g_ptr_array_free (g_steal_pointer (&array), FALSE);
}

char **
builder_options_get_config_opts (BuilderOptions *self,
                                 BuilderContext *context,
                                 char          **base_opts)
{
  return builder_options_get_strv (self, context, base_opts, G_STRUCT_OFFSET (BuilderOptions, config_opts));
}

char **
builder_options_get_make_args (BuilderOptions *self,
                               BuilderContext *context,
                               char          **base_args)
{
  return builder_options_get_strv (self, context, base_args, G_STRUCT_OFFSET (BuilderOptions, make_args));
}

char **
builder_options_get_make_install_args (BuilderOptions *self,
                                       BuilderContext *context,
                                       char          **base_args)
{
  return builder_options_get_strv (self, context, base_args, G_STRUCT_OFFSET (BuilderOptions, make_install_args));
}

void
builder_options_checksum (BuilderOptions *self,
                          BuilderCache   *cache,
                          BuilderContext *context)
{
  BuilderOptions *arch_options;

  builder_cache_checksum_str (cache, BUILDER_OPTION_CHECKSUM_VERSION);
  builder_cache_checksum_str (cache, self->cflags);
  builder_cache_checksum_compat_boolean (cache, self->cflags_override);
  builder_cache_checksum_str (cache, self->cxxflags);
  builder_cache_checksum_compat_boolean (cache, self->cxxflags_override);
  builder_cache_checksum_str (cache, self->cppflags);
  builder_cache_checksum_compat_boolean (cache, self->cppflags_override);
  builder_cache_checksum_str (cache, self->ldflags);
  builder_cache_checksum_compat_boolean (cache, self->ldflags_override);
  builder_cache_checksum_str (cache, self->prefix);
  builder_cache_checksum_compat_str (cache, self->libdir);
  builder_cache_checksum_strv (cache, self->env);
  builder_cache_checksum_strv (cache, self->build_args);
  builder_cache_checksum_compat_strv (cache, self->test_args);
  builder_cache_checksum_strv (cache, self->config_opts);
  builder_cache_checksum_strv (cache, self->make_args);
  builder_cache_checksum_strv (cache, self->make_install_args);
  builder_cache_checksum_boolean (cache, self->strip);
  builder_cache_checksum_boolean (cache, self->no_debuginfo);
  builder_cache_checksum_boolean (cache, self->no_debuginfo_compression);

  builder_cache_checksum_compat_str (cache, self->append_path);
  builder_cache_checksum_compat_str (cache, self->prepend_path);
  builder_cache_checksum_compat_str (cache, self->append_ld_library_path);
  builder_cache_checksum_compat_str (cache, self->prepend_ld_library_path);
  builder_cache_checksum_compat_str (cache, self->append_pkg_config_path);
  builder_cache_checksum_compat_str (cache, self->prepend_pkg_config_path);

  arch_options = g_hash_table_lookup (self->arch, builder_context_get_arch (context));
  if (arch_options)
    builder_options_checksum (arch_options, cache, context);
}
