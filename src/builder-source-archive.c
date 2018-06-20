/* builder-source-archive.c
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
#include "builder-source-archive.h"

struct BuilderSourceArchive
{
  BuilderSource parent;

  char         *path;
  char         *url;
  char         *md5;
  char         *sha1;
  char         *sha256;
  char         *sha512;
  guint         strip_components;
  char         *dest_filename;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourceArchiveClass;

G_DEFINE_TYPE (BuilderSourceArchive, builder_source_archive, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_PATH,
  PROP_URL,
  PROP_MD5,
  PROP_SHA1,
  PROP_SHA256,
  PROP_SHA512,
  PROP_STRIP_COMPONENTS,
  PROP_DEST_FILENAME,
  LAST_PROP
};

typedef enum {
  UNKNOWN,
  RPM,
  TAR,
  TAR_GZIP,
  TAR_COMPRESS,
  TAR_BZIP2,
  TAR_LZIP,
  TAR_LZMA,
  TAR_LZOP,
  TAR_XZ,
  ZIP
} BuilderArchiveType;

static gboolean
is_tar (BuilderArchiveType type)
{
  return (type >= TAR) && (type <= TAR_XZ);
}

static const char *
tar_decompress_flag (BuilderArchiveType type)
{
  switch (type)
    {
    default:
    case TAR:
      return NULL;

    case TAR_GZIP:
      return "-z";

    case TAR_COMPRESS:
      return "-Z";

    case TAR_BZIP2:
      return "-j";

    case TAR_LZIP:
      return "--lzip";

    case TAR_LZMA:
      return "--lzma";

    case TAR_LZOP:
      return "--lzop";

    case TAR_XZ:
      return "-J";
    }
}

static void
builder_source_archive_finalize (GObject *object)
{
  BuilderSourceArchive *self = (BuilderSourceArchive *) object;

  g_free (self->url);
  g_free (self->path);
  g_free (self->md5);
  g_free (self->sha1);
  g_free (self->sha256);
  g_free (self->sha512);
  g_free (self->dest_filename);

  G_OBJECT_CLASS (builder_source_archive_parent_class)->finalize (object);
}

static void
builder_source_archive_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  BuilderSourceArchive *self = BUILDER_SOURCE_ARCHIVE (object);

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

    case PROP_STRIP_COMPONENTS:
      g_value_set_uint (value, self->strip_components);
      break;

    case PROP_DEST_FILENAME:
      g_value_set_string (value, self->dest_filename);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_archive_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  BuilderSourceArchive *self = BUILDER_SOURCE_ARCHIVE (object);

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
      break;

    case PROP_SHA1:
      g_free (self->sha1);
      self->sha1 = g_value_dup_string (value);
      break;

    case PROP_SHA256:
      g_free (self->sha256);
      self->sha256 = g_value_dup_string (value);
      break;

    case PROP_SHA512:
      g_free (self->sha512);
      self->sha512 = g_value_dup_string (value);
      break;

    case PROP_STRIP_COMPONENTS:
      self->strip_components = g_value_get_uint (value);
      break;

    case PROP_DEST_FILENAME:
      g_free (self->dest_filename);
      self->dest_filename = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static SoupURI *
get_uri (BuilderSourceArchive *self,
         GError              **error)
{
  SoupURI *uri;

  if (self->url == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "URL not specified");
      return NULL;
    }

  uri = soup_uri_new (self->url);
  if (uri == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid URL '%s'", self->url);
      return NULL;
    }
  return uri;
}

static GFile *
get_download_location (BuilderSourceArchive *self,
                       BuilderContext       *context,
                       gboolean             *is_local,
                       GError              **error)
{
  g_autoptr(SoupURI) uri = NULL;
  const char *path;
  g_autofree char *base_name = NULL;
  g_autoptr(GFile) file = NULL;
  const char *checksums[BUILDER_CHECKSUMS_LEN];
  GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN];

  uri = get_uri (self, error);
  if (uri == NULL)
    return FALSE;

  path = soup_uri_get_path (uri);

  base_name = g_path_get_basename (path);

  builder_get_all_checksums (checksums, checksums_type,
                             self->md5,
                             self->sha1,
                             self->sha256,
                             self->sha512);

  if (checksums[0] == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No checksum specified for archive source %s", base_name);
      return FALSE;
    }

  file = builder_context_find_in_sources_dirs (context,
                                               "downloads",
                                               checksums[0],
                                               base_name,
                                               NULL);
  if (file)
    {
      *is_local = TRUE;
      return g_steal_pointer (&file);
    }

  return flatpak_build_file (builder_context_get_download_dir (context),
                             checksums[0],
                             base_name,
                             NULL);
}

static GFile *
get_source_file (BuilderSourceArchive *self,
                 BuilderContext       *context,
                 gboolean             *is_local,
                 GError              **error)
{
  GFile *base_dir = BUILDER_SOURCE (self)->base_dir;

  if (self->url != NULL && self->url[0] != 0)
    {
      *is_local = FALSE;
      return get_download_location (self, context, is_local, error);
    }

  if (self->path != NULL && self->path[0] != 0)
    {
      *is_local = TRUE;
      return g_file_resolve_relative_path (base_dir, self->path);
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "source file path or url not specified");
  return NULL;
}

static gboolean
builder_source_archive_show_deps (BuilderSource  *source,
                                  GError        **error)
{
  BuilderSourceArchive *self = BUILDER_SOURCE_ARCHIVE (source);

  if (self->path && self->path[0] != 0)
    g_print ("%s\n", self->path);

  return TRUE;
}

static gboolean
builder_source_archive_download (BuilderSource  *source,
                                 gboolean        update_vcs,
                                 BuilderContext *context,
                                 GError        **error)
{
  BuilderSourceArchive *self = BUILDER_SOURCE_ARCHIVE (source);

  g_autoptr(GFile) file = NULL;
  g_autofree char *base_name = NULL;
  gboolean is_local;
  const char *checksums[BUILDER_CHECKSUMS_LEN];
  GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN];

  file = get_source_file (self, context, &is_local, error);
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
      if (is_local && checksums[0]  != NULL)
        {
          g_autofree char *data = NULL;
          gsize len;

          if (!g_file_load_contents (file, NULL, &data, &len, NULL, error))
            return FALSE;

          if (!builder_verify_checksums (base_name,
                                         data, len,
                                         checksums, checksums_type,
                                         error))
            return FALSE;
        }
      return TRUE;
    }

  if (is_local)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't find file at %s", self->path);
      return FALSE;
    }

  if (!builder_context_download_uri (context,
                                     self->url,
                                     file,
                                     checksums,
                                     checksums_type,
                                     error))
    return FALSE;

  return TRUE;
}

static gboolean
tar (GFile   *dir,
     GError **error,
     ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (dir, NULL, 0, error, "tar", ap);
  va_end (ap);

  return res;
}

static gboolean
unzip (GFile   *dir,
       GError **error,
       ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (dir, NULL, 0, error, "unzip", ap);
  va_end (ap);

  return res;
}

static gboolean
unrpm (GFile   *dir,
       const char *rpm_path,
       GError **error)
{
  gboolean res;
  const gchar *argv[] = { "sh", "-c", "rpm2cpio \"$1\" | cpio -i -d",
      "sh", /* shell's $0 */
      rpm_path, /* shell's $1 */
      NULL };

  res = flatpak_spawnv (dir, NULL, 0, error, argv);

  return res;
}

