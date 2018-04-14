/*
 * Copyright © 2015 Red Hat, Inc
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
 *       Alexander Larsson <alexl@redhat.com>
 */

#ifndef __BUILDER_UTILS_H__
#define __BUILDER_UTILS_H__

#include <gio/gio.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include <libxml/tree.h>

G_BEGIN_DECLS

#define BUILDER_N_CHECKSUMS 4 /* We currently support 4 checksum types */
#define BUILDER_CHECKSUMS_LEN (BUILDER_N_CHECKSUMS + 1) /* One more for null termination */

typedef struct BuilderUtils BuilderUtils;

char *builder_uri_to_filename (const char *uri);

gboolean strip (GError **error,
                ...);
gboolean eu_strip (GError **error,
                   ...);
gboolean eu_elfcompress (GError **error,
			 ...);

gboolean is_elf_file (const char *path,
                      gboolean   *is_shared,
                      gboolean   *is_stripped);

char ** builder_get_debuginfo_file_references (const char *filename,
                                               GError    **error);

gboolean directory_is_empty (const char *path);

gboolean flatpak_matches_path_pattern (const char *path,
                                       const char *pattern);
void     flatpak_collect_matches_for_path_pattern (const char *path,
                                                   const char *pattern,
                                                   const char *add_prefix,
                                                   GHashTable *to_remove_ht);
gboolean builder_migrate_locale_dirs (GFile   *root_dir,
                                      GError **error);

GQuark builder_yaml_parse_error_quark (void);
#define BUILDER_YAML_PARSE_ERROR (builder_yaml_parse_error_quark ())

GObject * builder_gobject_from_data (GType       gtype,
                                     const char *relpath,
                                     const char *contents,
                                     GError    **error);

gboolean builder_host_spawnv (GFile                *dir,
                              char                **output,
                              GSubprocessFlags      flags,
                              GError              **error,
                              const gchar * const  *argv);
gboolean builder_maybe_host_spawnv (GFile                *dir,
                                    char                **output,
                                    GSubprocessFlags      flags,
                                    GError              **error,
                                    const gchar * const  *argv);

gboolean builder_download_uri (SoupURI        *uri,
                               GFile          *dest,
                               const char     *checksums[BUILDER_CHECKSUMS_LEN],
                               GChecksumType   checksums_type[BUILDER_CHECKSUMS_LEN],
                               SoupSession    *soup_session,
                               GError        **error);

gsize builder_get_all_checksums (const char *checksums[BUILDER_CHECKSUMS_LEN],
                                 GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN],
                                 const char *md5,
                                 const char *sha1,
                                 const char *sha256,
                                 const char *sha512);

gboolean builder_verify_checksums (const char *name,
                                   const char *data,
                                   gsize len,
                                   const char *checksums[BUILDER_CHECKSUMS_LEN],
                                   GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN],
                                   GError **error);

GParamSpec * builder_serializable_find_property_with_error (JsonSerializable *serializable,
                                                            const char       *name);

void builder_set_term_title (const gchar *format,
                             ...) G_GNUC_PRINTF (1, 2);

static inline void
xml_autoptr_cleanup_generic_free (void *p)
{
  void **pp = (void**)p;
  if (*pp)
    xmlFree (*pp);
}

#define xml_autofree _GLIB_CLEANUP(xml_autoptr_cleanup_generic_free)

G_DEFINE_AUTOPTR_CLEANUP_FUNC (xmlDoc, xmlFreeDoc)

G_END_DECLS

#endif /* __BUILDER_UTILS_H__ */
