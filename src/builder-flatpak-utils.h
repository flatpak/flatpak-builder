/*
 * Copyright © 2014 Red Hat, Inc
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

#ifndef __FLATPAK_UTILS_H__
#define __FLATPAK_UTILS_H__

#include <string.h>

#include "libglnx/libglnx.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <libsoup/soup.h>
#include <ostree.h>
#include <json-glib/json-glib.h>

typedef enum {
  FLATPAK_HOST_COMMAND_FLAGS_CLEAR_ENV = 1 << 0,
} FlatpakHostCommandFlags;

typedef void (*FlatpakLoadUriProgress) (guint64 downloaded_bytes,
                                        gpointer user_data);


#define FLATPAK_METADATA_GROUP_PREFIX_EXTENSION "Extension "
#define FLATPAK_METADATA_KEY_ADD_LD_PATH "add-ld-path"
#define FLATPAK_METADATA_KEY_AUTODELETE "autodelete"
#define FLATPAK_METADATA_KEY_DIRECTORY "directory"
#define FLATPAK_METADATA_KEY_DOWNLOAD_IF "download-if"
#define FLATPAK_METADATA_KEY_ENABLE_IF "enable-if"
#define FLATPAK_METADATA_KEY_MERGE_DIRS "merge-dirs"
#define FLATPAK_METADATA_KEY_NO_AUTODOWNLOAD "no-autodownload"
#define FLATPAK_METADATA_KEY_LOCALE_SUBSET "locale-subset"
#define FLATPAK_METADATA_KEY_SUBDIRECTORIES "subdirectories"
#define FLATPAK_METADATA_KEY_SUBDIRECTORY_SUFFIX "subdirectory-suffix"
#define FLATPAK_METADATA_KEY_VERSION "version"
#define FLATPAK_METADATA_KEY_VERSIONS "versions"

#define FLATPAK_METADATA_GROUP_APPLICATION "Application"
#define FLATPAK_METADATA_GROUP_RUNTIME "Runtime"
#define FLATPAK_METADATA_KEY_COMMAND "command"
#define FLATPAK_METADATA_KEY_NAME "name"
#define FLATPAK_METADATA_KEY_REQUIRED_FLATPAK "required-flatpak"
#define FLATPAK_METADATA_KEY_RUNTIME "runtime"
#define FLATPAK_METADATA_KEY_SDK "sdk"
#define FLATPAK_METADATA_KEY_TAGS "tags"

/* https://github.com/GNOME/libglnx/pull/38
 * Note by using #define rather than wrapping via a static inline, we
 * don't have to re-define attributes like G_GNUC_PRINTF.
 */
#define flatpak_fail glnx_throw

gint flatpak_strcmp0_ptr (gconstpointer a,
                          gconstpointer b);

gboolean  flatpak_has_path_prefix (const char *str,
                                   const char *prefix);

const char * flatpak_path_match_prefix (const char *pattern,
                                        const char *path);

gboolean flatpak_is_in_sandbox (void);

gboolean flatpak_fancy_output (void);

const char * flatpak_get_arch (void);
const char ** flatpak_get_arches (void);

char ** flatpak_get_current_locale_subpaths (void);

GFile *flatpak_file_new_tmp_in (GFile *dir,
                                const char *templatename,
                                GError        **error);
gboolean flatpak_break_hardlink (GFile *file, GError **error);

gboolean flatpak_write_update_checksum (GOutputStream  *out,
                                        gconstpointer   data,
                                        gsize           len,
                                        gsize          *out_bytes_written,
                                        GChecksum     **checksums,
                                        gsize           n_checksums,
                                        GCancellable   *cancellable,
                                        GError        **error);


gboolean flatpak_splice_update_checksum (GOutputStream  *out,
                                         GInputStream   *in,
                                         GChecksum     **checksums,
                                         gsize           n_checksums,
                                         FlatpakLoadUriProgress progress,
                                         gpointer        progress_data,
                                         GCancellable   *cancellable,
                                         GError        **error);

