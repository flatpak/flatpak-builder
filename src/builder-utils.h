/*
 * Copyright © 2015 Red Hat, Inc
 * Copyright © 2023 GNOME Foundation Inc.
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
 *       Hubert Figuière <hub@figuiere.net>
 */

#ifndef __BUILDER_UTILS_H__
#define __BUILDER_UTILS_H__

#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>

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

GQuark builder_curl_error_quark (void);
#define BUILDER_CURL_ERROR (builder_curl_error_quark ())

GQuark builder_yaml_parse_error_quark (void);
#define BUILDER_YAML_PARSE_ERROR (builder_yaml_parse_error_quark ())

JsonNode * builder_json_node_from_data (const char *relpath,
                                        const char *contents,
                                        GError    **error);

GObject * builder_gobject_from_data (GType       gtype,
                                     const char *relpath,
                                     const char *contents,
                                     GError    **error);

gboolean builder_host_spawnv (GFile                *dir,
                              char                **output,
                              GSubprocessFlags      flags,
                              GError              **error,
                              const gchar * const  *argv,
                              const gchar * const  *unresolved_argv);
gboolean builder_maybe_host_spawnv (GFile                *dir,
                                    char                **output,
                                    GSubprocessFlags      flags,
                                    GError              **error,
                                    const gchar * const  *argv,
                                    const gchar * const  *unresolved_argv);

gboolean builder_download_uri (GUri           *uri,
                               const char     *http_referer,
                               gboolean        disable_http_decompression,
                               GFile          *dest,
                               const char     *checksums[BUILDER_CHECKSUMS_LEN],
                               GChecksumType   checksums_type[BUILDER_CHECKSUMS_LEN],
                               CURL           *curl_session,
                               GError        **error);

gboolean builder_download_uri_buffer (GUri           *uri,
                                      const char     *http_referer,
                                      gboolean        disable_http_decompression,
                                      CURL           *session,
                                      GOutputStream  *out,
                                      GChecksum     **checksums,
                                      gsize           n_checksums,
                                      GError        **error);


gsize builder_get_all_checksums (const char *checksums[BUILDER_CHECKSUMS_LEN],
                                 GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN],
                                 const char *md5,
                                 const char *sha1,
                                 const char *sha256,
                                 const char *sha512);

gboolean builder_verify_checksums (const char *name,
                                   GFile *file,
                                   const char *checksums[BUILDER_CHECKSUMS_LEN],
                                   GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN],
                                   GError **error);

GParamSpec * builder_serializable_find_property (JsonSerializable *serializable,
                                                 const char       *name);
GParamSpec ** builder_serializable_list_properties (JsonSerializable *serializable,
                                                    guint            *n_pspecs);
gboolean builder_serializable_deserialize_property (JsonSerializable *serializable,
                                                    const gchar      *property_name,
                                                    GValue           *value,
                                                    GParamSpec       *pspec,
                                                    JsonNode         *property_node);
JsonNode * builder_serializable_serialize_property (JsonSerializable *serializable,
                                                    const gchar      *property_name,
                                                    const GValue     *value,
                                                    GParamSpec       *pspec);
void builder_serializable_get_property (JsonSerializable *serializable,
                                        GParamSpec       *pspec,
                                        GValue           *value);
void builder_serializable_set_property (JsonSerializable *serializable,
                                        GParamSpec       *pspec,
                                        const GValue     *value);

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

typedef struct FlatpakXml FlatpakXml;

struct FlatpakXml
{
  gchar      *element_name; /* NULL == text */
  char      **attribute_names;
  char      **attribute_values;
  char       *text;
  FlatpakXml *parent;
  FlatpakXml *first_child;
  FlatpakXml *last_child;
  FlatpakXml *next_sibling;
};

FlatpakXml *flatpak_xml_new (const gchar *element_name);
FlatpakXml *flatpak_xml_new_with_attributes (const gchar *element_name,
                                             const gchar **attribute_names,
                                             const gchar **attribute_values);
FlatpakXml *flatpak_xml_new_text (const gchar *text);
void       flatpak_xml_add (FlatpakXml *parent,
                            FlatpakXml *node);
void       flatpak_xml_free (FlatpakXml *node);
FlatpakXml *flatpak_xml_parse (GInputStream *in,
                               gboolean      compressed,
                               GCancellable *cancellable,
                               GError      **error);
const gchar *flatpak_xml_attribute (FlatpakXml  *node,
                                    const gchar *name);
gboolean flatpak_xml_set_attribute (FlatpakXml  *node,
                                    const gchar *name,
                                    const gchar *value);
void       flatpak_xml_to_string (FlatpakXml *node,
                                  GString    *res);
FlatpakXml *flatpak_xml_unlink (FlatpakXml *node,
                                FlatpakXml *prev_sibling);
/** Find the first child of `type`. */
FlatpakXml *flatpak_xml_find (FlatpakXml  *node,
                              const char  *type,
                              FlatpakXml **prev_child_out);
/** Find the next child from sibling. If `sibling` is NULL, it's
 *  equivalant to calling `flatpak_xml_find()`.
 */
FlatpakXml *flatpak_xml_find_next (FlatpakXml  *node,
                                   const char  *type,
                                   FlatpakXml  *sibling,
                                   FlatpakXml **prev_child_out);

GBytes *   flatpak_read_stream (GInputStream *in,
                                gboolean      null_terminate,
                                GError      **error);
GVariant * flatpak_variant_compress (GVariant *variant);
GVariant * flatpak_variant_uncompress (GVariant *variant, const GVariantType *type);


gboolean flatpak_version_check (int major,
                                int minor,
                                int micro);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakXml, flatpak_xml_free);

G_END_DECLS

#endif /* __BUILDER_UTILS_H__ */
