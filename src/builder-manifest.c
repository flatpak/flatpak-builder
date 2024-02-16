/* builder-manifest.c
 *
 * Copyright (C) 2015 Red Hat, Inc
 * Copyright © 2023 GNOME Foundation Inc.
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
 *       Hubert Figuière <hub@figuiere.net>
 */

#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statfs.h>
#include <glib/gi18n.h>

#include "builder-manifest.h"
#include "builder-utils.h"
#include "builder-flatpak-utils.h"
#include "builder-post-process.h"
#include "builder-extension.h"

#include <libxml/parser.h>

#include "libglnx.h"

#define LOCALES_SEPARATE_DIR "share/runtime/locale"

static GFile *demarshal_base_dir = NULL;

void
builder_manifest_set_demarshal_base_dir (GFile *dir)
{
  g_set_object (&demarshal_base_dir, dir);
}

GFile *
builder_manifest_get_demarshal_base_dir (void)
{
  return g_object_ref (demarshal_base_dir);
}

struct BuilderManifest
{
  GObject         parent;

  char           *id;
  char           *id_platform;
  char           *branch;
  char           *default_branch;
  char           *collection_id;
  gint32          token_type;
  char           *extension_tag;
  char           *type;
  char           *runtime;
  char           *runtime_commit;
  char           *runtime_version;
  char           *sdk;
  char           *sdk_commit;
  char           *var;
  char           *base;
  char           *base_commit;
  char           *base_version;
  char          **base_extensions;
  char           *metadata;
  char           *metadata_platform;
  gboolean        separate_locales;
  char          **cleanup;
  char          **cleanup_commands;
  char          **cleanup_platform;
  char          **cleanup_platform_commands;
  char          **prepare_platform_commands;
  char          **finish_args;
  char          **inherit_extensions;
  char          **inherit_sdk_extensions;
  char          **tags;
  char           *rename_desktop_file;
  char           *rename_appdata_file;
  char           *rename_mime_file;
  char           *appdata_license;
  char           *rename_icon;
  char          **rename_mime_icons;
  gboolean        copy_icon;
  char           *desktop_file_name_prefix;
  char           *desktop_file_name_suffix;
  gboolean        build_runtime;
  gboolean        build_extension;
  gboolean        writable_sdk;
  gboolean        appstream_compose;
  char          **sdk_extensions;
  char          **platform_extensions;
  char           *command;
  BuilderOptions *build_options;
  GList          *modules;
  GList          *expanded_modules;
  GList          *add_extensions;
  GList          *add_build_extensions;
  gint64          source_date_epoch;
};

typedef struct
{
  GObjectClass parent_class;
} BuilderManifestClass;

static void serializable_iface_init (JsonSerializableIface *serializable_iface);

G_DEFINE_TYPE_WITH_CODE (BuilderManifest, builder_manifest, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (JSON_TYPE_SERIALIZABLE, serializable_iface_init));

enum {
  PROP_0,
  PROP_APP_ID, /* Backwards compat with early version, use id */
  PROP_ID,
  PROP_ID_PLATFORM,
  PROP_BRANCH,
  PROP_DEFAULT_BRANCH,
  PROP_RUNTIME,
  PROP_RUNTIME_VERSION,
  PROP_RUNTIME_COMMIT,
  PROP_SDK,
  PROP_SDK_COMMIT,
  PROP_BASE,
  PROP_BASE_VERSION,
  PROP_BASE_COMMIT,
  PROP_BASE_EXTENSIONS,
  PROP_VAR,
  PROP_METADATA,
  PROP_METADATA_PLATFORM,
  PROP_BUILD_OPTIONS,
  PROP_COMMAND,
  PROP_MODULES,
  PROP_CLEANUP,
  PROP_CLEANUP_COMMANDS,
  PROP_CLEANUP_PLATFORM_COMMANDS,
  PROP_CLEANUP_PLATFORM,
  PROP_PREPARE_PLATFORM_COMMANDS,
  PROP_BUILD_RUNTIME,
  PROP_BUILD_EXTENSION,
  PROP_SEPARATE_LOCALES,
  PROP_WRITABLE_SDK,
  PROP_APPSTREAM_COMPOSE,
  PROP_SDK_EXTENSIONS,
  PROP_PLATFORM_EXTENSIONS,
  PROP_FINISH_ARGS,
  PROP_INHERIT_EXTENSIONS,
  PROP_INHERIT_SDK_EXTENSIONS,
  PROP_TAGS,
  PROP_RENAME_DESKTOP_FILE,
  PROP_RENAME_APPDATA_FILE,
  PROP_RENAME_MIME_FILE,
  PROP_APPDATA_LICENSE,
  PROP_RENAME_ICON,
  PROP_RENAME_MIME_ICONS,
  PROP_COPY_ICON,
  PROP_DESKTOP_FILE_NAME_PREFIX,
  PROP_DESKTOP_FILE_NAME_SUFFIX,
  PROP_COLLECTION_ID,
  PROP_ADD_EXTENSIONS,
  PROP_ADD_BUILD_EXTENSIONS,
  PROP_EXTENSION_TAG,
  PROP_TOKEN_TYPE,
  PROP_SOURCE_DATE_EPOCH,
  LAST_PROP
};

static void
builder_manifest_finalize (GObject *object)
{
  BuilderManifest *self = (BuilderManifest *) object;

  g_free (self->id);
  g_free (self->branch);
  g_free (self->default_branch);
  g_free (self->collection_id);
  g_free (self->extension_tag);
  g_free (self->runtime);
  g_free (self->runtime_commit);
  g_free (self->runtime_version);
  g_free (self->sdk);
  g_free (self->sdk_commit);
  g_free (self->base);
  g_free (self->base_commit);
  g_free (self->base_version);
  g_free (self->var);
  g_free (self->metadata);
  g_free (self->metadata_platform);
  g_free (self->command);
  g_clear_object (&self->build_options);
  g_list_free_full (self->modules, g_object_unref);
  g_list_free_full (self->add_extensions, g_object_unref);
  g_list_free_full (self->add_build_extensions, g_object_unref);
  g_list_free (self->expanded_modules);
  g_strfreev (self->cleanup);
  g_strfreev (self->cleanup_commands);
  g_strfreev (self->cleanup_platform);
  g_strfreev (self->cleanup_platform_commands);
  g_strfreev (self->prepare_platform_commands);
  g_strfreev (self->finish_args);
  g_strfreev (self->inherit_extensions);
  g_strfreev (self->inherit_sdk_extensions);
  g_strfreev (self->tags);
  g_free (self->rename_desktop_file);
  g_free (self->rename_appdata_file);
  g_free (self->rename_mime_file);
  g_free (self->appdata_license);
  g_free (self->rename_icon);
  g_free (self->rename_mime_icons);
  g_free (self->desktop_file_name_prefix);
  g_free (self->desktop_file_name_suffix);

  G_OBJECT_CLASS (builder_manifest_parent_class)->finalize (object);
}

static gboolean
expand_modules (BuilderContext *context, GList *modules,
                GList **expanded, GHashTable *names, GError **error)
{
  GList *l;

  for (l = modules; l; l = l->next)
    {
      BuilderModule *m = l->data;
      GList *submodules = NULL;
      g_autofree char *new_name = NULL;
      int new_name_counter;
      const char *orig_name;
      const char *name;

      if (!builder_module_is_enabled (m, context))
        continue;

      if (!expand_modules (context, builder_module_get_modules (m), &submodules, names, error))
        return FALSE;

      *expanded = g_list_concat (*expanded, submodules);

      name = builder_module_get_name (m);
      if (name == NULL)
        {
          /* FIXME: We'd like to report *something* for the user
                    to locate the errornous module definition.
           */
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Module has no 'name' attribute set");
          return FALSE;
        }
      orig_name = name;

      /* Duplicated name happen sometimes, like e.g. when including snippets out of your control.
       * It is not a huge problem for building, but we need unique names for e.g the cache, so
       * uniquify on collision */
      new_name_counter = 2;
      while (g_hash_table_lookup (names, name) != NULL)
        {
          g_free (new_name);
          new_name = g_strdup_printf ("%s-%d", orig_name, new_name_counter++);
          name = new_name;
        }

      if (name != orig_name)
        builder_module_set_name (m, name);

      g_hash_table_insert (names, (char *)name, (char *)name);
      *expanded = g_list_append (*expanded, m);
    }

  return TRUE;
}

