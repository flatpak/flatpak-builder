/* builder-source-file.c
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
#include "builder-source-file.h"

struct BuilderSourceFile
{
  BuilderSource parent;

  char         *path;
  char         *url;
  char        **mirror_urls;
  char         *md5;
  char         *sha1;
  char         *sha256;
  char         *sha512;
  char         *dest_filename;
  char         *http_referer;
  gboolean      disable_http_decompression;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourceFileClass;

G_DEFINE_TYPE (BuilderSourceFile, builder_source_file, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_PATH,
  PROP_URL,
  PROP_MD5,
  PROP_SHA1,
  PROP_SHA256,
  PROP_SHA512,
  PROP_DEST_FILENAME,
  PROP_MIRROR_URLS,
  PROP_HTTP_REFERER,
  PROP_DISABLE_HTTP_DECOMPRESSION,
  LAST_PROP
};

static void
builder_source_file_finalize (GObject *object)
{
  BuilderSourceFile *self = (BuilderSourceFile *) object;

  g_free (self->path);
  g_free (self->url);
  g_free (self->md5);
  g_free (self->sha1);
  g_free (self->sha256);
  g_free (self->sha512);
  g_free (self->dest_filename);
  g_free (self->http_referer);
  g_strfreev (self->mirror_urls);

  G_OBJECT_CLASS (builder_source_file_parent_class)->finalize (object);
}

static void
builder_source_file_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, self->path);
      break;

    case PROP_URL:
      g_value_set_string (value, self->url);
      break;

    case PROP_MD5:
      g_value_set_string (value, self->md5);
      break;

    case PROP_SHA1:
      g_value_set_string (value, self->sha1);
      break;

    case PROP_SHA256:
      g_value_set_string (value, self->sha256);
      break;

    case PROP_SHA512:
      g_value_set_string (value, self->sha512);
      break;

    case PROP_DEST_FILENAME:
      g_value_set_string (value, self->dest_filename);
      break;

    case PROP_MIRROR_URLS:
      g_value_set_boxed (value, self->mirror_urls);
      break;

    case PROP_HTTP_REFERER:
      g_value_set_string (value, self->http_referer);
      break;

    case PROP_DISABLE_HTTP_DECOMPRESSION:
      g_value_set_boolean (value, self->disable_http_decompression);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_file_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (object);
  gchar **tmp;

  switch (prop_id)
    {
    case PROP_PATH:
      g_free (self->path);
      self->path = g_value_dup_string (value);
      break;

    case PROP_URL:
      g_free (self->url);
      self->url = g_value_dup_string (value);
      break;

    case PROP_MD5:
      g_free (self->md5);
      self->md5 = g_value_dup_string (value);
      if (self->md5 != NULL && self->md5[0] != '\0')
        {
          g_printerr ("The \"md5\" source property is deprecated due to the weakness of MD5 hashes.\n");
          g_printerr ("Use the \"sha256\" property for the more secure SHA256 hash.\n");
        }

      break;

    case PROP_SHA1:
      g_free (self->sha1);
      self->sha1 = g_value_dup_string (value);
      if (self->sha1 != NULL && self->sha1[0] != '\0')
        {
          g_printerr ("The \"sha1\" source property is deprecated due to the weakness of SHA1 hashes.\n");
          g_printerr ("Use the \"sha256\" property for the more secure SHA256 hash.\n");
        }

      break;

    case PROP_SHA256:
      g_free (self->sha256);
      self->sha256 = g_value_dup_string (value);
      break;

    case PROP_SHA512:
      g_free (self->sha512);
      self->sha512 = g_value_dup_string (value);
      break;

    case PROP_DEST_FILENAME:
      g_free (self->dest_filename);
      self->dest_filename = g_value_dup_string (value);
      break;

    case PROP_MIRROR_URLS:
      tmp = self->mirror_urls;
      self->mirror_urls = g_strdupv (g_value_get_boxed (value));
      g_strfreev (tmp);
      break;

    case PROP_HTTP_REFERER:
      g_free (self->http_referer);
      self->http_referer = g_value_dup_string (value);
      break;

    case PROP_DISABLE_HTTP_DECOMPRESSION:
      self->disable_http_decompression = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
builder_source_file_validate (BuilderSource  *source,
                              GError        **error)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (source);

  if (self->dest_filename != NULL &&
      strchr (self->dest_filename, '/') != NULL)
    return flatpak_fail (error, "No slashes allowed in dest-filename, use dest property for directory");

  return TRUE;
}


static GUri *
get_uri (BuilderSourceFile *self,
         GError           **error)
{
  GUri *uri;

  if (self->url == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "URL not specified");
      return NULL;
    }

  uri = g_uri_parse (self->url, CONTEXT_HTTP_URI_FLAGS, error);
  if (uri == NULL)
    {
      return NULL;
    }
  return uri;
}

static GFile *
get_download_location (BuilderSourceFile *self,
                       gboolean          *is_inline,
                       BuilderContext    *context,
                       GError           **error)
{
  g_autoptr(GUri) uri = NULL;
  const char *path;
  g_autofree char *base_name = NULL;
  g_autoptr(GFile) file = NULL;
  const char *checksums[BUILDER_CHECKSUMS_LEN];
  GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN];

  uri = get_uri (self, error);
  if (uri == NULL)
    return FALSE;

  path = g_uri_get_path (uri);

  if (g_str_has_prefix (self->url, "data:"))
    {
      *is_inline = TRUE;
      return g_file_new_for_path ("inline data");
    }
  *is_inline = FALSE;

  base_name = g_path_get_basename (path);

  builder_get_all_checksums (checksums, checksums_type,
                             self->md5,
                             self->sha1,
                             self->sha256,
                             self->sha512);

  if (checksums[0] == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No checksum specified for file source %s", base_name);
      return FALSE;
    }

  file = builder_context_find_in_sources_dirs (context,
                                               "downloads",
                                               checksums[0],
                                               base_name,
                                               NULL);
  if (file != NULL)
    return g_steal_pointer (&file);

  file = flatpak_build_file (builder_context_get_download_dir (context),
                             checksums[0],
                             base_name,
                             NULL);
  return g_steal_pointer (&file);
}

static GFile *
get_source_file (BuilderSourceFile *self,
                 BuilderContext    *context,
                 gboolean          *is_local,
                 gboolean          *is_inline,
                 GError           **error)
{
  GFile *base_dir = BUILDER_SOURCE (self)->base_dir;

  if (self->url != NULL && self->url[0] != 0)
    {
      *is_local = FALSE;
      return get_download_location (self, is_inline, context, error);
    }

  if (self->path != NULL && self->path[0] != 0)
    {
      g_autoptr(GFile) file = NULL;
      *is_local = TRUE;
      *is_inline = FALSE;
      file = g_file_resolve_relative_path (base_dir, self->path);

      if (!builder_context_ensure_parent_dir_sandboxed (context, file, error))
        {
          g_prefix_error (error, "Unable to get source file '%s': ", self->path);
          return NULL;
        }

      return g_steal_pointer (&file);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "source file path or url not specified");
  return NULL;
}

#define BASE64_INDICATOR ";base64"
#define BASE64_INDICATOR_LEN (strlen (BASE64_INDICATOR))

static GBytes *
download_data_uri (const char     *url,
                   GError        **error)
{
  gboolean is_base64 = FALSE;
  const char *comma, *start;
  g_autoptr(GBytes) bytes = NULL;

  /* This is a simplified version of soup_uri_decode_data_uri()
   * SPDX-License-Identifier: LGPL-2.0-or-later
   */

  start = url + strlen ("data:");
  comma = strchr (start, ',');

  /* Check params to see if it is base64. */
  if (comma && comma != start)
  {
    if (comma >= start + BASE64_INDICATOR_LEN && !g_ascii_strncasecmp (comma - BASE64_INDICATOR_LEN,
                                                                        BASE64_INDICATOR, BASE64_INDICATOR_LEN))
      is_base64 = TRUE;
  }

  if (comma)
    start = comma + 1;

  if (*start)
    {
      bytes = g_uri_unescape_bytes (start, -1, NULL, error);
      if (!bytes)
        return NULL;

      if (is_base64)
        {
          GByteArray *unescaped_array;
          gsize content_length;

          if (g_bytes_get_size (bytes) <= 1)
            {
              g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "data URI contains invalid base64 data");
              return NULL;
            }

          unescaped_array = g_bytes_unref_to_array (bytes);
          g_base64_decode_inplace ((char*)unescaped_array->data, &content_length);
          unescaped_array->len = content_length;
          bytes = g_byte_array_free_to_bytes (unescaped_array);
        }
    }
  else
    bytes = g_bytes_new_static (NULL, 0); /* Empty data */

  return g_steal_pointer (&bytes);
}