static BuilderArchiveType
get_type (GFile *archivefile)
{
  g_autofree char *base_name = NULL;
  g_autofree gchar *lower = NULL;

  base_name = g_file_get_basename (archivefile);
  lower = g_ascii_strdown (base_name, -1);

  if (g_str_has_suffix (lower, ".tar"))
    return TAR;

  if (g_str_has_suffix (lower, ".tar.gz") ||
      g_str_has_suffix (lower, ".tgz") ||
      g_str_has_suffix (lower, ".taz"))
    return TAR_GZIP;

  if (g_str_has_suffix (lower, ".tar.Z") ||
      g_str_has_suffix (lower, ".taZ"))
    return TAR_COMPRESS;

  if (g_str_has_suffix (lower, ".tar.bz2") ||
      g_str_has_suffix (lower, ".tz2") ||
      g_str_has_suffix (lower, ".tbz2") ||
      g_str_has_suffix (lower, ".tbz"))
    return TAR_BZIP2;

  if (g_str_has_suffix (lower, ".tar.lz"))
    return TAR_LZIP;

  if (g_str_has_suffix (lower, ".tar.lzma") ||
      g_str_has_suffix (lower, ".tlz"))
    return TAR_LZMA;

  if (g_str_has_suffix (lower, ".tar.lzo"))
    return TAR_LZOP;

  if (g_str_has_suffix (lower, ".tar.xz"))
    return TAR_XZ;

  if (g_str_has_suffix (lower, ".zip"))
    return ZIP;

  if (g_str_has_suffix (lower, ".rpm"))
    return RPM;

  return UNKNOWN;
}