GBytes * flatpak_read_stream (GInputStream *in,
                              gboolean      null_terminate,
                              GError      **error);


gboolean flatpak_has_name_prefix (const char *string,
                                  const char *name);
gboolean flatpak_is_valid_name (const char *string,
                                GError **error);
gboolean flatpak_is_valid_branch (const char *string,
                                  GError **error);

char * flatpak_make_valid_id_prefix (const char *orig_id);
gboolean flatpak_id_has_subref_suffix (const char *id);

char **flatpak_decompose_ref (const char *ref,
                              GError    **error);

char * flatpak_compose_ref (gboolean    app,
                            const char *name,
                            const char *branch,
                            const char *arch,
                            GError    **error);

char * flatpak_build_untyped_ref (const char *runtime,
                                  const char *branch,
                                  const char *arch);
char * flatpak_build_runtime_ref (const char *runtime,
                                  const char *branch,
                                  const char *arch);
char * flatpak_build_app_ref (const char *app,
                              const char *branch,
                              const char *arch);

#if !GLIB_CHECK_VERSION (2, 40, 0)
static inline gboolean
g_key_file_save_to_file (GKeyFile    *key_file,
                         const gchar *filename,
                         GError     **error)
{
  gchar *contents;
  gboolean success;
  gsize length;

  contents = g_key_file_to_data (key_file, &length, NULL);
  success = g_file_set_contents (filename, contents, length, error);
  g_free (contents);

  return success;
}
#endif

/* Returns the first string in subset that is not in strv */
static inline const gchar *
g_strv_subset (const gchar * const *strv,
               const gchar * const *subset)
{
  int i;

  for (i = 0; subset[i]; i++)
    {
      const char *key;

      key = subset[i];
      if (!g_strv_contains (strv, key))
        return key;
    }

  return NULL;
}

static inline void
flatpak_auto_unlock_helper (GMutex **mutex)
{
  if (*mutex)
    g_mutex_unlock (*mutex);
}

static inline GMutex *
flatpak_auto_lock_helper (GMutex *mutex)
{
  if (mutex)
    g_mutex_lock (mutex);
  return mutex;
}

gint flatpak_mkstempat (int    dir_fd,
                        gchar *tmpl,
                        int    flags,
                        int    mode);

char * flatpak_quote_argv (const char *argv[]);
gboolean flatpak_file_arg_has_suffix (const char *arg, const char *suffix);

gboolean            flatpak_spawn (GFile       *dir,
                                   char       **output,
                                   GSubprocessFlags flags,
                                   GError     **error,
                                   const gchar *argv0,
                                   va_list      args);

gboolean            flatpak_spawnv (GFile                *dir,
                                    char                **output,
                                    GSubprocessFlags      flags,
                                    GError              **error,
                                    const gchar * const  *argv);

const char *flatpak_file_get_path_cached (GFile *file);

GFile *flatpak_build_file_va (GFile *base,
                              va_list args);
GFile *flatpak_build_file (GFile *base, ...) G_GNUC_NULL_TERMINATED;

gboolean flatpak_openat_noatime (int            dfd,
                                 const char    *name,
                                 int           *ret_fd,
                                 GCancellable  *cancellable,
                                 GError       **error);

typedef enum {
  FLATPAK_CP_FLAGS_NONE = 0,
  FLATPAK_CP_FLAGS_MERGE = 1<<0,
  FLATPAK_CP_FLAGS_NO_CHOWN = 1<<1,
  FLATPAK_CP_FLAGS_MOVE = 1<<2,
} FlatpakCpFlags;

gboolean   flatpak_cp_a (GFile         *src,
                         GFile         *dest,
                         FlatpakCpFlags flags,
                         GCancellable  *cancellable,
                         GError       **error);

gboolean flatpak_zero_mtime (int parent_dfd,
                             const char *rel_path,
                             GCancellable  *cancellable,
                             GError       **error);