static gboolean
builder_source_file_show_deps (BuilderSource  *source,
                               GError        **error)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (source);

  if (self->path && self->path[0] != 0)
    g_print ("%s\n", self->path);

  return TRUE;
}

static gboolean
builder_source_file_download (BuilderSource  *source,
                              gboolean        update_vcs,
                              BuilderContext *context,
                              GError        **error)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (source);

  g_autoptr(GFile) file = NULL;
  gboolean is_local, is_inline;
  g_autofree char *base_name = NULL;
  const char *checksums[BUILDER_CHECKSUMS_LEN];
  GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN];

  file = get_source_file (self, context, &is_local, &is_inline, error);
  if (file == NULL)
    return FALSE;

  base_name = g_file_get_basename (file);

  builder_get_all_checksums (checksums, checksums_type,
                             self->md5,
                             self->sha1,
                             self->sha256,
                             self->sha512);

  if (g_file_query_exists (file, NULL))
    {
      return !is_local || checksums[0] == NULL ||
             builder_verify_checksums (base_name, file,
                                       checksums, checksums_type,
                                       error);
    }

  if (is_inline)
    return TRUE;

  if (is_local)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't find file at %s", self->path);
      return FALSE;
    }

  if (checksums[0] == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No checksum specified for file source %s", base_name);
      return FALSE;
    }

  if (!builder_context_download_uri (context,
                                     self->url,
                                     (const char **)self->mirror_urls,
                                     self->http_referer,
                                     self->disable_http_decompression,
                                     file,
                                     checksums,
                                     checksums_type,
                                     error))
    return FALSE;

  return TRUE;
}