static gboolean
strip_components_into (GFile   *dest,
                       GFile   *src,
                       int      level,
                       GError **error)
{
  g_autoptr(GFileEnumerator) dir_enum = NULL;
  g_autoptr(GFileInfo) child_info = NULL;
  GError *temp_error = NULL;

  dir_enum = g_file_enumerate_children (src, "standard::name,standard::type",
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                        NULL, error);
  if (!dir_enum)
    return FALSE;

  while ((child_info = g_file_enumerator_next_file (dir_enum, NULL, &temp_error)))
    {
      g_autoptr(GFile) child = NULL;
      g_autoptr(GFile) dest_child = NULL;

      child = g_file_get_child (src, g_file_info_get_name (child_info));

      if (g_file_info_get_file_type (child_info) == G_FILE_TYPE_DIRECTORY &&
          level > 0)
        {
          if (!strip_components_into (dest, child, level - 1, error))
            return FALSE;

          g_clear_object (&child_info);
          continue;
        }

      dest_child = g_file_get_child (dest, g_file_info_get_name (child_info));
      if (!g_file_move (child, dest_child, G_FILE_COPY_NONE, NULL, NULL, NULL, error))
        return FALSE;

      g_clear_object (&child_info);
      continue;
    }

  if (temp_error != NULL)
    {
      g_propagate_error (error, temp_error);
      return FALSE;
    }

  if (!g_file_delete (src, NULL, error))
    return FALSE;

  return TRUE;
}

static GFile *
create_uncompress_directory (BuilderSourceArchive *self, GFile *dest, GError **error)
{
  GFile *uncompress_dest = NULL;

  if (self->strip_components > 0)
    {
      g_autoptr(GFile) tmp_dir_template = g_file_get_child (dest, ".uncompressXXXXXX");
      g_autofree char *tmp_dir_path = g_file_get_path (tmp_dir_template);

      if (g_mkdtemp (tmp_dir_path) == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Can't create uncompress directory");
          return NULL;
        }

      uncompress_dest = g_file_new_for_path (tmp_dir_path);
    }
  else
    {
      uncompress_dest = g_object_ref (dest);
    }

  return uncompress_dest;
}

static gboolean
builder_source_archive_extract (BuilderSource  *source,
                                GFile          *dest,
                                BuilderOptions *build_options,
                                BuilderContext *context,
                                GError        **error)
{
  BuilderSourceArchive *self = BUILDER_SOURCE_ARCHIVE (source);

  g_autoptr(GFile) archivefile = NULL;
  g_autofree char *archive_path = NULL;
  BuilderArchiveType type;
  gboolean is_local;
  g_autoptr(GFile) dest_file = NULL;
  g_autofree char *dest_filename = NULL;

  archivefile = get_source_file (self, context, &is_local, error);
  if (archivefile == NULL)
    return FALSE;

  if (self->dest_filename)
    {
      dest_filename = g_strdup (self->dest_filename);
      dest_file = g_file_get_child (dest, dest_filename);

      /* If the destination file exists, just delete it. We can encounter errors when
       * trying to overwrite files that are not writable. */
      if (g_file_query_exists (dest_file, NULL) && !g_file_delete (dest_file, NULL, error))
        return FALSE;

      /* Make sure the target is gone, because g_file_copy does
         truncation on hardlinked destinations */
      (void)g_file_delete (dest_file, NULL, NULL);

      if (!g_file_copy (archivefile, dest_file,
            G_FILE_COPY_OVERWRITE,
            NULL,
            NULL, NULL,
            error))
        return FALSE;

      archivefile = dest_file;
    }


  type = get_type (archivefile);

  archive_path = g_file_get_path (archivefile);

  if (is_tar (type))
    {
      g_autofree char *strip_components = g_strdup_printf ("--strip-components=%u", self->strip_components);
      /* Note: tar_decompress_flag can return NULL, so put it last */
      if (!tar (dest, error, "xf", archive_path, "--no-same-owner", strip_components, tar_decompress_flag (type), NULL))
        return FALSE;
    }
  else if (type == ZIP)
    {
      g_autoptr(GFile) zip_dest = NULL;

      zip_dest = create_uncompress_directory (self, dest, error);
      if (zip_dest == NULL)
        return FALSE;

      if (!unzip (zip_dest, error, archive_path, NULL))
        return FALSE;

      if (self->strip_components > 0)
        {
          if (!strip_components_into (dest, zip_dest, self->strip_components, error))
            return FALSE;
        }
    }
  else if (type == RPM)
    {
      g_autoptr(GFile) rpm_dest = NULL;

      rpm_dest = create_uncompress_directory (self, dest, error);
      if (rpm_dest == NULL)
        return FALSE;

      if (!unrpm (rpm_dest, archive_path, error))
        return FALSE;

      if (self->strip_components > 0)
        {
          if (!strip_components_into (dest, rpm_dest, self->strip_components, error))
            return FALSE;
        }
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unknown archive format of '%s'", archive_path);
      return FALSE;
    }

  return TRUE;
}