gboolean flatpak_mkdir_p (GFile         *dir,
                          GCancellable  *cancellable,
                          GError       **error);

gboolean flatpak_rm_rf (GFile         *dir,
                        GCancellable  *cancellable,
                        GError       **error);

gboolean flatpak_open_in_tmpdir_at (int                tmpdir_fd,
                                    int                mode,
                                    char              *tmpl,
                                    GOutputStream    **out_stream,
                                    GCancellable      *cancellable,
                                    GError           **error);

static inline void
flatpak_temp_dir_destroy (void *p)
{
  GFile *dir = p;

  if (dir)
    {
      flatpak_rm_rf (dir, NULL, NULL);
      g_object_unref (dir);
    }
}

typedef GFile FlatpakTempDir;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakTempDir, flatpak_temp_dir_destroy)


#define AUTOLOCK(name) G_GNUC_UNUSED __attribute__((cleanup (flatpak_auto_unlock_helper))) GMutex * G_PASTE (auto_unlock, __LINE__) = flatpak_auto_lock_helper (&G_LOCK_NAME (name))

/* OSTREE_CHECK_VERSION was added immediately after the 2017.3 release */
#ifndef OSTREE_CHECK_VERSION
#define OSTREE_CHECK_VERSION(year, minor) (0)
#endif
/* Cleanups are always exported in 2017.4, and some git releases between 2017.3 and 2017.4.
   We actually check against 2017.3 so that we work on the git releases *after* 2017.3
   which is safe, because the real OSTREE_CHECK_VERSION macro was added after 2017.3
   too. */
#if !OSTREE_CHECK_VERSION(2017, 3)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (OstreeRepo, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (OstreeMutableTree, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (OstreeAsyncProgress, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (OstreeGpgVerifyResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (OstreeRepoCommitModifier, ostree_repo_commit_modifier_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (OstreeRepoDevInoCache, ostree_repo_devino_cache_unref)
#endif

#ifndef SOUP_AUTOCLEANUPS_H
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupSession, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupMessage, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupRequest, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupRequestHTTP, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SoupURI, soup_uri_free)
#endif

#if !JSON_CHECK_VERSION(1,1,2)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonArray, json_array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonBuilder, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonGenerator, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonNode, json_node_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonObject, json_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonParser, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonPath, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (JsonReader, g_object_unref)
#endif

#if !GLIB_CHECK_VERSION(2, 43, 4)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUnixFDList, g_object_unref)
#endif

gboolean flatpak_allocate_tmpdir (int           tmpdir_dfd,
                                  const char   *tmpdir_relpath,
                                  const char   *tmpdir_prefix,
                                  char        **tmpdir_name_out,
                                  int          *tmpdir_fd_out,
                                  GLnxLockFile *file_lock_out,
                                  gboolean     *reusing_dir_out,
                                  GCancellable *cancellable,
                                  GError      **error);


SoupSession * flatpak_create_soup_session (const char *user_agent);
GBytes * flatpak_load_http_uri (SoupSession *soup_session,
                                const char   *uri,
                                const char   *etag,
                                char        **out_etag,
                                FlatpakLoadUriProgress progress,
                                gpointer      user_data,
                                GCancellable *cancellable,
                                GError      **error);
gboolean flatpak_download_http_uri (SoupSession *soup_session,
                                    const char   *uri,
                                    GOutputStream *out,
                                    FlatpakLoadUriProgress progress,
                                    gpointer      user_data,
                                    GCancellable *cancellable,
                                    GError      **error);

typedef struct FlatpakContext FlatpakContext;

FlatpakContext *flatpak_context_new (void);
void           flatpak_context_free (FlatpakContext *context);
GOptionGroup  *flatpak_context_get_options (FlatpakContext *context);
void           flatpak_context_to_args (FlatpakContext *context,
                                        GPtrArray *args);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakContext, flatpak_context_free)


#endif /* __FLATPAK_UTILS_H__ */