static gboolean
builder_source_file_extract (BuilderSource  *source,
                             GFile          *dest,
                             GFile          *source_dir,
                             BuilderOptions *build_options,
                             BuilderContext *context,
                             GError        **error)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (source);

  g_autoptr(GFile) src = NULL;
  g_autoptr(GFile) dest_file = NULL;
  g_autofree char *dest_filename = NULL;
  gboolean is_local, is_inline;

  src = get_source_file (self, context, &is_local, &is_inline, error);
  if (src == NULL)
    return FALSE;

  if (self->dest_filename)
    {
      dest_filename = g_strdup (self->dest_filename);
    }
  else
    {
      if (is_inline)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "No dest-filename set for inline file data");
          return FALSE;
        }
      dest_filename = g_file_get_basename (src);
    }

  dest_file = g_file_get_child (dest, dest_filename);

  /* If the destination file exists, just delete it. We can encounter errors when
   * trying to overwrite files that are not writable.
   */
  if (flatpak_file_query_exists_nofollow (dest_file) &&
      !g_file_delete (dest_file, NULL, error))
    return FALSE;

  if (is_inline)
    {
      g_autoptr(GBytes) content = NULL;

      content = download_data_uri (self->url,
                                   error);
      if (content == NULL)
        return FALSE;

      if (!g_file_set_contents (flatpak_file_get_path_cached (dest_file),
                                g_bytes_get_data (content, NULL),
                                g_bytes_get_size (content), error))
        return FALSE;
    }
  else
    {
      /* Make sure the target is gone, because g_file_copy does
         truncation on hardlinked destinations */
      (void)g_file_delete (dest_file, NULL, NULL);

      if (!g_file_copy (src, dest_file,
                        G_FILE_COPY_OVERWRITE,
                        NULL,
                        NULL, NULL,
                        error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
builder_source_file_bundle (BuilderSource  *source,
                            BuilderContext *context,
                            GError        **error)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (source);

  g_autoptr(GFile) file = NULL;
  g_autoptr(GFile) destination_file = NULL;
  g_autoptr(GFile) destination_dir = NULL;
  g_autofree char *file_name = NULL;
  gboolean is_local, is_inline;
  const char *checksums[BUILDER_CHECKSUMS_LEN];
  GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN];

  file = get_source_file (self, context, &is_local, &is_inline, error);
  if (file == NULL)
    return FALSE;

  /* Inline URIs (data://) need not be bundled */
  if (is_inline)
    return TRUE;

  builder_get_all_checksums (checksums, checksums_type,
                             self->md5,
                             self->sha1,
                             self->sha256,
                             self->sha512);

  if (is_local)
    {
      GFile *manifest_base_dir = builder_context_get_base_dir (context);
      g_autofree char *rel_path = g_file_get_relative_path (manifest_base_dir, file);

      if (rel_path == NULL)
        {
          g_warning ("Local file %s is outside manifest tree, not bundling", flatpak_file_get_path_cached (file));
          return TRUE;
        }

      destination_file = flatpak_build_file (builder_context_get_app_dir (context),
                                             "sources/manifest", rel_path, NULL);
    }
  else
    {
      file_name = g_file_get_basename (file);
      destination_file = flatpak_build_file (builder_context_get_app_dir (context),
                                             "sources/downloads",
                                             checksums[0],
                                             file_name,
                                             NULL);
    }

  destination_dir = g_file_get_parent (destination_file);
  if (!flatpak_mkdir_p (destination_dir, NULL, error))
    return FALSE;

  if (!g_file_copy (file, destination_file,
                    G_FILE_COPY_OVERWRITE,
                    NULL,
                    NULL, NULL,
                    error))
    return FALSE;

  return TRUE;
}

static gboolean
builder_source_file_update (BuilderSource  *source,
                            BuilderContext *context,
                            GError        **error)
{
  return TRUE;
}

static void
builder_source_file_checksum (BuilderSource  *source,
                              BuilderCache   *cache,
                              BuilderContext *context)
{
  BuilderSourceFile *self = BUILDER_SOURCE_FILE (source);

  g_autoptr(GFile) src = NULL;
  g_autofree char *data = NULL;
  gsize len;
  gboolean is_local, is_inline;

  src = get_source_file (self, context, &is_local, &is_inline, NULL);
  if (src == NULL)
    return;

  if (is_local &&
      g_file_load_contents (src, NULL, &data, &len, NULL, NULL))
    builder_cache_checksum_data (cache, (guchar *) data, len);

  builder_cache_checksum_str (cache, self->path);
  builder_cache_checksum_str (cache, self->url);
  builder_cache_checksum_str (cache, self->sha256);
  builder_cache_checksum_compat_str (cache, self->md5);
  builder_cache_checksum_compat_str (cache, self->sha1);
  builder_cache_checksum_compat_str (cache, self->sha512);
  builder_cache_checksum_str (cache, self->dest_filename);
  builder_cache_checksum_compat_strv (cache, self->mirror_urls);
}

static void
builder_source_file_class_init (BuilderSourceFileClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_file_finalize;
  object_class->get_property = builder_source_file_get_property;
  object_class->set_property = builder_source_file_set_property;

  source_class->show_deps = builder_source_file_show_deps;
  source_class->download = builder_source_file_download;
  source_class->extract = builder_source_file_extract;
  source_class->bundle = builder_source_file_bundle;
  source_class->update = builder_source_file_update;
  source_class->checksum = builder_source_file_checksum;
  source_class->validate = builder_source_file_validate;

  g_object_class_install_property (object_class,
                                   PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_URL,
                                   g_param_spec_string ("url",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_MD5,
                                   g_param_spec_string ("md5",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_SHA1,
                                   g_param_spec_string ("sha1",
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
                                   PROP_SHA512,
                                   g_param_spec_string ("sha512",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_DEST_FILENAME,
                                   g_param_spec_string ("dest-filename",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_MIRROR_URLS,
                                   g_param_spec_boxed ("mirror-urls",
                                                       "",
                                                       "",
                                                       G_TYPE_STRV,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_HTTP_REFERER,
                                   g_param_spec_string ("referer",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_DISABLE_HTTP_DECOMPRESSION,
                                   g_param_spec_boolean ("disable-http-decompression",
                                                        "",
                                                        "",
                                                        FALSE,
                                                        G_PARAM_READWRITE));
}

static void
builder_source_file_init (BuilderSourceFile *self)
{
}
