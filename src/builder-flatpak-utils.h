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

#include "libglnx.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <ostree.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>

typedef enum {
  FLATPAK_HOST_COMMAND_FLAGS_CLEAR_ENV = 1 << 0,
  FLATPAK_HOST_COMMAND_FLAGS_WATCH_BUS = 1 << 1,
} FlatpakHostCommandFlags;

typedef void (*FlatpakLoadUriProgress) (guint64 downloaded_bytes,
                                        gpointer user_data);


#define FLATPAK_METADATA_GROUP_PREFIX_EXTENSION "Extension "
#define FLATPAK_METADATA_KEY_ADD_LD_PATH "add-ld-path"
#define FLATPAK_METADATA_KEY_AUTODELETE "autodelete"
#define FLATPAK_METADATA_KEY_DIRECTORY "directory"
#define FLATPAK_METADATA_KEY_DOWNLOAD_IF "download-if"
#define FLATPAK_METADATA_KEY_ENABLE_IF "enable-if"
#define FLATPAK_METADATA_KEY_AUTOPRUNE_UNLESS "autoprune-unless"
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

#define FLATPAK_METADATA_GROUP_BUILD "Build"
#define FLATPAK_METADATA_KEY_BUILD_EXTENSIONS "built-extensions"

/* https://github.com/GNOME/libglnx/pull/38
 * Note by using #define rather than wrapping via a static inline, we
 * don't have to re-define attributes like G_GNUC_PRINTF.
 */
#define flatpak_fail glnx_throw

const char * flatpak_path_match_prefix (const char *pattern,
                                        const char *path);

gboolean flatpak_is_in_sandbox (void);

const char * flatpak_get_arch (void);

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


gboolean flatpak_has_name_prefix (const char *string,
                                  const char *name);

char * flatpak_make_valid_id_prefix (const char *orig_id);

char * flatpak_compose_ref (gboolean    app,
                            const char *name,
                            const char *branch,
                            const char *arch);

char * flatpak_build_untyped_ref (const char *runtime,
                                  const char *branch,
                                  const char *arch);
char * flatpak_build_runtime_ref (const char *runtime,
                                  const char *branch,
                                  const char *arch);
char * flatpak_build_app_ref (const char *app,
                              const char *branch,
                              const char *arch);

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

char * flatpak_quote_argv (const char *argv[]);

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
                                    const gchar * const  *argv,
                                    const gchar * const  *unresolved_argv);

const char *flatpak_file_get_path_cached (GFile *file);
gboolean flatpak_file_query_exists_nofollow (GFile *file);

GFile *flatpak_build_file_va (GFile *base,
                              va_list args);
GFile *flatpak_build_file (GFile *base, ...) G_GNUC_NULL_TERMINATED;

gboolean flatpak_file_rename (GFile *from,
                              GFile *to,
                              GCancellable  *cancellable,
                              GError       **error);

GFile * flatpak_canonicalize_file (GFile *file,
                                   GError **error);
/* NOTE: This requires both files to exist */
gboolean flatpak_file_is_in (GFile *file,
                             GFile *toplevel);

typedef enum {
  FLATPAK_CP_FLAGS_NONE = 0,
  FLATPAK_CP_FLAGS_MERGE = 1<<0,
  FLATPAK_CP_FLAGS_NO_CHOWN = 1<<1,
  FLATPAK_CP_FLAGS_MOVE = 1<<2,
} FlatpakCpFlags;

gboolean   flatpak_cp_a (GFile         *src,
                         GFile         *dest,
                         GFile         *keep_in_toplevel,
                         FlatpakCpFlags flags,
                         GPtrArray     *skip_files,
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

static inline void
builder_object_list_destroy (void *p)
{
  GList *list = p;

  g_list_free_full (list, g_object_unref);
}

typedef GList BuilderObjectList;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (BuilderObjectList, builder_object_list_destroy)

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

gboolean flatpak_allocate_tmpdir (int           tmpdir_dfd,
                                  const char   *tmpdir_relpath,
                                  const char   *tmpdir_prefix,
                                  char        **tmpdir_name_out,
                                  int          *tmpdir_fd_out,
                                  GLnxLockFile *file_lock_out,
                                  gboolean     *reusing_dir_out,
                                  GCancellable *cancellable,
                                  GError      **error);


CURL * flatpak_create_curl_session (const char *user_agent);

typedef struct FlatpakContext FlatpakContext;

FlatpakContext *flatpak_context_new (void);
void           flatpak_context_free (FlatpakContext *context);
GOptionGroup  *flatpak_context_get_options (FlatpakContext *context);
void           flatpak_context_to_args (FlatpakContext *context,
                                        GPtrArray *args);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FlatpakContext, flatpak_context_free)


#endif /* __FLATPAK_UTILS_H__ */