static void
builder_manifest_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BuilderManifest *self = BUILDER_MANIFEST (object);

  switch (prop_id)
    {
    case PROP_APP_ID:
      g_value_set_string (value, NULL);
      break;

    case PROP_ID:
      g_value_set_string (value, self->id);
      break;

    case PROP_ID_PLATFORM:
      g_value_set_string (value, self->id_platform);
      break;

    case PROP_BRANCH:
      g_value_set_string (value, self->branch);
      break;

    case PROP_DEFAULT_BRANCH:
      g_value_set_string (value, self->default_branch);
      break;

    case PROP_RUNTIME:
      g_value_set_string (value, self->runtime);
      break;

    case PROP_RUNTIME_COMMIT:
      g_value_set_string (value, self->runtime_commit);
      break;

    case PROP_RUNTIME_VERSION:
      g_value_set_string (value, self->runtime_version);
      break;

    case PROP_SDK:
      g_value_set_string (value, self->sdk);
      break;

    case PROP_SDK_COMMIT:
      g_value_set_string (value, self->sdk_commit);
      break;

    case PROP_BASE:
      g_value_set_string (value, self->base);
      break;

    case PROP_BASE_COMMIT:
      g_value_set_string (value, self->base_commit);
      break;

    case PROP_BASE_VERSION:
      g_value_set_string (value, self->base_version);
      break;

    case PROP_BASE_EXTENSIONS:
      g_value_set_boxed (value, self->base_extensions);
      break;

    case PROP_VAR:
      g_value_set_string (value, self->var);
      break;

    case PROP_METADATA:
      g_value_set_string (value, self->metadata);
      break;

    case PROP_METADATA_PLATFORM:
      g_value_set_string (value, self->metadata_platform);
      break;

    case PROP_COMMAND:
      g_value_set_string (value, self->command);
      break;

    case PROP_BUILD_OPTIONS:
      g_value_set_object (value, self->build_options);
      break;

    case PROP_MODULES:
      g_value_set_pointer (value, self->modules);
      break;

    case PROP_ADD_EXTENSIONS:
      g_value_set_pointer (value, self->add_extensions);
      break;

    case PROP_ADD_BUILD_EXTENSIONS:
      g_value_set_pointer (value, self->add_build_extensions);
      break;

    case PROP_CLEANUP:
      g_value_set_boxed (value, self->cleanup);
      break;

    case PROP_CLEANUP_COMMANDS:
      g_value_set_boxed (value, self->cleanup_commands);
      break;

    case PROP_CLEANUP_PLATFORM:
      g_value_set_boxed (value, self->cleanup_platform);
      break;

    case PROP_CLEANUP_PLATFORM_COMMANDS:
      g_value_set_boxed (value, self->cleanup_platform_commands);
      break;

    case PROP_PREPARE_PLATFORM_COMMANDS:
      g_value_set_boxed (value, self->prepare_platform_commands);
      break;

    case PROP_FINISH_ARGS:
      g_value_set_boxed (value, self->finish_args);
      break;

    case PROP_INHERIT_EXTENSIONS:
      g_value_set_boxed (value, self->inherit_extensions);
      break;

    case PROP_INHERIT_SDK_EXTENSIONS:
      g_value_set_boxed (value, self->inherit_sdk_extensions);
      break;

    case PROP_TAGS:
      g_value_set_boxed (value, self->tags);
      break;

    case PROP_BUILD_RUNTIME:
      g_value_set_boolean (value, self->build_runtime);
      break;

    case PROP_BUILD_EXTENSION:
      g_value_set_boolean (value, self->build_extension);
      break;

    case PROP_SEPARATE_LOCALES:
      g_value_set_boolean (value, self->separate_locales);
      break;

    case PROP_WRITABLE_SDK:
      g_value_set_boolean (value, self->writable_sdk);
      break;

    case PROP_APPSTREAM_COMPOSE:
      g_value_set_boolean (value, self->appstream_compose);
      break;

    case PROP_SDK_EXTENSIONS:
      g_value_set_boxed (value, self->sdk_extensions);
      break;

    case PROP_PLATFORM_EXTENSIONS:
      g_value_set_boxed (value, self->platform_extensions);
      break;

    case PROP_COPY_ICON:
      g_value_set_boolean (value, self->copy_icon);
      break;

    case PROP_RENAME_DESKTOP_FILE:
      g_value_set_string (value, self->rename_desktop_file);
      break;

    case PROP_RENAME_APPDATA_FILE:
      g_value_set_string (value, self->rename_appdata_file);
      break;

    case PROP_RENAME_MIME_FILE:
      g_value_set_string (value, self->rename_mime_file);
      break;

    case PROP_APPDATA_LICENSE:
      g_value_set_string (value, self->appdata_license);
      break;

    case PROP_RENAME_ICON:
      g_value_set_string (value, self->rename_icon);
      break;

    case PROP_RENAME_MIME_ICONS:
      g_value_set_boxed (value, self->rename_mime_icons);
      break;

    case PROP_DESKTOP_FILE_NAME_PREFIX:
      g_value_set_string (value, self->desktop_file_name_prefix);
      break;

    case PROP_DESKTOP_FILE_NAME_SUFFIX:
      g_value_set_string (value, self->desktop_file_name_suffix);
      break;

    case PROP_COLLECTION_ID:
      g_value_set_string (value, self->collection_id);
      break;

    case PROP_EXTENSION_TAG:
      g_value_set_string (value, self->extension_tag);
      break;

    case PROP_TOKEN_TYPE:
      g_value_set_int (value, (int)self->token_type);
      break;

    case PROP_SOURCE_DATE_EPOCH:
      g_value_set_int64 (value, self->source_date_epoch);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_manifest_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BuilderManifest *self = BUILDER_MANIFEST (object);
  gchar **tmp;

  switch (prop_id)
    {
    case PROP_APP_ID:
      g_free (self->id);
      self->id = g_value_dup_string (value);
      break;

    case PROP_ID:
      g_free (self->id);
      self->id = g_value_dup_string (value);
      break;

    case PROP_ID_PLATFORM:
      g_free (self->id_platform);
      self->id_platform = g_value_dup_string (value);
      break;

    case PROP_BRANCH:
      g_free (self->branch);
      self->branch = g_value_dup_string (value);
      break;

    case PROP_DEFAULT_BRANCH:
      g_free (self->default_branch);
      self->default_branch = g_value_dup_string (value);
      break;

    case PROP_RUNTIME:
      g_free (self->runtime);
      self->runtime = g_value_dup_string (value);
      break;

    case PROP_RUNTIME_COMMIT:
      g_free (self->runtime_commit);
      self->runtime_commit = g_value_dup_string (value);
      break;

    case PROP_RUNTIME_VERSION:
      g_free (self->runtime_version);
      self->runtime_version = g_value_dup_string (value);
      break;

    case PROP_SDK:
      g_free (self->sdk);
      self->sdk = g_value_dup_string (value);
      break;

    case PROP_SDK_COMMIT:
      g_free (self->sdk_commit);
      self->sdk_commit = g_value_dup_string (value);
      break;

    case PROP_BASE:
      g_free (self->base);
      self->base = g_value_dup_string (value);
      break;

    case PROP_BASE_COMMIT:
      g_free (self->base_commit);
      self->base_commit = g_value_dup_string (value);
      break;

    case PROP_BASE_VERSION:
      g_free (self->base_version);
      self->base_version = g_value_dup_string (value);
      break;

    case PROP_BASE_EXTENSIONS:
      tmp = self->base_extensions;
      self->base_extensions = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_VAR:
      g_free (self->var);
      self->var = g_value_dup_string (value);
      break;

    case PROP_METADATA:
      g_free (self->metadata);
      self->metadata = g_value_dup_string (value);
      break;

    case PROP_METADATA_PLATFORM:
      g_free (self->metadata_platform);
      self->metadata_platform = g_value_dup_string (value);
      break;

    case PROP_COMMAND:
      g_free (self->command);
      self->command = g_value_dup_string (value);
      break;

    case PROP_BUILD_OPTIONS:
      g_set_object (&self->build_options,  g_value_get_object (value));
      break;

    case PROP_MODULES:
      g_list_free_full (self->modules, g_object_unref);
      /* NOTE: This takes ownership of the list! */
      self->modules = g_value_get_pointer (value);
      break;

    case PROP_ADD_EXTENSIONS:
      g_list_free_full (self->add_extensions, g_object_unref);
      /* NOTE: This takes ownership of the list! */
      self->add_extensions = g_value_get_pointer (value);
      break;

    case PROP_ADD_BUILD_EXTENSIONS:
      g_list_free_full (self->add_build_extensions, g_object_unref);
      /* NOTE: This takes ownership of the list! */
      self->add_build_extensions = g_value_get_pointer (value);
      break;

    case PROP_CLEANUP:
      tmp = self->cleanup;
      self->cleanup = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_CLEANUP_COMMANDS:
      tmp = self->cleanup_commands;
      self->cleanup_commands = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_CLEANUP_PLATFORM:
      tmp = self->cleanup_platform;
      self->cleanup_platform = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_CLEANUP_PLATFORM_COMMANDS:
      tmp = self->cleanup_platform_commands;
      self->cleanup_platform_commands = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_PREPARE_PLATFORM_COMMANDS:
      tmp = self->prepare_platform_commands;
      self->prepare_platform_commands = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_FINISH_ARGS:
      tmp = self->finish_args;
      self->finish_args = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_INHERIT_EXTENSIONS:
      tmp = self->inherit_extensions;
      self->inherit_extensions = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_INHERIT_SDK_EXTENSIONS:
      tmp = self->inherit_sdk_extensions;
      self->inherit_sdk_extensions = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_TAGS:
      tmp = self->tags;
      self->tags = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_BUILD_RUNTIME:
      self->build_runtime = g_value_get_boolean (value);
      break;

    case PROP_BUILD_EXTENSION:
      self->build_extension = g_value_get_boolean (value);
      break;

    case PROP_SEPARATE_LOCALES:
      self->separate_locales = g_value_get_boolean (value);
      break;

    case PROP_WRITABLE_SDK:
      self->writable_sdk = g_value_get_boolean (value);
      break;

    case PROP_APPSTREAM_COMPOSE:
      self->appstream_compose = g_value_get_boolean (value);
      break;

    case PROP_SDK_EXTENSIONS:
      tmp = self->sdk_extensions;
      self->sdk_extensions = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_PLATFORM_EXTENSIONS:
      tmp = self->platform_extensions;
      self->platform_extensions = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_COPY_ICON:
      self->copy_icon = g_value_get_boolean (value);
      break;

    case PROP_RENAME_DESKTOP_FILE:
      g_free (self->rename_desktop_file);
      self->rename_desktop_file = g_value_dup_string (value);
      break;

    case PROP_RENAME_APPDATA_FILE:
      g_free (self->rename_appdata_file);
      self->rename_appdata_file = g_value_dup_string (value);
      break;

    case PROP_RENAME_MIME_FILE:
      g_free (self->rename_mime_file);
      self->rename_mime_file = g_value_dup_string (value);
      break;

    case PROP_APPDATA_LICENSE:
      g_free (self->appdata_license);
      self->appdata_license = g_value_dup_string (value);
      break;

    case PROP_RENAME_ICON:
      g_free (self->rename_icon);
      self->rename_icon = g_value_dup_string (value);
      break;

    case PROP_RENAME_MIME_ICONS:
      tmp = self->rename_mime_icons;
      self->rename_mime_icons = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_DESKTOP_FILE_NAME_PREFIX:
      g_free (self->desktop_file_name_prefix);
      self->desktop_file_name_prefix = g_value_dup_string (value);
      break;

    case PROP_DESKTOP_FILE_NAME_SUFFIX:
      g_free (self->desktop_file_name_suffix);
      self->desktop_file_name_suffix = g_value_dup_string (value);
      break;

    case PROP_COLLECTION_ID:
      g_free (self->collection_id);
      self->collection_id = g_value_dup_string (value);
      break;

    case PROP_EXTENSION_TAG:
      g_free (self->extension_tag);
      self->extension_tag = g_value_dup_string (value);
      break;

    case PROP_TOKEN_TYPE:
      self->token_type = (gint32)g_value_get_int (value);
      break;

    case PROP_SOURCE_DATE_EPOCH:
      self->source_date_epoch = g_value_get_int64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_manifest_class_init (BuilderManifestClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = builder_manifest_finalize;
  object_class->get_property = builder_manifest_get_property;
  object_class->set_property = builder_manifest_set_property;

  g_object_class_install_property (object_class,
                                   PROP_APP_ID,
                                   g_param_spec_string ("app-id",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_ID,
                                   g_param_spec_string ("id",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_ID_PLATFORM,
                                   g_param_spec_string ("id-platform",
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
                                   PROP_DEFAULT_BRANCH,
                                   g_param_spec_string ("default-branch",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RUNTIME,
                                   g_param_spec_string ("runtime",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RUNTIME_COMMIT,
                                   g_param_spec_string ("runtime-commit",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RUNTIME_VERSION,
                                   g_param_spec_string ("runtime-version",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SDK,
                                   g_param_spec_string ("sdk",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SDK_COMMIT,
                                   g_param_spec_string ("sdk-commit",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BASE,
                                   g_param_spec_string ("base",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BASE_COMMIT,
                                   g_param_spec_string ("base-commit",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BASE_VERSION,
                                   g_param_spec_string ("base-version",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BASE_EXTENSIONS,
                                   g_param_spec_boxed ("base-extensions",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_VAR,
                                   g_param_spec_string ("var",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_METADATA,
                                   g_param_spec_string ("metadata",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_METADATA_PLATFORM,
                                   g_param_spec_string ("metadata-platform",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_COMMAND,
                                   g_param_spec_string ("command",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BUILD_OPTIONS,
                                   g_param_spec_object ("build-options",
                                                        "",
                                                        "",
                                                        BUILDER_TYPE_OPTIONS,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_MODULES,
                                   g_param_spec_pointer ("modules",
                                                         "",
                                                         "",
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_ADD_EXTENSIONS,
                                   g_param_spec_pointer ("add-extensions",
                                                         "",
                                                         "",
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_ADD_BUILD_EXTENSIONS,
                                   g_param_spec_pointer ("add-build-extensions",
                                                         "",
                                                         "",
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CLEANUP,
                                   g_param_spec_boxed ("cleanup",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CLEANUP_COMMANDS,
                                   g_param_spec_boxed ("cleanup-commands",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CLEANUP_PLATFORM,
                                   g_param_spec_boxed ("cleanup-platform",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_CLEANUP_PLATFORM_COMMANDS,
                                   g_param_spec_boxed ("cleanup-platform-commands",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PREPARE_PLATFORM_COMMANDS,
                                   g_param_spec_boxed ("prepare-platform-commands",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_FINISH_ARGS,
                                   g_param_spec_boxed ("finish-args",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_INHERIT_EXTENSIONS,
                                   g_param_spec_boxed ("inherit-extensions",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_INHERIT_SDK_EXTENSIONS,
                                   g_param_spec_boxed ("inherit-sdk-extensions",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BUILD_RUNTIME,
                                   g_param_spec_boolean ("build-runtime",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BUILD_EXTENSION,
                                   g_param_spec_boolean ("build-extension",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SEPARATE_LOCALES,
                                   g_param_spec_boolean ("separate-locales",
                                                         "",
                                                         "",
                                                         TRUE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_WRITABLE_SDK,
                                   g_param_spec_boolean ("writable-sdk",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_APPSTREAM_COMPOSE,
                                   g_param_spec_boolean ("appstream-compose",
                                                         "",
                                                         "",
                                                         TRUE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SDK_EXTENSIONS,
                                   g_param_spec_boxed ("sdk-extensions",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_PLATFORM_EXTENSIONS,
                                   g_param_spec_boxed ("platform-extensions",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_TAGS,
                                   g_param_spec_boxed ("tags",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RENAME_DESKTOP_FILE,
                                   g_param_spec_string ("rename-desktop-file",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RENAME_APPDATA_FILE,
                                   g_param_spec_string ("rename-appdata-file",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RENAME_MIME_FILE,
                                   g_param_spec_string ("rename-mime-file",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_APPDATA_LICENSE,
                                   g_param_spec_string ("appdata-license",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RENAME_ICON,
                                   g_param_spec_string ("rename-icon",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_RENAME_MIME_ICONS,
                                   g_param_spec_boxed ("rename-mime-icons",
                                                        "",
                                                        "",
                                                        G_TYPE_STRV,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_COPY_ICON,
                                   g_param_spec_boolean ("copy-icon",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_DESKTOP_FILE_NAME_PREFIX,
                                   g_param_spec_string ("desktop-file-name-prefix",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_DESKTOP_FILE_NAME_SUFFIX,
                                   g_param_spec_string ("desktop-file-name-suffix",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_COLLECTION_ID,
                                   g_param_spec_string ("collection-id",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_EXTENSION_TAG,
                                   g_param_spec_string ("extension-tag",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_TOKEN_TYPE,
                                   g_param_spec_int ("token-type",
                                                     "",
                                                     "",
                                                     -1,
                                                     G_MAXINT32,
                                                     -1,
                                                     G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_SOURCE_DATE_EPOCH,
                                   g_param_spec_int64 ("source-date-epoch",
                                                       "",
                                                       "",
                                                       0,
                                                       G_MAXINT64,
                                                       0,
                                                       G_PARAM_READWRITE));
}

static void
builder_manifest_init (BuilderManifest *self)
{
  self->token_type = -1;
  self->appstream_compose = TRUE;
  self->separate_locales = TRUE;
}

static JsonNode *
builder_manifest_serialize_property (JsonSerializable *serializable,
                                     const gchar      *property_name,
                                     const GValue     *value,
                                     GParamSpec       *pspec)
{
  if (strcmp (property_name, "modules") == 0)
    {
      BuilderManifest *self = BUILDER_MANIFEST (serializable);
      JsonNode *retval = NULL;
      GList *l;

      if (self->modules)
        {
          JsonArray *array;

          array = json_array_sized_new (g_list_length (self->modules));

          for (l = self->modules; l != NULL; l = l->next)
            {
              JsonNode *child = json_gobject_serialize (l->data);
              json_array_add_element (array, child);
            }

          retval = json_node_init_array (json_node_alloc (), array);
          json_array_unref (array);
        }

      return retval;
    }
  else if (strcmp (property_name, "add-extensions") == 0 ||
           strcmp (property_name, "add-build-extensions") == 0)
    {
      BuilderManifest *self = BUILDER_MANIFEST (serializable);
      JsonNode *retval = NULL;
      GList *extensions;

      if (strcmp (property_name, "add-extensions") == 0)
        extensions = self->add_extensions;
      else
        extensions = self->add_build_extensions;

      if (extensions)
        {
          JsonObject *object;
          GList *l;

          object = json_object_new ();

          for (l = extensions; l != NULL; l = l->next)
            {
              BuilderExtension *e = l->data;
              JsonNode *child = json_gobject_serialize (G_OBJECT (e));
              json_object_set_member (object, (char *) builder_extension_get_name (e), child);
            }

          retval = json_node_init_object (json_node_alloc (), object);
          json_object_unref (object);
        }

      return retval;
    }
  else
    {
      return builder_serializable_serialize_property (serializable,
                                                      property_name,
                                                      value,
                                                      pspec);
    }
}

static gint
sort_extension (gconstpointer  a,
                gconstpointer  b)
{
  return strcmp (builder_extension_get_name (BUILDER_EXTENSION (a)),
                 builder_extension_get_name (BUILDER_EXTENSION (b)));
}

static gboolean
builder_manifest_deserialize_property (JsonSerializable *serializable,
                                       const gchar      *property_name,
                                       GValue           *value,
                                       GParamSpec       *pspec,
                                       JsonNode         *property_node)
{
  if (strcmp (property_name, "modules") == 0)
    {
      if (JSON_NODE_TYPE (property_node) == JSON_NODE_NULL)
        {
          g_value_set_pointer (value, NULL);
          return TRUE;
        }
      else if (JSON_NODE_TYPE (property_node) == JSON_NODE_ARRAY)
        {
          JsonArray *array = json_node_get_array (property_node);
          guint i, array_len = json_array_get_length (array);
          g_autoptr(GFile) saved_demarshal_base_dir = builder_manifest_get_demarshal_base_dir ();
          GList *modules = NULL;
          GObject *module;

          for (i = 0; i < array_len; i++)
            {
              JsonNode *element_node = json_array_get_element (array, i);

              module = NULL;

              if (JSON_NODE_HOLDS_VALUE (element_node) &&
                  json_node_get_value_type (element_node) == G_TYPE_STRING)
                {
                  const char *module_relpath = json_node_get_string (element_node);
                  g_autoptr(GFile) module_file =
                    g_file_resolve_relative_path (demarshal_base_dir, module_relpath);
                  const char *module_path = flatpak_file_get_path_cached (module_file);
                  g_autofree char *module_contents = NULL;
                  g_autoptr(GError) error = NULL;

                  if (g_file_get_contents (module_path, &module_contents, NULL, &error))
                    {
                      g_autoptr(GFile) module_file_dir = g_file_get_parent (module_file);
                      builder_manifest_set_demarshal_base_dir (module_file_dir);
                      module = builder_gobject_from_data (BUILDER_TYPE_MODULE,
                                                          module_relpath, module_contents, &error);
                      builder_manifest_set_demarshal_base_dir (saved_demarshal_base_dir);
                      if (module)
                        {
                          builder_module_set_json_path (BUILDER_MODULE (module), module_path);
                          builder_module_set_base_dir (BUILDER_MODULE (module), module_file_dir);
                        }
                    }
                  if (error != NULL)
                    {
                      g_error ("Failed to load included manifest (%s): %s", module_path, error->message);
                    }
                }
              else if (JSON_NODE_HOLDS_OBJECT (element_node))
                {
                  module = json_gobject_deserialize (BUILDER_TYPE_MODULE, element_node);
                  if (module != NULL)
                    builder_module_set_base_dir (BUILDER_MODULE (module), saved_demarshal_base_dir);
                }

              if (module == NULL)
                {
                  g_list_free_full (modules, g_object_unref);
                  return FALSE;
                }

              modules = g_list_prepend (modules, module);
            }

          g_value_set_pointer (value, g_list_reverse (modules));

          return TRUE;
        }

      return FALSE;
    }
  else if (strcmp (property_name, "add-extensions") == 0 ||
           strcmp (property_name, "add-build-extensions") == 0)
    {
      if (JSON_NODE_TYPE (property_node) == JSON_NODE_NULL)
        {
          g_value_set_pointer (value, NULL);
          return TRUE;
        }
      else if (JSON_NODE_TYPE (property_node) == JSON_NODE_OBJECT)
        {
          JsonObject *object = json_node_get_object (property_node);
          g_autoptr(GHashTable) hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
          g_autoptr(GList) members = NULL;
          GList *extensions;
          GList *l;

          members = json_object_get_members (object);
          for (l = members; l != NULL; l = l->next)
            {
              const char *member_name = l->data;
              JsonNode *val;
              GObject *extension;

              val = json_object_get_member (object, member_name);
              extension = json_gobject_deserialize (BUILDER_TYPE_EXTENSION, val);
              if (extension == NULL)
                return FALSE;

              builder_extension_set_name (BUILDER_EXTENSION (extension), member_name);
              g_hash_table_insert (hash, (char *)builder_extension_get_name (BUILDER_EXTENSION (extension)), extension);
            }

          extensions = g_hash_table_get_values (hash);
          g_hash_table_steal_all (hash);

          extensions = g_list_sort (extensions, sort_extension);
          g_value_set_pointer (value, extensions);

          return TRUE;
        }

      return FALSE;
    }
  else
    {
      return builder_serializable_deserialize_property (serializable,
                                                        property_name,
                                                        value,
                                                        pspec, property_node);
    }
}

static void
serializable_iface_init (JsonSerializableIface *serializable_iface)
{
  serializable_iface->serialize_property = builder_manifest_serialize_property;
  serializable_iface->deserialize_property = builder_manifest_deserialize_property;
  serializable_iface->find_property = builder_serializable_find_property;
  serializable_iface->list_properties = builder_serializable_list_properties;
  serializable_iface->set_property = builder_serializable_set_property;
  serializable_iface->get_property = builder_serializable_get_property;
}

char *
builder_manifest_serialize (BuilderManifest *self)
{
  JsonNode *node;
  JsonGenerator *generator;
  char *json;

  node = json_gobject_serialize (G_OBJECT (self));
  generator = json_generator_new ();
  json_generator_set_pretty (generator, TRUE);
  json_generator_set_root (generator, node);
  json = json_generator_to_data (generator, NULL);
  g_object_unref (generator);
  json_node_free (node);

  return json;
}

const char *
builder_manifest_get_id (BuilderManifest *self)
{
  return self->id;
}

char *
builder_manifest_get_locale_id (BuilderManifest *self)
{
  g_autofree char *id = flatpak_make_valid_id_prefix (self->id);
  return g_strdup_printf ("%s.Locale", id);
}

char *
builder_manifest_get_debug_id (BuilderManifest *self)
{
  g_autofree char *id = flatpak_make_valid_id_prefix (self->id);
  return g_strdup_printf ("%s.Debug", id);
}

char *
builder_manifest_get_sources_id (BuilderManifest *self)
{
  g_autofree char *id = flatpak_make_valid_id_prefix (self->id);
  return g_strdup_printf ("%s.Sources", id);
}

const char *
builder_manifest_get_id_platform (BuilderManifest *self)
{
  return self->id_platform;
}

char *
builder_manifest_get_locale_id_platform (BuilderManifest *self)
{
  char *res = NULL;

  if (self->id_platform != NULL)
    {
      g_autofree char *id = flatpak_make_valid_id_prefix (self->id_platform);
      res = g_strdup_printf ("%s.Locale", id);
    }

  return res;
}

BuilderOptions *
builder_manifest_get_build_options (BuilderManifest *self)
{
  return self->build_options;
}

GList *
builder_manifest_get_modules (BuilderManifest *self)
{
  return self->modules;
}

GList *
builder_manifest_get_add_extensions (BuilderManifest *self)
{
  return self->add_extensions;
}

GList *
builder_manifest_get_add_build_extensions (BuilderManifest *self)
{
  return self->add_build_extensions;
}

static const char *
builder_manifest_get_runtime_version (BuilderManifest *self)
{
  return self->runtime_version ? self->runtime_version : "master";
}

const char *
builder_manifest_get_branch (BuilderManifest *self,
                             BuilderContext  *context)
{
  if (self->branch)
    return self->branch;

  if (context &&
      builder_context_get_default_branch (context))
    return builder_context_get_default_branch (context);

  if (self->default_branch)
    return self->default_branch;

  return "master";
}

const char *
builder_manifest_get_collection_id (BuilderManifest *self)
{
  return self->collection_id;
}

void
builder_manifest_set_default_collection_id (BuilderManifest *self,
                                            const char      *default_collection_id)
{
  if (self->collection_id == NULL)
    self->collection_id = g_strdup (default_collection_id);
}

gint32
builder_manifest_get_token_type (BuilderManifest *self)
{
  return self->token_type;
}

void
builder_manifest_set_default_token_type (BuilderManifest *self,
                                         gint32           default_token_type)
{
  if (self->token_type == -1)
    self->token_type = default_token_type;
}

void
builder_manifest_add_tags (BuilderManifest *self,
                          const char      **add_tags)
{
  GPtrArray *new_tags = g_ptr_array_new ();
  int i;

  for (i = 0; self->tags != NULL && self->tags[i] != NULL; i++)
    g_ptr_array_add (new_tags, self->tags[i]);

  for (i = 0; add_tags[i] != NULL; i++)
    {
      const char *new_tag = add_tags[i];
      if (self->tags == NULL || !g_strv_contains ((const char **)self->tags, new_tag))
        g_ptr_array_add (new_tags, g_strdup (new_tag));
    }

  g_ptr_array_add (new_tags, NULL);

  g_free (self->tags);
  self->tags = (char **)g_ptr_array_free (new_tags, FALSE);

}

void
builder_manifest_remove_tags (BuilderManifest *self,
                             const char      **remove_tags)
{
  GPtrArray *new_tags = g_ptr_array_new ();
  int i;

  if (self->tags)
    {
      for (i = 0; self->tags[i] != NULL; i++)
        {
          char *old_tag = self->tags[i];
          if (g_strv_contains (remove_tags, old_tag))
            g_free (old_tag);
          else
            g_ptr_array_add (new_tags, old_tag);
        }
    }

  g_ptr_array_add (new_tags, NULL);

  g_free (self->tags);
  self->tags = (char **)g_ptr_array_free (new_tags, FALSE);
}


const char *
builder_manifest_get_extension_tag (BuilderManifest *self)
{
  return self->extension_tag;
}

static const char *
builder_manifest_get_base_version (BuilderManifest *self)
{
  return self->base_version ? self->base_version : builder_manifest_get_branch (self, NULL);
}

G_GNUC_NULL_TERMINATED
static char *
flatpak (GError **error,
         ...)
{
  gboolean res;
  g_autofree char *output = NULL;
  g_autoptr(GPtrArray) ar = g_ptr_array_new ();
  va_list ap;

  va_start (ap, error);
  g_ptr_array_add (ar, "flatpak");
  while (TRUE)
    {
      gchar *param = va_arg (ap, gchar *);
      g_ptr_array_add (ar, param);
      if (param == NULL)
        break;
    }
  va_end (ap);

  res = builder_maybe_host_spawnv (NULL, &output, 0, error,
                                   (const gchar * const *)ar->pdata, NULL);

  if (res)
    {
      g_strchomp (output);
      return g_steal_pointer (&output);
    }

  return NULL;
}

static void
add_installation_args (GPtrArray *args,
                       gboolean opt_user,
                       const char *opt_installation)
{
  if (opt_user)
    g_ptr_array_add (args, g_strdup ("--user"));
  else if (opt_installation)
    g_ptr_array_add (args, g_strdup_printf ("--installation=%s", opt_installation));
  else
    g_ptr_array_add (args, g_strdup ("--system"));
}

static char *
flatpak_info (gboolean opt_user,
              const char *opt_installation,
              const char *ref,
              const char *extra_arg,
              GError **error)
{
  gboolean res;
  g_autofree char *output = NULL;
  g_autoptr(GPtrArray) args = NULL;

  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("flatpak"));
  add_installation_args (args, opt_user, opt_installation);
  g_ptr_array_add (args, g_strdup ("info"));
  if (extra_arg)
    g_ptr_array_add (args, g_strdup (extra_arg));
  g_ptr_array_add (args, g_strdup (ref));
  g_ptr_array_add (args, NULL);

  res = builder_maybe_host_spawnv (NULL, &output, G_SUBPROCESS_FLAGS_STDERR_SILENCE, error, (const char * const *)args->pdata, NULL);

  if (res)
    {
      g_strchomp (output);
      return g_steal_pointer (&output);
    }
  return NULL;
}

static char *
flatpak_info_show_path (const char *id,
                        const char *branch,
                        BuilderContext  *context)
{
  g_autofree char *output = NULL;
  g_autofree char *arch_option = NULL;

  arch_option = g_strdup_printf ("--arch=%s", builder_context_get_arch (context));

  output = flatpak (NULL, "info", "--show-location", arch_option, id, branch, NULL);
  if (output == NULL)
    return NULL;

  g_strchomp (output);
  return g_steal_pointer (&output);
}

gboolean
builder_manifest_start (BuilderManifest *self,
                        gboolean download_only,
                        gboolean allow_missing_runtimes,
                        BuilderContext  *context,
                        GError         **error)
{
  g_autofree char *arch_option = NULL;
  g_autoptr(GHashTable) names = g_hash_table_new (g_str_hash, g_str_equal);
  g_autofree char *sdk_path = NULL;
  const char *stop_at;

  self->source_date_epoch = builder_context_get_source_date_epoch (context);

  if (self->sdk == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "sdk not specified");
      return FALSE;
    }

  arch_option = g_strdup_printf ("--arch=%s", builder_context_get_arch (context));

  self->sdk_commit = flatpak (NULL, "info", arch_option, "--show-commit", self->sdk,
                              builder_manifest_get_runtime_version (self), NULL);
  if (!download_only && !allow_missing_runtimes && self->sdk_commit == NULL)
    return flatpak_fail (error, "Unable to find sdk %s version %s",
                         self->sdk,
                         builder_manifest_get_runtime_version (self));

  sdk_path = flatpak_info_show_path (self->sdk, builder_manifest_get_runtime_version (self), context);
  if (sdk_path != NULL &&
      !builder_context_load_sdk_config (context, sdk_path, error))
    return FALSE;

  self->runtime_commit = flatpak (NULL, "info", arch_option, "--show-commit", self->runtime,
                                  builder_manifest_get_runtime_version (self), NULL);
  if (!download_only && !allow_missing_runtimes && self->runtime_commit == NULL)
    return flatpak_fail (error, "Unable to find runtime %s version %s",
                         self->runtime,
                         builder_manifest_get_runtime_version (self));

  if (self->base != NULL && *self->base != 0)
    {
      self->base_commit = flatpak (NULL, "info", arch_option, "--show-commit", self->base,
                                   builder_manifest_get_base_version (self), NULL);
      if (!download_only && self->base_commit == NULL)
        return flatpak_fail (error, "Unable to find app %s version %s",
                             self->base, builder_manifest_get_base_version (self));
    }

  if (!expand_modules (context, self->modules, &self->expanded_modules, names, error))
    return FALSE;

  stop_at = builder_context_get_stop_at (context);
  if (stop_at != NULL && g_hash_table_lookup (names, stop_at) == NULL)
    return flatpak_fail (error, "No module named %s (specified with --stop-at)", stop_at);

  return TRUE;
}

gboolean
builder_manifest_init_app_dir (BuilderManifest *self,
                               BuilderCache    *cache,
                               BuilderContext  *context,
                               GError         **error)
{
  GFile *app_dir = builder_context_get_app_dir (context);

  g_autoptr(GPtrArray) args = NULL;
  GList *l;
  int i;

  g_print ("Initializing build dir\n");

  if (self->id == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "id not specified");
      return FALSE;
    }

  if (self->runtime == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "runtime not specified");
      return FALSE;
    }

  if (self->sdk == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "sdk not specified");
      return FALSE;
    }

  args = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (args, g_strdup ("flatpak"));
  g_ptr_array_add (args, g_strdup ("build-init"));
  if (self->writable_sdk || self->build_runtime)
    {
      if (self->build_runtime)
        g_ptr_array_add (args, g_strdup ("--type=runtime"));
      else
        g_ptr_array_add (args, g_strdup ("--writable-sdk"));
    }

  for (l = self->add_build_extensions; l != NULL; l = l->next)
    builder_extension_add_finish_args (l->data, args);

  for (i = 0; self->sdk_extensions != NULL && self->sdk_extensions[i] != NULL; i++)
    {
      const char *ext = self->sdk_extensions[i];
      g_ptr_array_add (args, g_strdup_printf ("--sdk-extension=%s", ext));
    }

  if (self->build_extension)
    {
      g_ptr_array_add (args, g_strdup ("--type=extension"));
    }
  if (self->tags)
    {
      for (i = 0; self->tags[i] != NULL; i++)
        g_ptr_array_add (args, g_strdup_printf ("--tag=%s", self->tags[i]));
    }
  if (self->var)
    g_ptr_array_add (args, g_strdup_printf ("--var=%s", self->var));

  if (self->base)
    {
      g_ptr_array_add (args, g_strdup_printf ("--base=%s", self->base));
      g_ptr_array_add (args, g_strdup_printf ("--base-version=%s", builder_manifest_get_base_version (self)));

      for (i = 0; self->base_extensions != NULL && self->base_extensions[i] != NULL; i++)
        {
          const char *ext = self->base_extensions[i];
          g_ptr_array_add (args, g_strdup_printf ("--base-extension=%s", ext));
        }
    }

  if (self->extension_tag != NULL)
    g_ptr_array_add (args, g_strdup_printf ("--extension-tag=%s", self->extension_tag));

  g_ptr_array_add (args, g_strdup_printf ("--arch=%s", builder_context_get_arch (context)));
  g_ptr_array_add (args, g_file_get_path (app_dir));
  g_ptr_array_add (args, g_strdup (self->id));
  g_ptr_array_add (args, g_strdup (self->sdk));
  g_ptr_array_add (args, g_strdup (self->runtime));
  g_ptr_array_add (args, g_strdup (builder_manifest_get_runtime_version (self)));
  g_ptr_array_add (args, NULL);

  if (!flatpak_spawnv (NULL, NULL, G_SUBPROCESS_FLAGS_NONE, error,
                       (const gchar * const *) args->pdata, NULL))
    return FALSE;

  if (self->build_runtime && self->separate_locales)
    {
      g_autoptr(GFile) root_dir = NULL;

      root_dir = g_file_get_child (app_dir, "usr");

      if (!builder_migrate_locale_dirs (root_dir, error))
        return FALSE;
    }

  /* Fix up any python timestamps from base */
  if (!builder_post_process (BUILDER_POST_PROCESS_FLAGS_PYTHON_TIMESTAMPS, app_dir,
                             cache, context, error))
    return FALSE;

  return TRUE;
}

/* This gets the checksum of everything that globally affects the build */
void
builder_manifest_checksum (BuilderManifest *self,
                           BuilderCache    *cache,
                           BuilderContext  *context)
{
  GList *l;

  builder_cache_checksum_str (cache, BUILDER_MANIFEST_CHECKSUM_VERSION);
  builder_cache_checksum_str (cache, self->id);
  /* No need to include version here, it doesn't affect the build */
  builder_cache_checksum_str (cache, self->runtime);
  builder_cache_checksum_str (cache, builder_manifest_get_runtime_version (self));
  builder_cache_checksum_str (cache, self->sdk);
  /* Always rebuild on sdk change if we're actually including the sdk in the cache */
  if (self->writable_sdk || self->build_runtime ||
      builder_context_get_rebuild_on_sdk_change (context))
    builder_cache_checksum_str (cache, self->sdk_commit);
  builder_cache_checksum_str (cache, self->var);
  builder_cache_checksum_str (cache, self->metadata);
  builder_cache_checksum_strv (cache, self->tags);
  builder_cache_checksum_boolean (cache, self->writable_sdk);
  builder_cache_checksum_strv (cache, self->sdk_extensions);
  builder_cache_checksum_boolean (cache, self->build_runtime);
  builder_cache_checksum_boolean (cache, self->build_extension);
  builder_cache_checksum_boolean (cache, self->separate_locales);
  builder_cache_checksum_str (cache, self->base);
  builder_cache_checksum_str (cache, self->base_version);
  builder_cache_checksum_str (cache, self->base_commit);
  builder_cache_checksum_strv (cache, self->base_extensions);
  builder_cache_checksum_compat_str (cache, self->extension_tag);

  if (self->build_options)
    builder_options_checksum (self->build_options, cache, context);

  for (l = self->add_build_extensions; l != NULL; l = l->next)
    {
      BuilderExtension *e = l->data;
      builder_extension_checksum (e, cache, context);
    }
}

static void
builder_manifest_checksum_for_cleanup (BuilderManifest *self,
                                       BuilderCache    *cache,
                                       BuilderContext  *context)
{
  GList *l;

  builder_cache_checksum_str (cache, BUILDER_MANIFEST_CHECKSUM_CLEANUP_VERSION);
  builder_cache_checksum_strv (cache, self->cleanup);
  builder_cache_checksum_strv (cache, self->cleanup_commands);
  builder_cache_checksum_str (cache, self->rename_desktop_file);
  builder_cache_checksum_str (cache, self->rename_appdata_file);
  builder_cache_checksum_str (cache, self->rename_mime_file);
  builder_cache_checksum_str (cache, self->appdata_license);
  builder_cache_checksum_str (cache, self->rename_icon);
  builder_cache_checksum_strv (cache, self->rename_mime_icons);
  builder_cache_checksum_boolean (cache, self->copy_icon);
  builder_cache_checksum_str (cache, self->desktop_file_name_prefix);
  builder_cache_checksum_str (cache, self->desktop_file_name_suffix);
  builder_cache_checksum_boolean (cache, self->appstream_compose);

  for (l = self->expanded_modules; l != NULL; l = l->next)
    {
      BuilderModule *m = l->data;
      builder_module_checksum_for_cleanup (m, cache, context);
    }
}

static void
builder_manifest_checksum_for_finish (BuilderManifest *self,
                                      BuilderCache    *cache,
                                      BuilderContext  *context)
{
  GList *l;
  g_autofree char *json = NULL;

  builder_cache_checksum_str (cache, BUILDER_MANIFEST_CHECKSUM_FINISH_VERSION);
  builder_cache_checksum_strv (cache, self->finish_args);
  builder_cache_checksum_str (cache, self->command);
  builder_cache_checksum_strv (cache, self->inherit_extensions);
  builder_cache_checksum_compat_strv (cache, self->inherit_sdk_extensions);

  for (l = self->add_extensions; l != NULL; l = l->next)
    {
      BuilderExtension *e = l->data;
      builder_extension_checksum (e, cache, context);
    }

  if (self->metadata)
    {
      GFile *base_dir = builder_context_get_base_dir (context);
      g_autoptr(GFile) metadata = g_file_resolve_relative_path (base_dir, self->metadata);
      g_autofree char *data = NULL;
      g_autoptr(GError) my_error = NULL;
      gsize len;

      if (g_file_load_contents (metadata, NULL, &data, &len, NULL, &my_error))
        builder_cache_checksum_data (cache, (guchar *) data, len);
      else
        g_warning ("Can't load metadata file %s: %s", self->metadata, my_error->message);
    }

  json = builder_manifest_serialize (self);
  builder_cache_checksum_str (cache, json);
}

static void
builder_manifest_checksum_for_bundle_sources (BuilderManifest *self,
                                              BuilderCache    *cache,
                                              BuilderContext  *context)
{
  builder_cache_checksum_str (cache, BUILDER_MANIFEST_CHECKSUM_BUNDLE_SOURCES_VERSION);
  builder_cache_checksum_boolean (cache, builder_context_get_bundle_sources (context));
}

static void
builder_manifest_checksum_for_platform_base (BuilderManifest *self,
                                             BuilderCache    *cache,
                                             BuilderContext  *context)
{
  builder_cache_checksum_str (cache, BUILDER_MANIFEST_CHECKSUM_PLATFORM_VERSION);
  builder_cache_checksum_str (cache, self->id_platform);
  builder_cache_checksum_str (cache, self->runtime_commit);
  builder_cache_checksum_strv (cache, self->platform_extensions);
  builder_cache_checksum_str (cache, self->metadata_platform);

  if (self->metadata_platform)
    {
      GFile *base_dir = builder_context_get_base_dir (context);
      g_autoptr(GFile) metadata = g_file_resolve_relative_path (base_dir, self->metadata_platform);
      g_autofree char *data = NULL;
      g_autoptr(GError) my_error = NULL;
      gsize len;

      if (g_file_load_contents (metadata, NULL, &data, &len, NULL, &my_error))
        builder_cache_checksum_data (cache, (guchar *) data, len);
      else
        g_warning ("Can't load metadata-platform file %s: %s", self->metadata_platform, my_error->message);
    }
}

static void
builder_manifest_checksum_for_platform_prepare (BuilderManifest *self,
                                                BuilderCache    *cache,
                                                BuilderContext  *context)
{
  GList *l;

  builder_cache_checksum_strv (cache, self->prepare_platform_commands);
  builder_cache_checksum_strv (cache, self->cleanup_platform);
  for (l = self->expanded_modules; l != NULL; l = l->next)
    {
      BuilderModule *m = l->data;
      builder_module_checksum_for_platform_cleanup (m, cache, context);
    }
}

static void
builder_manifest_checksum_for_platform_finish (BuilderManifest *self,
                                               BuilderCache    *cache,
                                               BuilderContext  *context)
{
  builder_cache_checksum_strv (cache, self->cleanup_platform_commands);
}

gboolean
builder_manifest_download (BuilderManifest *self,
                           gboolean         update_vcs,
                           const char      *only_module,
                           BuilderContext  *context,
                           GError         **error)
{
  const char *stop_at = builder_context_get_stop_at (context);
  GList *l;

  g_print ("Downloading sources\n");
  for (l = self->expanded_modules; l != NULL; l = l->next)
    {
      BuilderModule *m = l->data;
      const char *name = builder_module_get_name (m);

      if (only_module && strcmp (name, only_module) != 0)
        continue;

      if (stop_at != NULL && strcmp (name, stop_at) == 0)
        {
          g_print ("Stopping at module %s\n", stop_at);
          return TRUE;
        }

      if (!builder_module_download_sources (m, update_vcs, context, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
setup_context (BuilderManifest *self,
               BuilderContext  *context,
               GError         **error)
{
  builder_context_set_options (context, self->build_options);
  builder_context_set_global_cleanup (context, (const char **) self->cleanup);
  builder_context_set_global_cleanup_platform (context, (const char **) self->cleanup_platform);
  if (self->build_runtime && self->build_extension)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Can't build both a runtime and an extension");
      return FALSE;
    }
  builder_context_set_build_runtime (context, self->build_runtime);
  builder_context_set_build_extension (context, self->build_extension);
  builder_context_set_separate_locales (context, self->separate_locales);
  return TRUE;
}

gboolean
builder_manifest_build_shell (BuilderManifest *self,
                              BuilderContext  *context,
                              const char      *modulename,
                              GError         **error)
{
  GList *l;
  BuilderModule *found = NULL;

  if (!builder_context_enable_rofiles (context, error))
    return FALSE;

  if (!setup_context (self, context, error))
    return FALSE;

  for (l = self->expanded_modules; l != NULL; l = l->next)
    {
      BuilderModule *m = l->data;
      const char *name = builder_module_get_name (m);

      if (strcmp (name, modulename) == 0)
        {
          found = m;
          break;
        }
    }

  if (found == NULL)
    return flatpak_fail (error, "Can't find module %s", modulename);

  if (!builder_module_build (found, NULL, context, TRUE, error))
    return FALSE;

  return TRUE;
}

gboolean
builder_manifest_build (BuilderManifest *self,
                        BuilderCache    *cache,
                        BuilderContext  *context,
                        GError         **error)
{
  const char *stop_at = builder_context_get_stop_at (context);
  GList *l;

  if (!setup_context (self, context, error))
    return FALSE;

  g_print ("Starting build of %s\n", self->id ? self->id : "app");
  for (l = self->expanded_modules; l != NULL; l = l->next)
    {
      BuilderModule *m = l->data;
      g_autoptr(GPtrArray) changes = NULL;
      const char *name = builder_module_get_name (m);

      g_autofree char *stage = g_strdup_printf ("build-%s", name);

      if (stop_at != NULL && strcmp (name, stop_at) == 0)
        {
          g_print ("Stopping at module %s\n", stop_at);
          return TRUE;
        }

      if (!builder_module_should_build (m))
        {
          g_print ("Skipping module %s (no sources)\n", name);
          continue;
        }

      builder_module_checksum (m, cache, context);

      if (!builder_cache_lookup (cache, stage))
        {
          g_autofree char *body =
            g_strdup_printf ("Built %s\n", name);
          if (!builder_module_ensure_writable (m, cache, context, error))
            return FALSE;
          if (!builder_context_enable_rofiles (context, error))
            return FALSE;
          if (!builder_module_build (m, cache, context, FALSE, error))
            return FALSE;
          if (!builder_context_disable_rofiles (context, error))
            return FALSE;
          if (!builder_cache_commit (cache, body, error))
            return FALSE;
        }
      else
        {
          g_print ("Cache hit for %s, skipping build\n", name);
        }

      changes = builder_cache_get_changes (cache, error);
      if (changes == NULL)
        return FALSE;

      builder_module_set_changes (m, changes);

      builder_module_update (m, context, error);
    }

  return TRUE;
}

static gboolean
command (GFile      *app_dir,
         char      **env_vars,
         char      **extra_args,
         const char *commandline,
         GError    **error)
{
  g_autoptr(GPtrArray) args = NULL;
  int i;

  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("flatpak"));
  g_ptr_array_add (args, g_strdup ("build"));

  g_ptr_array_add (args, g_strdup ("--die-with-parent"));
  g_ptr_array_add (args, g_strdup ("--nofilesystem=host:reset"));
  if (extra_args)
    {
      for (i = 0; extra_args[i] != NULL; i++)
        g_ptr_array_add (args, g_strdup (extra_args[i]));
    }

  if (env_vars)
    {
      for (i = 0; env_vars[i] != NULL; i++)
        g_ptr_array_add (args, g_strdup_printf ("--env=%s", env_vars[i]));
    }

  g_ptr_array_add (args, g_file_get_path (app_dir));

  g_ptr_array_add (args, g_strdup ("/bin/sh"));
  g_ptr_array_add (args, g_strdup ("-c"));
  g_ptr_array_add (args, g_strdup (commandline));
  g_ptr_array_add (args, NULL);

  return builder_maybe_host_spawnv (NULL, NULL, 0, error, (const char * const *)args->pdata, NULL);
}

typedef struct {
  const char *rename_icon;
  gboolean    copy_icon;
  const char *id;
} ForeachFile;

typedef gboolean (*ForeachFileFunc) (ForeachFile     *self,
                                     int              source_parent_fd,
                                     const char      *source_name,
                                     const char      *full_dir,
                                     const char      *rel_dir,
                                     struct stat     *stbuf,
                                     gboolean        *found,
                                     int              depth,
                                     GError         **error);

static gboolean
foreach_file_helper (ForeachFile     *self,
                     ForeachFileFunc  func,
                     int              source_parent_fd,
                     const char      *source_name,
                     const char      *full_dir,
                     const char      *rel_dir,
                     gboolean        *found,
                     int              depth,
                     GError         **error)
{
  g_auto(GLnxDirFdIterator) source_iter = {0};
  struct dirent *dent;
  g_autoptr(GError) my_error = NULL;

  if (!glnx_dirfd_iterator_init_at (source_parent_fd, source_name, FALSE, &source_iter, &my_error))
    {
      if (g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        return TRUE;

      g_propagate_error (error, g_steal_pointer (&my_error));
      return FALSE;
    }

  while (TRUE)
    {
      struct stat stbuf;

      if (!glnx_dirfd_iterator_next_dent (&source_iter, &dent, NULL, error))
        return FALSE;

      if (dent == NULL)
        break;

      if (fstatat (source_iter.fd, dent->d_name, &stbuf, AT_SYMLINK_NOFOLLOW) == -1)
        {
          if (errno == ENOENT)
            {
              continue;
            }
          else
            {
              glnx_set_error_from_errno (error);
              return FALSE;
            }
        }

      if (S_ISDIR (stbuf.st_mode))
        {
          g_autofree char *child_dir = g_build_filename (full_dir, dent->d_name, NULL);
          g_autofree char *child_rel_dir = g_build_filename (rel_dir, dent->d_name, NULL);
          if (!foreach_file_helper (self, func, source_iter.fd, dent->d_name, child_dir, child_rel_dir, found, depth + 1, error))
            return FALSE;
        }

      if (!func (self, source_iter.fd, dent->d_name, full_dir, rel_dir, &stbuf, found, depth, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
foreach_file (ForeachFile     *self,
              ForeachFileFunc  func,
              gboolean        *found,
              GFile           *root,
              GError         **error)
{
  return foreach_file_helper (self, func, AT_FDCWD,
                              flatpak_file_get_path_cached (root),
                              flatpak_file_get_path_cached (root),
                              "",
                              found, 0,
                              error);
}

static gboolean
_rename_icon (ForeachFile     *self,
              int              source_parent_fd,
              const char      *source_name,
              const char      *full_dir,
              const char      *rel_dir,
              gboolean         prefix,
              struct stat     *stbuf,
              gboolean        *found,
              int              depth,
              GError         **error)
{
  if (g_str_has_prefix (source_name, self->rename_icon))
    {
      if (S_ISREG (stbuf->st_mode) &&
          depth == 3 &&
          (g_str_has_prefix (source_name + strlen (self->rename_icon), ".") ||
           g_str_has_prefix (source_name + strlen (self->rename_icon), "-symbolic.")))
        {
          const char *extension = source_name + strlen (self->rename_icon);
          g_autofree char *new_name;
          int res;
          if (!prefix)
            new_name = g_strconcat (self->id, extension, NULL);
          else
            new_name = g_strconcat (self->id, ".", source_name, NULL);

          *found = TRUE;

          g_print ("%s icon %s/%s to %s/%s\n", self->copy_icon ? "Copying" : "Renaming", rel_dir, source_name, rel_dir, new_name);

          if (self->copy_icon)
            res = linkat (source_parent_fd, source_name, source_parent_fd, new_name, AT_SYMLINK_FOLLOW);
          else
            res = renameat (source_parent_fd, source_name, source_parent_fd, new_name);

          if (res != 0)
            {
              g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Can't rename icon %s/%s", rel_dir, source_name);
              return FALSE;
            }
        }
      else
        {
          if (!S_ISREG (stbuf->st_mode))
            g_debug ("%s/%s matches 'rename-icon', but not a regular file", full_dir, source_name);
          else if (depth != 3)
            g_debug ("%s/%s matches 'rename-icon', but not at depth 3", full_dir, source_name);
          else if (g_str_has_suffix (self->rename_icon, ".png") || g_str_has_suffix (self->rename_icon, ".svg"))
            g_debug ("%s/%s matches 'rename-icon', but 'rename-icon' incorrectly includes a file extension.", full_dir, source_name);
          else
            g_debug ("%s/%s matches 'rename-icon', but name does not continue with '.' or '-symbolic.'", full_dir, source_name);
        }
    }

  return TRUE;
}

static gboolean
rename_icon_cb (ForeachFile     *self,
                int              source_parent_fd,
                const char      *source_name,
                const char      *full_dir,
                const char      *rel_dir,
                struct stat     *stbuf,
                gboolean        *found,
                int              depth,
                GError         **error)
{
  return _rename_icon (self,
                       source_parent_fd, source_name,
                       full_dir, rel_dir, FALSE,
                       stbuf, found, depth, error);
}

static gboolean
rename_mime_icon_cb (ForeachFile     *self,
                     int              source_parent_fd,
                     const char      *source_name,
                     const char      *full_dir,
                     const char      *rel_dir,
                     struct stat     *stbuf,
                     gboolean        *found,
                     int              depth,
                     GError         **error)
{
  return _rename_icon (self,
                       source_parent_fd, source_name,
                       full_dir, rel_dir, TRUE,
                       stbuf, found, depth, error);
}

static int
cmpstringp (const void *p1, const void *p2)
{
  return strcmp (*(char * const *) p1, *(char * const *) p2);
}

static gboolean
appstreamcli_compose (GError **error,
                      ...)
{
  g_autoptr(GPtrArray) args = NULL;
  const gchar *arg;
  va_list ap;

  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("appstreamcli"));
  g_ptr_array_add (args, g_strdup ("compose"));

  va_start (ap, error);
  while ((arg = va_arg (ap, const gchar *)))
    g_ptr_array_add (args, g_strdup (arg));
  g_ptr_array_add (args, NULL);
  va_end (ap);

  if (!flatpak_spawnv (NULL, NULL, 0, error, (const char * const *)args->pdata, NULL))
    {
      g_prefix_error (error, "ERROR: appstreamcli compose failed: ");
      return FALSE;
    }

  return TRUE;
}

static char **
strcatv (char **strv1,
         char **strv2)
{
    if (strv1 == NULL && strv2 == NULL)
        return NULL;
    if (strv1 == NULL)
        return g_strdupv (strv2);
    if (strv2 == NULL)
        return g_strdupv (strv1);

    unsigned len1 = g_strv_length (strv1);
    unsigned len2 = g_strv_length (strv2);
    char **retval = g_new (char *, len1 + len2 + 1);
    unsigned ix;

    for (ix = 0; ix < len1; ix++)
        retval[ix] = g_strdup (strv1[ix]);
    for (ix = 0; ix < len2; ix++)
        retval[len1 + ix] = g_strdup (strv2[ix]);
    retval[len1 + len2] = NULL;

    return retval;
}

static gboolean
rewrite_appdata (GFile *file,
                 const char *license,
                 GError **error)
{
  g_autofree gchar *data = NULL;
  gsize data_len;
  g_autoptr(xmlDoc) doc = NULL;
  xml_autofree xmlChar *xmlbuff = NULL;
  int buffersize;
  xmlNode *root_element, *component_node;

  if (!g_file_load_contents (file, NULL, &data, &data_len, NULL, error))
    return FALSE;

  doc = xmlReadMemory (data, data_len, NULL, NULL,  0);
  if (doc == NULL)
    return flatpak_fail (error, _("Error parsing appstream"));

  root_element = xmlDocGetRootElement (doc);

  for (component_node = root_element; component_node; component_node = component_node->next)
    {
      xmlNode *sub_node = NULL;
      xmlNode *license_node = NULL;

      if (component_node->type != XML_ELEMENT_NODE ||
          strcmp ((char *)component_node->name, "component") != 0)
        continue;

      for (sub_node = component_node->children; sub_node; sub_node = sub_node->next)
        {
          if (sub_node->type != XML_ELEMENT_NODE ||
              strcmp ((char *)sub_node->name, "project_license") != 0)
            continue;

          license_node = sub_node;
          break;
        }

      if (license_node)
        xmlNodeSetContent(license_node, (xmlChar *)license);
      else
        xmlNewChild(component_node, NULL, (xmlChar *)"project_license", (xmlChar *)license);
    }

  xmlDocDumpFormatMemory (doc, &xmlbuff, &buffersize, 1);

  if (!g_file_set_contents (flatpak_file_get_path_cached (file),
                            (gchar *)xmlbuff, buffersize,
                            error))
    return FALSE;

  return TRUE;
}

static GFile *
builder_manifest_find_appdata_file (BuilderManifest *self,
                                    GFile           *app_root)
{
  /* We order these so that share/metainfo/$FLATPAK_ID.metainfo.xml is found
     first, as this is the target name, and apps may have both, which will
     cause issues with the rename. */
  const char *extensions[] = {
    ".metainfo.xml",
    ".appdata.xml",
  };
  const char *dirs[] = {
    "share/metainfo",
    "share/appdata",
  };
  g_autoptr(GFile) source = NULL;

  int i, j;
  for (j = 0; j < G_N_ELEMENTS (dirs); j++)
    {
      g_autoptr(GFile) appdata_dir = g_file_resolve_relative_path (app_root, dirs[j]);
      for (i = 0; i < G_N_ELEMENTS (extensions); i++)
        {
          g_autofree char *basename = NULL;

          if (self->rename_appdata_file != NULL)
            basename = g_strdup (self->rename_appdata_file);
          else
            basename = g_strconcat (self->id, extensions[i], NULL);

          source = g_file_get_child (appdata_dir, basename);
          if (g_file_query_exists (source, NULL))
            return g_steal_pointer (&source);

          g_clear_object (&source);
        }
    }
  return NULL;
}

/* Perform `rename-mime-file` */
static gboolean
_cleanup_rename_mime_file (BuilderManifest *self, GFile *app_root,
                           GError **error)
{
  g_autoptr(GFile) applications_dir = g_file_resolve_relative_path (app_root, "share/mime/packages");
  g_autoptr(GFile) src = g_file_get_child (applications_dir, self->rename_mime_file);
  g_autofree char *mime_basename = g_strdup_printf ("%s.xml", self->id);
  g_autoptr(GFile) dest = g_file_get_child (applications_dir, mime_basename);

  g_print ("Renaming %s to %s\n", self->rename_mime_file, mime_basename);
  if (!g_file_move (src, dest, 0, NULL, NULL, NULL, error))
    return FALSE;

  return TRUE;
}

static gboolean
_rename_mime_icon (BuilderManifest *self, const char *rename_icon,
                   GFile *icons_dir,
                   GError **error)
{
  gboolean found_icon = FALSE;

  ForeachFile renamer;
  renamer.rename_icon = rename_icon;
  renamer.copy_icon = self->copy_icon;
  renamer.id = self->id;
  if (!foreach_file (&renamer, rename_mime_icon_cb, &found_icon, icons_dir, error))
    return FALSE;

  if (!found_icon)
    {
      g_autofree char *icon_path = g_file_get_path (icons_dir);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "icon %s not found below %s",
                   rename_icon, icon_path);
      return FALSE;
    }

  return TRUE;
}

/* Rename the icons inside the mime_file.
 *
 * The problem is that they may not be in the file as there is an
 * automatic mapping of mimetypes with icon name, so we have to add
 * them to he mime file in that case.
 */
static gboolean
_cleanup_mime_file_rename_icons (char **rename_mime_icons,
                                 GFile *mime_file,
                                 const char *id, GError **error)
{
  FlatpakXml *n_root;
  g_autoptr(FlatpakXml) xml_root = NULL;
  g_autoptr(GInputStream) in = NULL;
  g_autoptr(GString) new_contents = NULL;

  in = (GInputStream *) g_file_read (mime_file, NULL, error);
  if (!in)
    return FALSE;
  xml_root = flatpak_xml_parse (in, FALSE, NULL, error);
  if (!xml_root)
    return FALSE;

  n_root = flatpak_xml_find (xml_root, "mime-info", NULL);

  for (char **current = rename_mime_icons; *current; current++)
    {
      FlatpakXml *n_type = NULL;
      while ((n_type = flatpak_xml_find_next (n_root, "mime-type", n_type, NULL)))
        {
          FlatpakXml *n_icon = flatpak_xml_find (n_type, "icon", NULL);
          if (!n_icon)
            n_icon = flatpak_xml_find (n_type, "generic-icon", NULL);
          if (n_icon)
            {
              const gchar *icon_name = flatpak_xml_attribute (n_icon, "name");
              if (g_strcmp0 (*current, icon_name) == 0)
                {
                  g_autofree gchar *renamed_icon = g_strdup_printf ("%s.%s", id, icon_name);
                  flatpak_xml_set_attribute (n_icon, "name", renamed_icon);
                }
            }
          else
            {
              g_autofree char* mimetype = g_strdup(flatpak_xml_attribute (n_type, "type"));
              /* Convert the mime type to an icon name. */
              for (char *p = mimetype; *p; p++)
                if (*p == '/')
                  *p = '-';

              if (g_strcmp0(mimetype, *current) == 0)
                {
                  g_autofree gchar *renamed_icon = g_strdup_printf ("%s.%s", id, *current);
                  const gchar *attrs[] = { "name", NULL };
                  const gchar *vals[] = { renamed_icon, NULL };
                  n_icon = flatpak_xml_new_with_attributes ("icon", attrs, vals);
                  flatpak_xml_add (n_type, g_steal_pointer (&n_icon));
                }
            }
        }
    }

  new_contents = g_string_new ("");
  flatpak_xml_to_string (xml_root, new_contents);
  if (!g_file_set_contents (flatpak_file_get_path_cached (mime_file),
                            new_contents->str,
                            new_contents->len,
                            error))
    return FALSE;

  return TRUE;
}

/* Perform `rename-mime-icons` */
static gboolean
_cleanup_rename_mime_icons (BuilderManifest *self, GFile *app_root,
                            GError **error)
{
  g_autoptr(GFile) icons_dir = g_file_resolve_relative_path (app_root, "share/icons");
  g_autoptr(GFile) mime_dir = NULL;
  g_autofree char *mime_basename = NULL;
  g_autoptr(GFile) mime_file = NULL;

  for (char **current = self->rename_mime_icons; *current; current++)
    if (!_rename_mime_icon (self, *current, icons_dir, error))
      return FALSE;

  mime_dir = g_file_resolve_relative_path (app_root, "share/mime/packages");
  mime_basename = g_strdup_printf ("%s.xml", self->id);
  mime_file = g_file_get_child (mime_dir, mime_basename);

  if (!_cleanup_mime_file_rename_icons (self->rename_mime_icons, mime_file,
                                        self->id, error))
    return FALSE;

  return TRUE;
}

gboolean
builder_manifest_cleanup (BuilderManifest *self,
                          BuilderCache    *cache,
                          BuilderContext  *context,
                          GError         **error)
{
  g_autoptr(GFile) app_root = NULL;
  GList *l;
  g_auto(GStrv) env = NULL;
  g_autoptr(GFile) appdata_file = NULL;
  g_autoptr(GFile) appdata_source = NULL;
  int i;

  builder_manifest_checksum_for_cleanup (self, cache, context);
  if (!builder_cache_lookup (cache, "cleanup"))
    {
      g_autoptr(GHashTable) to_remove_ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
      g_autofree char **keys = NULL;
      GFile *app_dir = NULL;
      guint n_keys;

      g_print ("Cleaning up\n");

      if (!builder_context_enable_rofiles (context, error))
        return FALSE;

      /* Call after enabling rofiles */
      app_dir = builder_context_get_app_dir (context);

      if (self->cleanup_commands)
        {
          g_auto(GStrv) build_args = builder_options_get_build_args (self->build_options, context, error);
          if (!build_args)
            return FALSE;
          env = builder_options_get_env (self->build_options, context);
          for (i = 0; self->cleanup_commands[i] != NULL; i++)
            {
              if (!command (app_dir, env, build_args, self->cleanup_commands[i], error))
                return FALSE;
            }
        }

      for (l = self->expanded_modules; l != NULL; l = l->next)
        {
          BuilderModule *m = l->data;

          builder_module_cleanup_collect (m, FALSE, context, to_remove_ht);
        }

      keys = (char **) g_hash_table_get_keys_as_array (to_remove_ht, &n_keys);

      qsort (keys, n_keys, sizeof (char *), cmpstringp);
      /* Iterate in reverse to remove leafs first */
      for (i = n_keys - 1; i >= 0; i--)
        {
          g_autoptr(GError) my_error = NULL;
          g_autoptr(GFile) f = g_file_resolve_relative_path (app_dir, keys[i]);
          g_print ("Removing %s\n", keys[i]);
          if (!g_file_delete (f, NULL, &my_error))
            {
              if (!g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
                  !g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY))
                {
                  g_propagate_error (error, g_steal_pointer (&my_error));
                  return FALSE;
                }
            }
        }

      app_root = g_file_get_child (app_dir, "files");

      appdata_source = builder_manifest_find_appdata_file (self, app_root);
      if (appdata_source)
        {
          g_autoptr(GFile) appdata_dir = g_file_resolve_relative_path (app_root, "share/metainfo");
          g_autofree char *appdata_basename = g_strdup_printf ("%s.metainfo.xml", self->id);

          appdata_file = g_file_get_child (appdata_dir, appdata_basename);

          if (!g_file_equal (appdata_source, appdata_file))
            {
              g_autofree char *src_basename = g_file_get_basename (appdata_source);
              g_print ("Renaming %s to share/metainfo/%s\n", src_basename, appdata_basename);

              if (!flatpak_mkdir_p (appdata_dir, NULL, error))
                return FALSE;
              if (!g_file_move (appdata_source, appdata_file, 0, NULL, NULL, NULL, error))
                return FALSE;
            }

          if (self->appdata_license != NULL && self->appdata_license[0] != 0)
            {
              if (!rewrite_appdata (appdata_file, self->appdata_license, error))
                return FALSE;
            }
        }

      if (self->rename_desktop_file != NULL || self->rename_appdata_file != NULL)
        {
          g_autofree char *desktop_basename = g_strdup_printf ("%s.desktop", self->id);
          if (self->rename_desktop_file != NULL)
            {
              g_autoptr(GFile) applications_dir = g_file_resolve_relative_path (app_root, "share/applications");
              g_autoptr(GFile) src = g_file_get_child (applications_dir, self->rename_desktop_file);
              g_autoptr(GFile) dest = g_file_get_child (applications_dir, desktop_basename);

              g_print ("Renaming %s to %s\n", self->rename_desktop_file, desktop_basename);
              if (!g_file_move (src, dest, 0, NULL, NULL, NULL, error))
                return FALSE;
            }

          if (appdata_file != NULL)
            {
              FlatpakXml *n_id;
              FlatpakXml *n_root;
              FlatpakXml *n_text;
              g_autofree char *old_id = NULL;
              g_autoptr(FlatpakXml) xml_root = NULL;
              g_autoptr(GInputStream) in = NULL;
              g_autoptr(GString) new_contents = NULL;

              in = (GInputStream *) g_file_read (appdata_file, NULL, error);
              if (!in)
                return FALSE;
              xml_root = flatpak_xml_parse (in, FALSE, NULL, error);
              if (!xml_root)
                return FALSE;

              /* replace component/id */
              n_root = flatpak_xml_find (xml_root, "component", NULL);
              if (!n_root)
                n_root = flatpak_xml_find (xml_root, "application", NULL);
              if (!n_root)
                {
                  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "no <component> node");
                  return FALSE;
                }
              n_id = flatpak_xml_find (n_root, "id", NULL);
              if (n_id)
                {
                  n_text = n_id->first_child;
                  if (n_text && g_strcmp0 (n_text->text, self->id) != 0)
                    {
                      old_id = g_steal_pointer (&n_text->text);
                      n_text->text = g_strdup (self->id);
                    }
                }

              if (old_id)
                {
                  FlatpakXml *n_provides = NULL;
                  FlatpakXml *n_provides_id = NULL;
                  FlatpakXml *id_text = NULL;

                  n_provides = flatpak_xml_find (n_root, "provides", NULL);
                  if (!n_provides)
                    {
                      n_provides = flatpak_xml_new ("provides");
                      flatpak_xml_add (n_root, n_provides);
                    }
                  n_provides_id = flatpak_xml_new ("id");
                  id_text = flatpak_xml_new_text (g_steal_pointer (&old_id));
                  flatpak_xml_add (n_provides_id, id_text);
                  flatpak_xml_add (n_provides, n_provides_id);
                }

              /* replace any optional launchable */
              n_id = flatpak_xml_find (n_root, "launchable", NULL);
              if (n_id)
                {
                  n_text = n_id->first_child;
                  if (n_text && g_strcmp0 (n_text->text, self->rename_desktop_file) == 0)
                    {
                      g_free (n_text->text);
                      n_text->text = g_strdup (desktop_basename);
                    }
                }

              new_contents = g_string_new ("");
              flatpak_xml_to_string (xml_root, new_contents);
              if (!g_file_set_contents (flatpak_file_get_path_cached (appdata_file),
                                        new_contents->str,
                                        new_contents->len,
                                        error))
                return FALSE;
            }
        }

      if (self->rename_mime_file != NULL)
        if (!_cleanup_rename_mime_file (self, app_root, error))
          return FALSE;

      if (self->rename_icon)
        {
          gboolean found_icon = FALSE;
          g_autoptr(GFile) icons_dir = g_file_resolve_relative_path (app_root, "share/icons");
          ForeachFile renamer;
          renamer.rename_icon = self->rename_icon;
          renamer.copy_icon = self->copy_icon;
          renamer.id = self->id;
          if (!foreach_file (&renamer, rename_icon_cb, &found_icon, icons_dir, error))
            return FALSE;

          if (!found_icon)
            {
              g_autofree char *icon_path = g_file_get_path (icons_dir);
              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "icon %s not found below %s",
                           self->rename_icon, icon_path);
              return FALSE;
            }
        }

      if (self->rename_mime_icons)
        if (!_cleanup_rename_mime_icons (self, app_root, error))
          return FALSE;

      if (self->rename_icon ||
          self->desktop_file_name_prefix ||
          self->desktop_file_name_suffix ||
          self->rename_desktop_file)
        {
          g_autoptr(GFile) applications_dir = g_file_resolve_relative_path (app_root, "share/applications");
          g_autofree char *desktop_basename = g_strdup_printf ("%s.desktop", self->id);
          g_autoptr(GFile) desktop = g_file_get_child (applications_dir, desktop_basename);
          g_autoptr(GKeyFile) keyfile = g_key_file_new ();
          g_autofree char *desktop_contents = NULL;
          gsize desktop_size;
          g_auto(GStrv) desktop_keys = NULL;

          g_print ("Rewriting contents of %s\n", desktop_basename);
          if (!g_file_load_contents (desktop, NULL,
                                     &desktop_contents, &desktop_size, NULL, error))
            {
              g_autofree char *desktop_path = g_file_get_path (desktop);
              g_prefix_error (error, "Can't load desktop file %s: ", desktop_path);
              return FALSE;
            }

          if (!g_key_file_load_from_data (keyfile,
                                          desktop_contents, desktop_size,
                                          G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                          error))
            return FALSE;

          if (self->rename_desktop_file)
            {
              g_auto(GStrv) old_renames = g_key_file_get_string_list (keyfile,
                                                                      G_KEY_FILE_DESKTOP_GROUP,
                                                                      "X-Flatpak-RenamedFrom",
                                                                      NULL, NULL);
              g_autofree const char **new_renames = NULL;
              int old_rename_len = 0;
              int new_rename_len = 0;

              if (old_renames)
                old_rename_len = g_strv_length (old_renames);

              new_renames = g_new (const char *, old_rename_len + 2);
              for (i = 0; i < old_rename_len; i++)
                new_renames[new_rename_len++] = old_renames[i];
              new_renames[new_rename_len++] = self->rename_desktop_file;
              new_renames[new_rename_len] = NULL;

              g_key_file_set_string_list (keyfile,
                                          G_KEY_FILE_DESKTOP_GROUP,
                                          "X-Flatpak-RenamedFrom",
                                          new_renames, new_rename_len);
            }

          desktop_keys = g_key_file_get_keys (keyfile,
                                              G_KEY_FILE_DESKTOP_GROUP,
                                              NULL, NULL);
          if (self->rename_icon)
            {
              g_autofree char *original_icon_name = g_key_file_get_string (keyfile,
                                                                           G_KEY_FILE_DESKTOP_GROUP,
                                                                           G_KEY_FILE_DESKTOP_KEY_ICON,
                                                                           NULL);

              g_key_file_set_string (keyfile,
                                     G_KEY_FILE_DESKTOP_GROUP,
                                     G_KEY_FILE_DESKTOP_KEY_ICON,
                                     self->id);

              /* Also rename localized version of the Icon= field */
              for (i = 0; desktop_keys[i]; i++)
                {
                  /* Only rename untranslated icon names */
                  if (g_str_has_prefix (desktop_keys[i], "Icon["))
                    {
                      g_autofree char *icon_name = g_key_file_get_string (keyfile,
                                                                          G_KEY_FILE_DESKTOP_GROUP,
                                                                          desktop_keys[i], NULL);

                      if (strcmp (icon_name, original_icon_name) == 0)
                        g_key_file_set_string (keyfile,
                                               G_KEY_FILE_DESKTOP_GROUP,
                                               desktop_keys[i],
                                               self->id);
                    }
                }
            }

          if (self->desktop_file_name_suffix ||
              self->desktop_file_name_prefix)
            {
              for (i = 0; desktop_keys[i]; i++)
                {
                  if (strcmp (desktop_keys[i], "Name") == 0 ||
                      g_str_has_prefix (desktop_keys[i], "Name["))
                    {
                      g_autofree char *name = g_key_file_get_string (keyfile, G_KEY_FILE_DESKTOP_GROUP, desktop_keys[i], NULL);
                      if (name)
                        {
                          g_autofree char *new_name =
                            g_strdup_printf ("%s%s%s",
                                             self->desktop_file_name_prefix ? self->desktop_file_name_prefix : "",
                                             name,
                                             self->desktop_file_name_suffix ? self->desktop_file_name_suffix : "");
                          g_key_file_set_string (keyfile,
                                                 G_KEY_FILE_DESKTOP_GROUP,
                                                 desktop_keys[i],
                                                 new_name);
                        }
                    }
                }
            }

          g_free (desktop_contents);
          desktop_contents = g_key_file_to_data (keyfile, &desktop_size, error);
          if (desktop_contents == NULL)
            return FALSE;

          if (!g_file_set_contents (flatpak_file_get_path_cached (desktop),
                                    desktop_contents, desktop_size, error))
            return FALSE;
        }

      if (self->appstream_compose && appdata_file != NULL)
        {
          g_autofree char *origin = g_strdup_printf ("--origin=%s",
                                                     builder_manifest_get_id (self));
          g_autofree char *components_arg = g_strdup_printf ("--components=%s,%s.desktop",
                                                             self->id, self->id);
          const char *app_root_path = flatpak_file_get_path_cached (app_root);
          g_autofree char *result_root_arg = g_strdup_printf ("--result-root=%s", app_root_path);
          g_autoptr(GFile) xml_dir = flatpak_build_file (app_root, "share/app-info/xmls", NULL);
          g_autoptr(GFile) icon_out = flatpak_build_file (app_root, "share/app-info/icons/flatpak", NULL);
          g_autoptr(GFile) media_dir = flatpak_build_file (app_root, "share/app-info/media", NULL);
          g_autofree char *data_dir = g_strdup_printf ("--data-dir=%s",
                                                       flatpak_file_get_path_cached (xml_dir));
          g_autofree char *icon_dir = g_strdup_printf ("--icons-dir=%s",
                                                       flatpak_file_get_path_cached (icon_out));
          const char *opt_mirror_screenshots_url = builder_context_get_opt_mirror_screenshots_url (context);
          gboolean opt_export_only = builder_context_get_opt_export_only (context);

          if (opt_mirror_screenshots_url && !opt_export_only)
            {
              g_autofree char *url = g_build_filename (opt_mirror_screenshots_url, NULL);
              g_autofree char *arg_base_url = g_strdup_printf ("--media-baseurl=%s", url);
              g_autofree char *arg_media_dir =  g_strdup_printf ("--media-dir=%s",
                                                                 flatpak_file_get_path_cached (media_dir));

              g_print ("Running appstreamcli compose\n");
              g_print ("Saving screenshots in %s\n", flatpak_file_get_path_cached (media_dir));
              if (!appstreamcli_compose (error,
                                         "--prefix=/",
                                         origin,
                                         arg_base_url,
                                         arg_media_dir,
                                         result_root_arg,
                                         data_dir,
                                         icon_dir,
                                         components_arg,
                                         app_root_path,
                                         NULL))
              return FALSE;
            }
          else
            {
              g_print ("Running appstreamcli compose\n");
              if (!appstreamcli_compose (error,
                                         "--prefix=/",
                                         origin,
                                         result_root_arg,
                                         data_dir,
                                         icon_dir,
                                         components_arg,
                                         app_root_path,
                                         NULL))
                return FALSE;
            }
        }

      if (!builder_context_disable_rofiles (context, error))
        return FALSE;

      if (!builder_cache_commit (cache, "Cleanup", error))
        return FALSE;
    }
  else
    {
      g_print ("Cache hit for cleanup, skipping\n");
    }

  return TRUE;
}

static char *
maybe_format_extension_tag (const char *extension_tag)
{
  if (extension_tag != NULL)
    return g_strdup_printf ("tag=%s\n", extension_tag);

  return g_strdup ("");
}

gboolean
builder_manifest_finish (BuilderManifest *self,
                         BuilderCache    *cache,
                         BuilderContext  *context,
                         GError         **error)
{
  g_autoptr(GFile) manifest_file = NULL;
  g_autoptr(GFile) debuginfo_dir = NULL;
  g_autoptr(GFile) sources_dir = NULL;
  g_autoptr(GFile) locale_parent_dir = NULL;
  g_autofree char *json = NULL;
  g_autoptr(GPtrArray) args = NULL;
  g_autoptr(GPtrArray) inherit_extensions = NULL;
  int i;
  GList *l;

  builder_manifest_checksum_for_finish (self, cache, context);
  if (!builder_cache_lookup (cache, "finish"))
    {
      GFile *app_dir = NULL;
      g_autoptr(GPtrArray) sub_ids = g_ptr_array_new_with_free_func (g_free);
      g_autofree char *ref = NULL;
      g_print ("Finishing app\n");

      builder_set_term_title (_("Finishing %s"), self->id);

      if (!builder_context_enable_rofiles (context, error))
        return FALSE;

      /* Call after enabling rofiles */
      app_dir = builder_context_get_app_dir (context);

      ref = flatpak_compose_ref (!self->build_runtime && !self->build_extension,
                                 builder_manifest_get_id (self),
                                 builder_manifest_get_branch (self, context),
                                 builder_context_get_arch (context));

      if (self->metadata)
        {
          GFile *base_dir = builder_context_get_base_dir (context);
          g_autoptr(GFile) dest_metadata = g_file_get_child (app_dir, "metadata");
          g_autoptr(GFile) src_metadata = g_file_resolve_relative_path (base_dir, self->metadata);
          g_autofree char *contents = NULL;
          gsize length;

          if (!g_file_get_contents (flatpak_file_get_path_cached (src_metadata),
                                    &contents, &length, error))
            return FALSE;

          if (!g_file_set_contents (flatpak_file_get_path_cached (dest_metadata),
                                    contents, length, error))
            return FALSE;
        }

      if ((self->inherit_extensions && self->inherit_extensions[0] != NULL) ||
          (self->inherit_sdk_extensions && self->inherit_sdk_extensions[0] != NULL))
        {
          g_autoptr(GFile) metadata = g_file_get_child (app_dir, "metadata");
          g_autoptr(GKeyFile) keyfile = g_key_file_new ();
          g_autoptr(GKeyFile) base_keyfile = g_key_file_new ();
          g_autofree char *arch_option = NULL;
          const char *parent_id = NULL;
          const char *parent_version = NULL;
          g_autofree char *base_metadata = NULL;

          arch_option = g_strdup_printf ("--arch=%s", builder_context_get_arch (context));

          if (self->base != NULL && *self->base != 0)
            {
              parent_id = self->base;
              parent_version = builder_manifest_get_base_version (self);
            }
          else
            {
              parent_id = self->sdk;
              parent_version = builder_manifest_get_runtime_version (self);
            }

          base_metadata = flatpak (NULL, "info", arch_option, "--show-metadata", parent_id, parent_version, NULL);
          if (base_metadata == NULL)
            return flatpak_fail (error, "Inherit extensions specified, but could not get metadata for parent %s version %s", parent_id, parent_version);

          if (!g_key_file_load_from_data (base_keyfile,
                                          base_metadata, -1,
                                          G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS,
                                          error))
            {
              g_prefix_error (error, "Can't load metadata file: ");
              return FALSE;
            }

          if (!g_key_file_load_from_file (keyfile,
                                          flatpak_file_get_path_cached (metadata),
                                          G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                                          error))
            {
              g_prefix_error (error, "Can't load metadata file: ");
              return FALSE;
            }

          inherit_extensions = g_ptr_array_new ();

          for (i = 0; self->inherit_extensions != NULL && self->inherit_extensions[i] != NULL; i++)
            g_ptr_array_add (inherit_extensions, self->inherit_extensions[i]);

          for (i = 0; self->inherit_sdk_extensions != NULL && self->inherit_sdk_extensions[i] != NULL; i++)
            g_ptr_array_add (inherit_extensions, self->inherit_sdk_extensions[i]);

          for (i = 0; i < inherit_extensions->len; i++)
            {
              const char *extension = inherit_extensions->pdata[i];
              g_autofree char *group = g_strconcat (FLATPAK_METADATA_GROUP_PREFIX_EXTENSION,
                                                    extension,
                                                    NULL);
              g_auto(GStrv) keys = NULL;
              int j;

              if (!g_key_file_has_group (base_keyfile, group))
                return flatpak_fail (error, "Can't find inherited extension point %s", extension);

              keys = g_key_file_get_keys (base_keyfile, group, NULL, error);
              if (keys == NULL)
                return FALSE;

              for (j = 0; keys[j] != NULL; j++)
                {
                  g_autofree char *value = g_key_file_get_value (base_keyfile, group, keys[j], error);
                  if (value == NULL)
                    return FALSE;
                  g_key_file_set_value (keyfile, group, keys[j], value);
                }

              if (!g_key_file_has_key (keyfile, group,
                                       FLATPAK_METADATA_KEY_VERSION, NULL) &&
                  !g_key_file_has_key (keyfile, group,
                                       FLATPAK_METADATA_KEY_VERSIONS, NULL))
                g_key_file_set_value (keyfile, group,
                                      FLATPAK_METADATA_KEY_VERSION,
                                      parent_version);
            }

          if (!g_key_file_save_to_file (keyfile,
                                        flatpak_file_get_path_cached (metadata),
                                        error))
            {
              g_prefix_error (error, "Can't save metadata.platform: ");
              return FALSE;
            }
        }

      if (self->command)
        {
          g_autoptr(GFile) files_dir = g_file_resolve_relative_path (app_dir, "files");
          g_autoptr(GFile) command_file = NULL;

          if (!g_path_is_absolute (self->command))
            {
              g_autoptr(GFile) bin_dir = g_file_resolve_relative_path (files_dir, "bin");
              command_file = g_file_get_child (bin_dir, self->command);
            }
          else if (g_str_has_prefix (self->command, "/app/"))
            command_file = g_file_resolve_relative_path (files_dir, self->command + strlen ("/app/"));

          if (command_file != NULL &&
              !g_file_query_exists (command_file, NULL))
            {
              const char *help = "";

              if (strchr (self->command, ' '))
                help = ". Use a shell wrapper for passing arguments";

              g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Command '%s' not found%s", self->command, help);

              return FALSE;
            }
        }

      args = g_ptr_array_new_with_free_func (g_free);
      g_ptr_array_add (args, g_strdup ("flatpak"));
      g_ptr_array_add (args, g_strdup ("build-finish"));
      if (self->command)
        g_ptr_array_add (args, g_strdup_printf ("--command=%s", self->command));

      if (self->finish_args)
        {
          for (i = 0; self->finish_args[i] != NULL; i++)
            g_ptr_array_add (args, g_strdup (self->finish_args[i]));
        }

      for (l = self->add_build_extensions; l != NULL; l = l->next)
        builder_extension_add_remove_args (l->data, args);

      for (l = self->add_extensions; l != NULL; l = l->next)
        builder_extension_add_finish_args (l->data, args);

      for (l = self->expanded_modules; l != NULL; l = l->next)
        {
          BuilderModule *m = l->data;
          builder_module_finish_sources (m, args, context);
        }

      g_ptr_array_add (args, g_file_get_path (app_dir));
      g_ptr_array_add (args, NULL);

      if (!flatpak_spawnv (NULL, NULL, G_SUBPROCESS_FLAGS_NONE, error,
                           (const gchar * const *) args->pdata, NULL))
        return FALSE;

      json = builder_manifest_serialize (self);

      if (self->build_runtime)
        manifest_file = g_file_resolve_relative_path (app_dir, "usr/manifest.json");
      else
        manifest_file = g_file_resolve_relative_path (app_dir, "files/manifest.json");

      if (g_file_query_exists (manifest_file, NULL))
        {
          /* Move existing base manifest aside */
          g_autoptr(GFile) manifest_dir = g_file_get_parent (manifest_file);
          g_autoptr(GFile) old_manifest = NULL;
          int ver = 0;

          do
            {
              g_autofree char *basename = g_strdup_printf ("manifest-base-%d.json", ++ver);
              g_clear_object (&old_manifest);
              old_manifest = g_file_get_child (manifest_dir, basename);
            }
          while (g_file_query_exists (old_manifest, NULL));

          if (!g_file_move (manifest_file, old_manifest, 0,
                            NULL, NULL, NULL, error))
            return FALSE;
        }

      if (!g_file_set_contents (flatpak_file_get_path_cached (manifest_file),
                                json, strlen (json), error))
        return FALSE;

      if (self->build_runtime)
        {
          debuginfo_dir = g_file_resolve_relative_path (app_dir, "usr/lib/debug");
          locale_parent_dir = g_file_resolve_relative_path (app_dir, "usr/" LOCALES_SEPARATE_DIR);
        }
      else
        {
          debuginfo_dir = g_file_resolve_relative_path (app_dir, "files/lib/debug");
          locale_parent_dir = g_file_resolve_relative_path (app_dir, "files/" LOCALES_SEPARATE_DIR);
        }
      sources_dir = g_file_resolve_relative_path (app_dir, "sources");

      if (self->separate_locales && g_file_query_exists (locale_parent_dir, NULL))
        {
          g_autoptr(GFile) metadata_file = NULL;
          g_autofree char *extension_contents = NULL;
          g_autoptr(GFileOutputStream) output = NULL;
          g_autoptr(GFile) metadata_locale_file = NULL;
          g_autofree char *metadata_contents = NULL;
          g_autofree char *locale_id = builder_manifest_get_locale_id (self);

          metadata_file = g_file_get_child (app_dir, "metadata");

          extension_contents = g_strdup_printf ("\n"
                                                "[Extension %s]\n"
                                                "directory=%s\n"
                                                "autodelete=true\n"
                                                "locale-subset=true\n",
                                                locale_id,
                                                LOCALES_SEPARATE_DIR);

          output = g_file_append_to (metadata_file, G_FILE_CREATE_NONE, NULL, error);
          if (output == NULL)
            return FALSE;

          if (!g_output_stream_write_all (G_OUTPUT_STREAM (output),
                                          extension_contents, strlen (extension_contents),
                                          NULL, NULL, error))
            return FALSE;


          metadata_locale_file = g_file_get_child (app_dir, "metadata.locale");
          metadata_contents = g_strdup_printf ("[Runtime]\n"
                                               "name=%s\n"
                                               "\n"
                                               "[ExtensionOf]\n"
                                               "ref=%s\n",
                                               locale_id, ref);
          if (!g_file_set_contents (flatpak_file_get_path_cached (metadata_locale_file),
                                    metadata_contents, strlen (metadata_contents),
                                    error))
            return FALSE;

          g_ptr_array_add (sub_ids, g_strdup (locale_id));
        }

      if (g_file_query_exists (debuginfo_dir, NULL))
        {
          g_autoptr(GFile) metadata_file = NULL;
          g_autoptr(GFile) metadata_debuginfo_file = NULL;
          g_autofree char *metadata_contents = NULL;
          g_autofree char *extension_contents = NULL;
          g_autoptr(GFileOutputStream) output = NULL;
          g_autofree char *debug_id = builder_manifest_get_debug_id (self);

          metadata_file = g_file_get_child (app_dir, "metadata");
          metadata_debuginfo_file = g_file_get_child (app_dir, "metadata.debuginfo");

          extension_contents = g_strdup_printf ("\n"
                                                "[Extension %s]\n"
                                                "directory=lib/debug\n"
                                                "autodelete=true\n"
                                                "no-autodownload=true\n",
                                                debug_id);

          output = g_file_append_to (metadata_file, G_FILE_CREATE_NONE, NULL, error);
          if (output == NULL)
            return FALSE;

          if (!g_output_stream_write_all (G_OUTPUT_STREAM (output), extension_contents, strlen (extension_contents),
                                          NULL, NULL, error))
            return FALSE;

          metadata_contents = g_strdup_printf ("[Runtime]\n"
                                               "name=%s\n"
                                               "\n"
                                               "[ExtensionOf]\n"
                                               "ref=%s\n",
                                               debug_id, ref);
          if (!g_file_set_contents (flatpak_file_get_path_cached (metadata_debuginfo_file),
                                    metadata_contents, strlen (metadata_contents), error))
            return FALSE;

          g_ptr_array_add (sub_ids, g_strdup (debug_id));
        }

      for (l = self->add_extensions; l != NULL; l = l->next)
        {
          BuilderExtension *e = l->data;
          g_autofree char *extension_metadata_name = NULL;
          g_autoptr(GFile) metadata_extension_file = NULL;
          g_autofree char *metadata_contents = NULL;
          g_autofree char *extension_tag_opt = NULL;

          if (!builder_extension_is_bundled (e))
            continue;

          extension_tag_opt = maybe_format_extension_tag (builder_manifest_get_extension_tag (self));
          extension_metadata_name = g_strdup_printf ("metadata.%s", builder_extension_get_name (e));
          metadata_extension_file = g_file_get_child (app_dir, extension_metadata_name);
          metadata_contents = g_strdup_printf ("[Runtime]\n"
                                               "name=%s\n"
                                               "\n"
                                               "[ExtensionOf]\n"
                                               "ref=%s\n"
                                               "%s",
                                               builder_extension_get_name (e),
                                               ref,
                                               extension_tag_opt);
          if (!g_file_set_contents (flatpak_file_get_path_cached (metadata_extension_file),
                                    metadata_contents, strlen (metadata_contents), error))
            return FALSE;

          g_ptr_array_add (sub_ids, g_strdup (builder_extension_get_name (e)));
        }

      if (sub_ids->len > 0)
        {
          g_autoptr(GFile) metadata_file = NULL;
          g_autoptr(GFileOutputStream) output = NULL;
          g_autoptr(GString) extension_contents = g_string_new ("\n"
                                                                "[Build]\n");

          g_string_append (extension_contents, FLATPAK_METADATA_KEY_BUILD_EXTENSIONS"=");
          for (i = 0; i < sub_ids->len; i++)
            {
              g_string_append (extension_contents, (const char *)sub_ids->pdata[i]);
              g_string_append (extension_contents, ";");
            }

          metadata_file = g_file_get_child (app_dir, "metadata");
          output = g_file_append_to (metadata_file, G_FILE_CREATE_NONE, NULL, error);
          if (output == NULL)
            return FALSE;

          if (!g_output_stream_write_all (G_OUTPUT_STREAM (output),
                                          extension_contents->str, extension_contents->len,
                                          NULL, NULL, error))
            return FALSE;
        }

      if (!builder_context_disable_rofiles (context, error))
        return FALSE;

      if (!builder_cache_commit (cache, "Finish", error))
        return FALSE;
    }
  else
    {
      g_print ("Cache hit for finish, skipping\n");
    }

  return TRUE;
}

/* Creates the platform directory based on the base platform (with
 * locales moved in place if needed), and the metadata.platform file
 * for it */
static gboolean
builder_manifest_create_platform_base (BuilderManifest *self,
                                       BuilderCache    *cache,
                                       BuilderContext  *context,
                                       GError         **error)
{
  builder_manifest_checksum_for_platform_base (self, cache, context);
  if (!builder_cache_lookup (cache, "platform-base"))
    {
      GFile *app_dir = NULL;
      g_autoptr(GFile) platform_dir = NULL;
      g_autoptr(GPtrArray) args = NULL;
      int i;

      g_print ("Creating platform based on %s\n", self->runtime);
      builder_set_term_title (_("Creating platform base for %s"), self->id);

      if (!builder_context_enable_rofiles (context, error))
        return FALSE;

      /* Call after enabling rofiles */
      app_dir = builder_context_get_app_dir (context);
      platform_dir = g_file_get_child (app_dir, "platform");

      args = g_ptr_array_new_with_free_func (g_free);

      g_ptr_array_add (args, g_strdup ("flatpak"));
      g_ptr_array_add (args, g_strdup ("build-init"));
      g_ptr_array_add (args, g_strdup ("--update"));
      g_ptr_array_add (args, g_strdup ("--writable-sdk"));
      g_ptr_array_add (args, g_strdup ("--sdk-dir=platform"));
      g_ptr_array_add (args, g_strdup_printf ("--arch=%s", builder_context_get_arch (context)));

      for (i = 0; self->platform_extensions != NULL && self->platform_extensions[i] != NULL; i++)
        {
          const char *ext = self->platform_extensions[i];
          g_ptr_array_add (args, g_strdup_printf ("--sdk-extension=%s", ext));
        }

      g_ptr_array_add (args, g_file_get_path (app_dir));
      g_ptr_array_add (args, g_strdup (self->id));
      g_ptr_array_add (args, g_strdup (self->runtime));
      g_ptr_array_add (args, g_strdup (self->runtime));
      g_ptr_array_add (args, g_strdup (builder_manifest_get_runtime_version (self)));

      g_ptr_array_add (args, NULL);

      if (!flatpak_spawnv (NULL, NULL, G_SUBPROCESS_FLAGS_NONE, error,
                           (const gchar * const *) args->pdata, NULL))
        return FALSE;

      if (self->separate_locales)
        {
          if (!builder_migrate_locale_dirs (platform_dir, error))
            return FALSE;
        }

      if (self->metadata_platform)
        {
          GFile *base_dir = builder_context_get_base_dir (context);
          g_autoptr(GFile) dest_metadata = g_file_get_child (app_dir, "metadata.platform");
          g_autoptr(GFile) src_metadata = g_file_resolve_relative_path (base_dir, self->metadata_platform);
          g_autofree char *contents = NULL;
          gsize length;

          if (!g_file_get_contents (flatpak_file_get_path_cached (src_metadata),
                                    &contents, &length, error))
            return FALSE;

          if (!g_file_set_contents (flatpak_file_get_path_cached (dest_metadata),
                                    contents, length, error))
            return FALSE;
        }
      else
        {
          g_autoptr(GFile) metadata = g_file_get_child (app_dir, "metadata");
          g_autoptr(GFile) dest_metadata = g_file_get_child (app_dir, "metadata.platform");
          g_autoptr(GKeyFile) keyfile = g_key_file_new ();
          g_auto(GStrv) groups = NULL;
          int j;

          if (!g_key_file_load_from_file (keyfile,
                                          flatpak_file_get_path_cached (metadata),
                                          G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                                          error))
            {
              g_prefix_error (error, "Can't load metadata file: ");
              return FALSE;
            }

          g_key_file_set_string (keyfile, FLATPAK_METADATA_GROUP_RUNTIME,
                                 FLATPAK_METADATA_KEY_NAME, self->id_platform);

          groups = g_key_file_get_groups (keyfile, NULL);
          for (j = 0; groups[j] != NULL; j++)
            {
              const char *ext;

              if (!g_str_has_prefix (groups[j], FLATPAK_METADATA_GROUP_PREFIX_EXTENSION))
                continue;

              ext = groups[j] + strlen (FLATPAK_METADATA_GROUP_PREFIX_EXTENSION);

              if (g_str_has_prefix (ext, self->id) ||
                  (self->inherit_sdk_extensions &&
                   g_strv_contains ((const char * const *)self->inherit_sdk_extensions, ext)))
                {
                  g_key_file_remove_group (keyfile, groups[j], NULL);
                }
            }

          if (!g_key_file_save_to_file (keyfile,
                                        flatpak_file_get_path_cached (dest_metadata),
                                        error))
            {
              g_prefix_error (error, "Can't save metadata.platform: ");
              return FALSE;
            }
        }

      if (!builder_context_disable_rofiles (context, error))
        return FALSE;

      if (!builder_cache_commit (cache, "Created platform base", error))
        return FALSE;
    }
  else
    {
      g_print ("Cache hit for create platform base, skipping\n");
    }

  return TRUE;
}

/* Run the prepare-platform commands, then layer on top all the
 * changes from the sdk build, except any new files mentioned by
 * cleanup-platform */
static gboolean
builder_manifest_prepare_platform (BuilderManifest *self,
                                   BuilderCache    *cache,
                                   BuilderContext  *context,
                                   GError         **error)
{
  builder_manifest_checksum_for_platform_prepare (self, cache, context);
  if (!builder_cache_lookup (cache, "platform-prepare"))
    {
      GFile *app_dir = NULL;
      g_autoptr(GFile) platform_dir = NULL;
      g_autoptr(GHashTable) to_remove_ht = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
      g_autoptr(GPtrArray) changes = NULL;
      GList *l;
      int i;

      g_print ("Preparing platform with new changes\n");
      builder_set_term_title (_("Preparing platform for %s"), self->id);

      if (!builder_context_enable_rofiles (context, error))
        return FALSE;

      /* Call after enabling rofiles */
      app_dir = builder_context_get_app_dir (context);
      platform_dir = g_file_get_child (app_dir, "platform");

      if (self->prepare_platform_commands)
        {
          g_auto(GStrv) env = builder_options_get_env (self->build_options, context);
          g_auto(GStrv) build_args = builder_options_get_build_args (self->build_options, context, error);
          if (!build_args)
            return FALSE;
          char *platform_args[] = { "--sdk-dir=platform", "--metadata=metadata.platform", NULL };
          g_auto(GStrv) extra_args = strcatv (build_args, platform_args);

          for (i = 0; self->prepare_platform_commands[i] != NULL; i++)
            {
              if (!command (app_dir, env, extra_args, self->prepare_platform_commands[i], error))
                return FALSE;
            }
        }

      for (l = self->expanded_modules; l != NULL; l = l->next)
        {
          BuilderModule *m = l->data;

          builder_module_cleanup_collect (m, TRUE, context, to_remove_ht);
        }

      /* This returns both additions and removals */
      changes = builder_cache_get_all_changes (cache, error);
      if (changes == NULL)
        return FALSE;

      for (i = 0; i < changes->len; i++)
        {
          const char *changed = g_ptr_array_index (changes, i);
          g_autoptr(GFile) src = NULL;
          g_autoptr(GFile) dest = NULL;
          g_autoptr(GFileInfo) info = NULL;
          g_autoptr(GError) my_error = NULL;

          if (!g_str_has_prefix (changed, "usr/"))
            continue;

          if (g_str_has_prefix (changed, "usr/lib/debug/") &&
              !g_str_equal (changed, "usr/lib/debug/app"))
            continue;

          src = g_file_resolve_relative_path (app_dir, changed);
          dest = g_file_resolve_relative_path (platform_dir, changed + strlen ("usr/"));

          info = g_file_query_info (src, "standard::type,standard::symlink-target",
                                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                    NULL, &my_error);
          if (info == NULL &&
              !g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            {
              g_propagate_error (error, g_steal_pointer (&my_error));
              return FALSE;
            }
          g_clear_error (&my_error);

          if (info == NULL)
            {
              /* File was removed from sdk, remove from platform also if it exists there */

              if (!g_file_delete (dest, NULL, &my_error) &&
                  !g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                {
                  g_propagate_error (error, g_steal_pointer (&my_error));
                  return FALSE;
                }

              continue;
            }

          if (g_hash_table_contains (to_remove_ht, changed))
            {
              g_print ("Ignoring %s\n", changed);
              continue;
            }

          if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
            {
              if (!flatpak_mkdir_p (dest, NULL, error))
                return FALSE;
            }
          else
            {
              g_autoptr(GFile) dest_parent = g_file_get_parent (dest);

              if (!flatpak_mkdir_p (dest_parent, NULL, error))
                return FALSE;

              if (!g_file_delete (dest, NULL, &my_error) &&
                  !g_error_matches (my_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
                {
                  g_propagate_error (error, g_steal_pointer (&my_error));
                  return FALSE;
                }
              g_clear_error (&my_error);

              if (g_file_info_get_file_type (info) == G_FILE_TYPE_SYMBOLIC_LINK)
                {
                  if (!g_file_make_symbolic_link (dest,
                                                  g_file_info_get_symlink_target (info),
                                                  NULL, error))
                    return FALSE;
                }
              else
                {
                  g_autofree char *src_path = g_file_get_path (src);
                  g_autofree char *dest_path = g_file_get_path (dest);

                  if (link (src_path, dest_path))
                    {
                      glnx_set_error_from_errno (error);
                      return FALSE;
                    }
                }
            }
        }

      if (!builder_context_disable_rofiles (context, error))
        return FALSE;

      if (!builder_cache_commit (cache, "Prepared platform", error))
        return FALSE;
    }
  else
    {
      g_print ("Cache hit for prepare platform, skipping\n");
    }

  return TRUE;
}

/* Run the cleanup-platform-commands (which we want to run in a new cache state to ensure it gets freshly zeroed mtimes)
 * and other finishing touches that need to happen after that, such as adding separate locale metadata. */
static gboolean
builder_manifest_finish_platform (BuilderManifest *self,
                                  BuilderCache    *cache,
                                  BuilderContext  *context,
                                  GError         **error)
{
  builder_manifest_checksum_for_platform_finish (self, cache, context);
  if (!builder_cache_lookup (cache, "platform-finish"))
    {
      GFile *app_dir = NULL;
      g_autoptr(GFile) platform_dir = NULL;
      g_autoptr(GFile) locale_dir = NULL;
      g_autofree char *ref = NULL;
      g_autoptr(GPtrArray) sub_ids = g_ptr_array_new_with_free_func (g_free);
      int i;

      g_print ("Finishing platform\n");
      builder_set_term_title (_("Finishing up platform for %s"), self->id);

      if (!builder_context_enable_rofiles (context, error))
        return FALSE;

      /* Call after enabling rofiles */
      app_dir = builder_context_get_app_dir (context);
      platform_dir = g_file_get_child (app_dir, "platform");
      locale_dir = g_file_resolve_relative_path (platform_dir, LOCALES_SEPARATE_DIR);

      ref = flatpak_compose_ref (!self->build_runtime && !self->build_extension,
                                 builder_manifest_get_id_platform (self),
                                 builder_manifest_get_branch (self, context),
                                 builder_context_get_arch (context));

      if (self->cleanup_platform_commands)
        {
          g_auto(GStrv) env = builder_options_get_env (self->build_options, context);
          g_auto(GStrv) build_args = builder_options_get_build_args (self->build_options, context, error);
          if (!build_args)
            return FALSE;
          char *platform_args[] = { "--sdk-dir=platform", "--metadata=metadata.platform", NULL };
          g_auto(GStrv) extra_args = strcatv (build_args, platform_args);

          for (i = 0; self->cleanup_platform_commands[i] != NULL; i++)
            {
              if (!command (app_dir, env, extra_args, self->cleanup_platform_commands[i], error))
                return FALSE;
            }
        }

      if (self->separate_locales && g_file_query_exists (locale_dir, NULL))
        {
          g_autoptr(GFile) metadata_file = NULL;
          g_autofree char *extension_contents = NULL;
          g_autoptr(GFileOutputStream) output = NULL;
          g_autoptr(GFile) metadata_locale_file = NULL;
          g_autofree char *metadata_contents = NULL;
          g_autofree char *locale_id = builder_manifest_get_locale_id_platform (self);

          metadata_file = g_file_get_child (app_dir, "metadata.platform");

          extension_contents = g_strdup_printf ("\n"
                                                "[Extension %s]\n"
                                                "directory=%s\n"
                                                "autodelete=true\n"
                                                "locale-subset=true\n",
                                                locale_id,
                                                LOCALES_SEPARATE_DIR);

          if (!flatpak_break_hardlink (metadata_file, error))
            return FALSE;
          output = g_file_append_to (metadata_file, G_FILE_CREATE_NONE, NULL, error);
          if (output == NULL)
            return FALSE;

          if (!g_output_stream_write_all (G_OUTPUT_STREAM (output),
                                          extension_contents, strlen (extension_contents),
                                          NULL, NULL, error))
            return FALSE;


          metadata_locale_file = g_file_get_child (app_dir, "metadata.platform.locale");
          metadata_contents = g_strdup_printf ("[Runtime]\n"
                                               "name=%s\n"
                                               "\n"
                                               "[ExtensionOf]\n"
                                               "ref=%s\n", locale_id, ref);
          if (!g_file_set_contents (flatpak_file_get_path_cached (metadata_locale_file),
                                    metadata_contents, strlen (metadata_contents),
                                    error))
            return FALSE;

          g_ptr_array_add (sub_ids, g_strdup (locale_id));
        }

      if (sub_ids->len > 0)
        {
          g_autoptr(GFile) metadata_file = NULL;
          g_autoptr(GFileOutputStream) output = NULL;
          g_autoptr(GString) extension_contents = g_string_new ("\n"
                                                                "[Build]\n");

          g_string_append (extension_contents, FLATPAK_METADATA_KEY_BUILD_EXTENSIONS"=");
          for (i = 0; i < sub_ids->len; i++)
            {
              g_string_append (extension_contents, (const char *)sub_ids->pdata[i]);
              g_string_append (extension_contents, ";");
            }
          metadata_file = g_file_get_child (app_dir, "metadata.platform");
          if (!flatpak_break_hardlink (metadata_file, error))
            return FALSE;
          output = g_file_append_to (metadata_file, G_FILE_CREATE_NONE, NULL, error);
          if (output == NULL)
            return FALSE;

          if (!g_output_stream_write_all (G_OUTPUT_STREAM (output),
                                          extension_contents->str, extension_contents->len,
                                          NULL, NULL, error))
            return FALSE;
        }

      if (!builder_context_disable_rofiles (context, error))
        return FALSE;

      if (!builder_cache_commit (cache, "Platform finish", error))
        return FALSE;
    }
  else
    {
      g_print ("Cache hit for platform finish, skipping\n");
    }

  return TRUE;
}


gboolean
builder_manifest_create_platform (BuilderManifest *self,
                                  BuilderCache    *cache,
                                  BuilderContext  *context,
                                  GError         **error)
{
  if (!self->build_runtime ||
      self->id_platform == NULL)
    return TRUE;

  if (!builder_manifest_create_platform_base (self, cache, context, error))
    return FALSE;

  if (!builder_manifest_prepare_platform (self, cache, context, error))
    return FALSE;

  if (!builder_manifest_finish_platform (self, cache, context, error))
    return FALSE;

  return TRUE;
}

gboolean
builder_manifest_bundle_sources (BuilderManifest *self,
                                 const char      *json,
                                 BuilderCache    *cache,
                                 BuilderContext  *context,
                                 GError         **error)
{

  builder_manifest_checksum_for_bundle_sources (self, cache, context);
  if (!builder_cache_lookup (cache, "bundle-sources"))
    {
      g_autofree char *sources_id = builder_manifest_get_sources_id (self);
      GFile *app_dir;
      g_autoptr(GFile) metadata_sources_file = NULL;
      g_autoptr(GFile) metadata = NULL;
      g_autoptr(GFile) json_dir = NULL;
      g_autofree char *manifest_filename = NULL;
      g_autoptr(GFile) manifest_file = NULL;
      g_autofree char *metadata_contents = NULL;
      g_autoptr(GKeyFile) metadata_keyfile = g_key_file_new ();
      g_autoptr(GPtrArray) subs = g_ptr_array_new ();
      g_auto(GStrv) old_subs = NULL;
      gsize i;
      GList *l;

      g_print ("Bundling sources\n");

      builder_set_term_title (_("Bundling sources for %s"), self->id);

      if (!builder_context_enable_rofiles (context, error))
        return FALSE;

      app_dir = builder_context_get_app_dir (context);
      metadata_sources_file = g_file_get_child (app_dir, "metadata.sources");
      metadata_contents = g_strdup_printf ("[Runtime]\n"
                                           "name=%s\n", sources_id);
      if (!g_file_set_contents (flatpak_file_get_path_cached (metadata_sources_file),
                                metadata_contents, strlen (metadata_contents), error))
        return FALSE;

      json_dir = g_file_resolve_relative_path (app_dir, "sources/manifest");
      if (!flatpak_mkdir_p (json_dir, NULL, error))
        return FALSE;

      manifest_filename = g_strconcat (self->id, ".json", NULL);
      manifest_file = g_file_get_child (json_dir, manifest_filename);
      if (!g_file_set_contents (flatpak_file_get_path_cached (manifest_file),
                                json, strlen (json), error))
        return FALSE;


      for (l = self->expanded_modules; l != NULL; l = l->next)
        {
          BuilderModule *m = l->data;

          if (!builder_module_bundle_sources (m, context, error))
            return FALSE;
        }


      metadata = g_file_get_child (app_dir, "metadata");
      if (!g_key_file_load_from_file (metadata_keyfile,
                                      flatpak_file_get_path_cached (metadata),
                                      G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,
                                      error))
        {
          g_prefix_error (error, "Can't load main metadata file: ");
          return FALSE;
        }

      old_subs = g_key_file_get_string_list (metadata_keyfile, "Build", "built-extensions", NULL, NULL);
      for (i = 0; old_subs != NULL && old_subs[i] != NULL; i++)
        g_ptr_array_add (subs, old_subs[i]);
      g_ptr_array_add (subs, sources_id);

      g_key_file_set_string_list (metadata_keyfile, FLATPAK_METADATA_GROUP_BUILD,
                                  FLATPAK_METADATA_KEY_BUILD_EXTENSIONS,
                                  (const char * const *)subs->pdata, subs->len);

      if (!g_key_file_save_to_file (metadata_keyfile,
                                    flatpak_file_get_path_cached (metadata),
                                    error))
        {
          g_prefix_error (error, "Can't save metadata.platform: ");
          return FALSE;
        }

      if (!builder_context_disable_rofiles (context, error))
        return FALSE;

      if (!builder_cache_commit (cache, "Bundled sources", error))
        return FALSE;
    }
  else
    {
      g_print ("Cache hit for bundle-sources, skipping\n");
    }

  return TRUE;
}

gboolean
builder_manifest_show_deps (BuilderManifest *self,
                            BuilderContext  *context,
                            GError         **error)
{
  g_autoptr(GHashTable) names = g_hash_table_new (g_str_hash, g_str_equal);
  GList *l;

  if (!expand_modules (context, self->modules, &self->expanded_modules, names, error))
    return FALSE;

  for (l = self->expanded_modules; l != NULL; l = l->next)
    {
      BuilderModule *module = l->data;

      if (!builder_module_show_deps (module, context, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
builder_manifest_install_single_dep (const char *ref,
				     const char *remote,
				     gboolean opt_user,
				     const char *opt_installation,
				     GError **error)
{
  g_autoptr(GPtrArray) args = NULL;
  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("flatpak"));
  add_installation_args (args, opt_user, opt_installation);

  g_ptr_array_add (args, g_strdup ("install"));

  g_ptr_array_add (args, g_strdup ("-y"));
  if (flatpak_version_check (1, 2, 0))
    g_ptr_array_add (args, g_strdup ("--noninteractive"));

  g_ptr_array_add (args, g_strdup (remote));
  g_ptr_array_add (args, g_strdup (ref));
  g_ptr_array_add (args, NULL);

  if (!builder_maybe_host_spawnv (NULL, NULL, 0, error, (const char * const *)args->pdata, NULL))
    {
      g_autofree char *commandline = flatpak_quote_argv ((const char **)args->pdata);
      g_prefix_error (error, "running `%s`: ", commandline);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}


static gboolean
builder_manifest_update_single_dep (const char *ref,
				     gboolean opt_user,
				     const char *opt_installation,
				     GError **error)
{
  g_autoptr(GPtrArray) args = NULL;
  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("flatpak"));
  add_installation_args (args, opt_user, opt_installation);

  g_ptr_array_add (args, g_strdup ("update"));
  g_ptr_array_add (args, g_strdup ("--subpath="));

  g_ptr_array_add (args, g_strdup ("-y"));
  if (flatpak_version_check (1, 2, 0))
    g_ptr_array_add (args, g_strdup ("--noninteractive"));

  g_ptr_array_add (args, g_strdup (ref));
  g_ptr_array_add (args, NULL);

  if (!builder_maybe_host_spawnv (NULL, NULL, 0, error, (const char * const *)args->pdata, NULL))
    {
      g_autofree char *commandline = flatpak_quote_argv ((const char **)args->pdata);
      g_prefix_error (error, "running `%s`: ", commandline);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

static gboolean
builder_manifest_install_dep (BuilderManifest *self,
                              BuilderContext  *context,
                              char *const *remotes,
                              gboolean opt_user,
                              const char *opt_installation,
                              const char *runtime,
                              const char *version,
                              gboolean opt_yes,
                              GError         **error)
{
  g_autofree char *ref = NULL;
  g_autofree char *commit = NULL;
  g_autoptr(GError) first_error = NULL;

  if (version == NULL)
    version = builder_manifest_get_runtime_version (self);

  ref = flatpak_build_untyped_ref (runtime,
                                   version,
                                   builder_context_get_arch (context));

  commit = flatpak_info (opt_user, opt_installation, "--show-commit", ref, NULL);

  if (commit != NULL)
    {
      g_print("Updating %s\n", ref);
      if (builder_manifest_update_single_dep(ref, opt_user, opt_installation,
                                             error))
      {
        return TRUE;
      }
    }
  else
    {
      gboolean multiple_remotes = (*(remotes+1) != NULL);
      for (const char *remote = *remotes; remote != NULL; remote = *(++remotes))
	{
	  g_autoptr(GError) current_error = NULL;
	  if (multiple_remotes)
	    {
            g_print("Trying to install %s from %s\n", ref, remote);
	    }
	  else {
	    g_print("Installing %s from %s\n", ref, remote);
	  }
	  if (builder_manifest_install_single_dep (ref, remote, opt_user, opt_installation,
                                                   &current_error))
	  {
	    return TRUE;
	  }
	  else {
	    gboolean fatal_error = current_error->domain != G_SPAWN_EXIT_ERROR;
	    if (first_error == NULL)
	      {
	        first_error = g_steal_pointer(&current_error);
	      }
	    if (fatal_error)
	      {
		break;
	      }
	  }
	}
    }
  *error = g_steal_pointer(&first_error);
  return FALSE;
}

static gboolean
builder_manifest_install_extension_deps (BuilderManifest *self,
                                         BuilderContext  *context,
                                         const char *runtime,
                                         const char *runtime_version,
                                         char **runtime_extensions,
                                         char * const *remotes,
                                         gboolean opt_user,
                                         const char *opt_installation,
                                         gboolean opt_yes,
                                         GError **error)
{
  g_autofree char *runtime_ref = flatpak_build_runtime_ref (runtime, runtime_version,
                                                            builder_context_get_arch (context));
  g_autofree char *metadata = NULL;
  g_autoptr(GKeyFile) keyfile = g_key_file_new ();
  int i;

  if (runtime_extensions == NULL)
    return TRUE;

  metadata = flatpak_info (opt_user, opt_installation, "--show-metadata", runtime_ref, NULL);
  if (metadata == NULL)
    return FALSE;

  if (!g_key_file_load_from_data (keyfile,
                                  metadata, -1,
                                  0,
                                  error))
    return FALSE;

  for (i = 0; runtime_extensions != NULL && runtime_extensions[i] != NULL; i++)
    {
      g_autofree char *extension_group = g_strdup_printf ("Extension %s", runtime_extensions[i]);
      g_autofree char *extension_version = NULL;

      if (!g_key_file_has_group (keyfile, extension_group))
        {
          g_autofree char *base = g_strdup (runtime_extensions[i]);
          char *dot = strrchr (base, '.');
          if (dot != NULL)
            *dot = 0;

          g_free (extension_group);
          extension_group = g_strdup_printf ("Extension %s", base);
          if (!g_key_file_has_group (keyfile, extension_group))
            return flatpak_fail (error, "Unknown extension '%s' in runtime\n", runtime_extensions[i]);
        }

      extension_version = g_key_file_get_string (keyfile, extension_group, "version", NULL);
      if (extension_version == NULL)
        extension_version = g_strdup (runtime_version);

      g_print ("Dependency Extension: %s %s\n", runtime_extensions[i], extension_version);
      if (!builder_manifest_install_dep (self, context, remotes, opt_user, opt_installation,
                                         runtime_extensions[i], extension_version,
                                         opt_yes,
                                         error))
        return FALSE;
    }

  return TRUE;
}

gboolean
builder_manifest_install_deps (BuilderManifest *self,
                               BuilderContext  *context,
                               char * const *remotes,
                               gboolean opt_user,
                               const char *opt_installation,
                               gboolean opt_yes,
                               GError **error)
{
  GList *l;

  const char *sdk = NULL;
  const char *sdk_branch = NULL;
  g_auto(GStrv) sdk_parts = g_strsplit (self->sdk, "/", 3);

  if (g_strv_length (sdk_parts) >= 3)
    {
      sdk = sdk_parts[0];
      sdk_branch = sdk_parts[2];
    }
  else
    {
      sdk = self->sdk;
      sdk_branch = builder_manifest_get_runtime_version (self);
    }

  /* Sdk */
  g_print ("Dependency Sdk: %s %s\n", sdk, sdk_branch);
  if (!builder_manifest_install_dep (self, context, remotes, opt_user, opt_installation,
                                     sdk, sdk_branch,
                                     opt_yes,
                                     error))
    return FALSE;

  /* Runtime */
  g_print ("Dependency Runtime: %s %s\n", self->runtime, builder_manifest_get_runtime_version (self));
  if (!builder_manifest_install_dep (self, context, remotes, opt_user, opt_installation,
                                     self->runtime, builder_manifest_get_runtime_version (self),
                                     opt_yes,
                                     error))
    return FALSE;

  if (self->base)
    {
      g_print ("Dependency Base: %s %s\n", self->base, builder_manifest_get_base_version (self));
      if (!builder_manifest_install_dep (self, context, remotes, opt_user, opt_installation,
                                         self->base, builder_manifest_get_base_version (self),
                                         opt_yes,
                                         error))
        return FALSE;
    }

  if (!builder_manifest_install_extension_deps (self, context,
                                                sdk, sdk_branch, self->sdk_extensions,
                                                remotes, opt_user, opt_installation,
                                                opt_yes,
                                                error))
    return FALSE;

  if (!builder_manifest_install_extension_deps (self, context,
                                                self->runtime, builder_manifest_get_runtime_version (self),
                                                self->platform_extensions,
                                                remotes, opt_user, opt_installation,
                                                opt_yes,
                                                error))
    return FALSE;

  for (l = self->add_build_extensions; l != NULL; l = l->next)
    {
      BuilderExtension *extension = l->data;
      const char *name = builder_extension_get_name (extension);
      const char *version = builder_extension_get_version (extension);

      if (name == NULL || version == NULL)
        continue;

      g_print ("Dependency Extension: %s %s\n", name, version);
      if (!builder_manifest_install_dep (self, context, remotes, opt_user, opt_installation,
                                         name, version,
                                         opt_yes,
                                         error))
        return FALSE;
    }

  return TRUE;
}

gboolean
builder_manifest_run (BuilderManifest *self,
                      BuilderContext  *context,
                      FlatpakContext  *arg_context,
                      char           **argv,
                      int              argc,
                      gboolean         log_session_bus,
                      gboolean         log_system_bus,
                      GError         **error)
{
  g_autoptr(GPtrArray) args = NULL;
  g_autofree char *commandline = NULL;
  g_autofree char *build_dir_path = NULL;
  g_autofree char *ccache_dir_path = NULL;
  g_auto(GStrv) env = NULL;
  g_auto(GStrv) build_args = NULL;
  int i;

  if (!builder_context_enable_rofiles (context, error))
    return FALSE;

  if (!flatpak_mkdir_p (builder_context_get_build_dir (context),
                        NULL, error))
    return FALSE;

  args = g_ptr_array_new_with_free_func (g_free);
  g_ptr_array_add (args, g_strdup ("flatpak"));
  g_ptr_array_add (args, g_strdup ("build"));
  g_ptr_array_add (args, g_strdup ("--with-appdir"));

  build_dir_path = g_file_get_path (builder_context_get_build_dir (context));
  /* We're not sure what we're building here, so lets set both the /run/build and /run/build-runtime dirs to the build dirs */
  g_ptr_array_add (args, g_strdup_printf ("--bind-mount=/run/build=%s", build_dir_path));
  g_ptr_array_add (args, g_strdup_printf ("--bind-mount=/run/build-runtime=%s", build_dir_path));

  if (g_file_query_exists (builder_context_get_ccache_dir (context), NULL))
    {
      ccache_dir_path = g_file_get_path (builder_context_get_ccache_dir (context));
      g_ptr_array_add (args, g_strdup_printf ("--bind-mount=/run/ccache=%s", ccache_dir_path));
    }

  build_args = builder_options_get_build_args (self->build_options, context, error);
  if (build_args == NULL)
    return FALSE;

  for (i = 0; build_args[i] != NULL; i++)
    g_ptr_array_add (args, g_strdup (build_args[i]));

  env = builder_options_get_env (self->build_options, context);
  if (env)
    {
      for (i = 0; env[i] != NULL; i++)
        g_ptr_array_add (args, g_strdup_printf ("--env=%s", env[i]));
    }

  /* Just add something so that we get the default rules (own our own id) */
  g_ptr_array_add (args, g_strdup ("--talk-name=org.freedesktop.DBus"));

  if (log_session_bus)
    g_ptr_array_add (args, g_strdup ("--log-session-bus"));

  if (log_system_bus)
    g_ptr_array_add (args, g_strdup ("--log-system-bus"));

  /* Inherit all finish args except --filesystem and some that
   * build doesn't understand so the command gets the same access
   * as the final app
   */
  if (self->finish_args)
    {
      for (i = 0; self->finish_args[i] != NULL; i++)
        {
          const char *arg = self->finish_args[i];
          if (!g_str_has_prefix (arg, "--filesystem") &&
              !g_str_has_prefix (arg, "--extension") &&
              !g_str_has_prefix (arg, "--sdk") &&
              !g_str_has_prefix (arg, "--runtime") &&
              !g_str_has_prefix (arg, "--command") &&
              !g_str_has_prefix (arg, "--extra-data") &&
              !g_str_has_prefix (arg, "--require-version") &&
              !g_str_has_prefix (arg, "--metadata"))
            g_ptr_array_add (args, g_strdup (arg));
        }
    }

  flatpak_context_to_args (arg_context, args);

  g_ptr_array_add (args, g_file_get_path (builder_context_get_app_dir (context)));

  for (i = 0; i < argc; i++)
    g_ptr_array_add (args, g_strdup (argv[i]));
  g_ptr_array_add (args, NULL);

  commandline = flatpak_quote_argv ((const char **) args->pdata);
  g_debug ("Running '%s'", commandline);


  if (flatpak_is_in_sandbox ())
    {
      if (builder_host_spawnv (NULL, NULL, G_SUBPROCESS_FLAGS_STDIN_INHERIT, NULL, (const gchar * const  *)args->pdata, NULL))
        exit (1);
      else
        exit (0);
    }
  else
    {
      if (execvp ((char *) args->pdata[0], (char **) args->pdata) == -1)
        {
          g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Unable to start flatpak build");
          return FALSE;
        }
    }

  /* Not reached */
  return TRUE;
}

char **
builder_manifest_get_exclude_dirs (BuilderManifest *self)
{
  g_autoptr(GPtrArray) dirs = NULL;
  GList *l;

  dirs = g_ptr_array_new ();

  for (l = self->add_extensions; l != NULL; l = l->next)
    {
      BuilderExtension *e = l->data;

      if (builder_extension_is_bundled (e))
        g_ptr_array_add (dirs, g_strdup (builder_extension_get_directory (e)));
    }
  g_ptr_array_add (dirs, NULL);

  return (char **)g_ptr_array_free (g_steal_pointer (&dirs), FALSE);
}