static gboolean
builder_source_archive_bundle (BuilderSource  *source,
                               BuilderContext *context,
                               GError        **error)
{
  BuilderSourceArchive *self = BUILDER_SOURCE_ARCHIVE (source);

  g_autoptr(GFile) file = NULL;
  g_autoptr(GFile) download_dir = NULL;
  g_autoptr(GFile) destination_file = NULL;
  g_autofree char *download_dir_path = NULL;
  g_autofree char *file_name = NULL;
  g_autofree char *destination_file_path = NULL;
  g_autofree char *app_dir_path = NULL;
  gboolean is_local;
  const char *checksums[BUILDER_CHECKSUMS_LEN];
  GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN];

  file = get_source_file (self, context, &is_local, error);
  if (file == NULL)
    return FALSE;

  builder_get_all_checksums (checksums, checksums_type,
                             self->md5,
                             self->sha1,
                             self->sha256,
                             self->sha512);

  app_dir_path = g_file_get_path (builder_context_get_app_dir (context));
  download_dir_path = g_build_filename (app_dir_path,
                                        "sources",
                                        "downloads",
                                        checksums[0],
                                        NULL);
  download_dir = g_file_new_for_path (download_dir_path);
  if (!flatpak_mkdir_p (download_dir, NULL, error))
    return FALSE;

  file_name = g_file_get_basename (file);
  destination_file_path = g_build_filename (download_dir_path,
                                            file_name,
                                            NULL);
  destination_file = g_file_new_for_path (destination_file_path);

  if (!g_file_copy (file, destination_file,
                    G_FILE_COPY_OVERWRITE,
                    NULL,
                    NULL, NULL,
                    error))
    return FALSE;

  return TRUE;
}

static void
builder_source_archive_checksum (BuilderSource  *source,
                                 BuilderCache   *cache,
                                 BuilderContext *context)
{
  BuilderSourceArchive *self = BUILDER_SOURCE_ARCHIVE (source);

  builder_cache_checksum_str (cache, self->url);
  builder_cache_checksum_str (cache, self->sha256);
  builder_cache_checksum_compat_str (cache, self->md5);
  builder_cache_checksum_compat_str (cache, self->sha1);
  builder_cache_checksum_compat_str (cache, self->sha512);
  builder_cache_checksum_uint32 (cache, self->strip_components);
  builder_cache_checksum_str (cache, self->dest_filename);
}


static void
builder_source_archive_class_init (BuilderSourceArchiveClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_archive_finalize;
  object_class->get_property = builder_source_archive_get_property;
  object_class->set_property = builder_source_archive_set_property;

  source_class->show_deps = builder_source_archive_show_deps;
  source_class->download = builder_source_archive_download;
  source_class->extract = builder_source_archive_extract;
  source_class->bundle = builder_source_archive_bundle;
  source_class->checksum = builder_source_archive_checksum;

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
                                   PROP_STRIP_COMPONENTS,
                                   g_param_spec_uint ("strip-components",
                                                      "",
                                                      "",
                                                      0, G_MAXUINT,
                                                      1,
                                                      G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
                                   PROP_DEST_FILENAME,
                                   g_param_spec_string ("dest-filename",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
}

static void
builder_source_archive_init (BuilderSourceArchive *self)
{
  self->strip_components = 1;
}
