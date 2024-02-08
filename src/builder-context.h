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

#ifndef __BUILDER_CONTEXT_H__
#define __BUILDER_CONTEXT_H__

#include <gio/gio.h>
#include <curl/curl.h>
#include "builder-options.h"
#include "builder-utils.h"
#include "builder-sdk-config.h"

G_BEGIN_DECLS

/* Same as SOUP_HTTP_URI_FLAGS, means all possible flags for http uris */

#if GLIB_CHECK_VERSION (2, 68, 0)
#define CONTEXT_HTTP_URI_FLAGS (G_URI_FLAGS_HAS_PASSWORD | G_URI_FLAGS_ENCODED_PATH | G_URI_FLAGS_ENCODED_QUERY | G_URI_FLAGS_ENCODED_FRAGMENT | G_URI_FLAGS_SCHEME_NORMALIZE)
#else
/* GLib 2.66 didn't support scheme-based normalization */
#define CONTEXT_HTTP_URI_FLAGS (G_URI_FLAGS_HAS_PASSWORD | G_URI_FLAGS_ENCODED_PATH | G_URI_FLAGS_ENCODED_QUERY | G_URI_FLAGS_ENCODED_FRAGMENT)
#endif

/* BuilderContext defined in builder-cache.h to fix include loop */

#define BUILDER_TYPE_CONTEXT (builder_context_get_type ())
#define BUILDER_CONTEXT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), BUILDER_TYPE_CONTEXT, BuilderContext))
#define BUILDER_IS_CONTEXT(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BUILDER_TYPE_CONTEXT))

GType builder_context_get_type (void);

GFile *         builder_context_get_app_dir (BuilderContext *self);
GFile *         builder_context_get_app_dir_raw (BuilderContext *self);
GFile *         builder_context_get_run_dir (BuilderContext *self);
GFile *         builder_context_get_base_dir (BuilderContext *self);
void            builder_context_set_base_dir (BuilderContext *self,
                                              GFile          *base_dir);
GFile *         builder_context_get_state_dir (BuilderContext *self);
GFile *         builder_context_get_cache_dir (BuilderContext *self);
GFile *         builder_context_get_build_dir (BuilderContext *self);
GFile *         builder_context_allocate_build_subdir (BuilderContext *self,
                                                       const char *name,
                                                       GError **error);
GFile *         builder_context_get_ccache_dir (BuilderContext *self);
GFile *         builder_context_get_download_dir (BuilderContext *self);
GPtrArray *     builder_context_get_sources_dirs (BuilderContext *self);
void            builder_context_set_sources_dirs (BuilderContext *self,
                                                  GPtrArray      *sources_dirs);
GFile *         builder_context_find_in_sources_dirs (BuilderContext *self,
                                                      ...) G_GNUC_NULL_TERMINATED;
GFile *         builder_context_find_in_sources_dirs_va (BuilderContext *self,
                                                         va_list args);
GPtrArray *     builder_context_get_sources_urls (BuilderContext *self);
void            builder_context_set_sources_urls (BuilderContext *self,
                                                  GPtrArray      *sources_urls);
gboolean        builder_context_download_uri (BuilderContext *self,
                                              const char     *url,
                                              const char    **mirrors,
                                              const char     *http_referer,
                                              gboolean        disable_http_decompression,
                                              GFile          *dest,
                                              const char     *checksums[BUILDER_CHECKSUMS_LEN],
                                              GChecksumType   checksums_type[BUILDER_CHECKSUMS_LEN],
                                              GError        **error);
CURL *          builder_context_get_curl_session (BuilderContext *self);
const char *    builder_context_get_arch (BuilderContext *self);
void            builder_context_set_arch (BuilderContext *self,
                                          const char     *arch);
const char *    builder_context_get_default_branch (BuilderContext *self);
void            builder_context_set_default_branch (BuilderContext *self,
                                                    const char     *default_branch);
gint64          builder_context_get_source_date_epoch (BuilderContext *self);
void            builder_context_set_source_date_epoch (BuilderContext *self,
                                                       gint64 source_date_epoch);
const char *    builder_context_get_stop_at (BuilderContext *self);
void            builder_context_set_stop_at (BuilderContext *self,
                                             const char     *module);
int             builder_context_get_jobs (BuilderContext *self);
void            builder_context_set_jobs (BuilderContext *self,
                                          int n_jobs);
void            builder_context_set_keep_build_dirs (BuilderContext *self,
                                                     gboolean        keep_build_dirs);
gboolean        builder_context_get_delete_build_dirs (BuilderContext *self);
void            builder_context_set_delete_build_dirs (BuilderContext *self,
                                                       gboolean        delete_build_dirs);
gboolean        builder_context_get_keep_build_dirs (BuilderContext *self);
void            builder_context_set_sandboxed (BuilderContext *self,
                                               gboolean        sandboxed);
gboolean        builder_context_ensure_file_sandboxed (BuilderContext *self,
                                                       GFile          *file,
                                                       GError        **error);
gboolean        builder_context_ensure_parent_dir_sandboxed (BuilderContext *self,
                                                             GFile          *file,
                                                             GError        **error);
gboolean        builder_context_get_sandboxed (BuilderContext *self);
void            builder_context_set_global_cleanup (BuilderContext *self,
                                                    const char    **cleanup);
const char **   builder_context_get_global_cleanup (BuilderContext *self);
void            builder_context_set_global_cleanup_platform (BuilderContext *self,
                                                             const char    **cleanup);
const char **   builder_context_get_global_cleanup_platform (BuilderContext *self);
BuilderOptions *builder_context_get_options (BuilderContext *self);
void            builder_context_set_options (BuilderContext *self,
                                             BuilderOptions *option);
gboolean        builder_context_get_build_runtime (BuilderContext *self);
void            builder_context_set_build_runtime (BuilderContext *self,
                                                   gboolean        build_runtime);
gboolean        builder_context_get_build_extension (BuilderContext *self);
void            builder_context_set_build_extension (BuilderContext *self,
                                                     gboolean        build_extension);
gboolean        builder_context_get_separate_locales (BuilderContext *self);
void            builder_context_set_separate_locales (BuilderContext *self,
                                                      gboolean        separate_locales);
void            builder_context_set_bundle_sources (BuilderContext *self,
                                                    gboolean        bundle_sources);
gboolean        builder_context_get_bundle_sources (BuilderContext *self);
gboolean        builder_context_get_rebuild_on_sdk_change (BuilderContext *self);
void            builder_context_set_rebuild_on_sdk_change (BuilderContext *self,
                                                           gboolean        rebuild_on_sdk_change);
char *          builder_context_get_checksum_for (BuilderContext *self,
                                                  const char *name);
gboolean        builder_context_set_checksum_for (BuilderContext  *self,
                                                  const char      *name,
                                                  const char      *checksum,
                                                  GError         **error);

BuilderContext *builder_context_new (GFile *run_dir,
                                     GFile *app_dir,
                                     const char *state_subdir);
gboolean        builder_context_set_enable_ccache (BuilderContext *self,
                                                   gboolean        enabled,
                                                   GError        **error);
gboolean        builder_context_enable_rofiles (BuilderContext *self,
                                                GError        **error);
gboolean        builder_context_disable_rofiles (BuilderContext *self,
                                                 GError        **error);
gboolean        builder_context_get_rofiles_active (BuilderContext *self);
gboolean        builder_context_get_use_rofiles (BuilderContext *self);
void            builder_context_set_use_rofiles (BuilderContext *self,
                                                 gboolean use_rofiles);
gboolean        builder_context_get_run_tests (BuilderContext *self);
void            builder_context_set_run_tests (BuilderContext *self,
                                               gboolean run_tests);
void            builder_context_set_no_shallow_clone (BuilderContext *self,
                                                      gboolean        no_shallow_clone);
gboolean        builder_context_get_no_shallow_clone (BuilderContext *self);
char **         builder_context_extend_env_pre (BuilderContext *self,
                                                 char          **envp);
char **         builder_context_extend_env_post (BuilderContext *self,
                                                 char          **envp);

gboolean        builder_context_load_sdk_config (BuilderContext       *self,
                                                 const char           *sdk_path,
                                                 GError              **error);

void            builder_context_set_opt_export_only (BuilderContext *self,
                                                     gboolean opt_export_only);

gboolean        builder_context_get_opt_export_only (BuilderContext *self);

void            builder_context_set_opt_mirror_screenshots_url (BuilderContext *self,
                                                                const char *opt_mirror_screenshots_url);

const char *    builder_context_get_opt_mirror_screenshots_url (BuilderContext *self);

BuilderSdkConfig * builder_context_get_sdk_config (BuilderContext *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (BuilderContext, g_object_unref)

G_END_DECLS

#endif /* __BUILDER_CONTEXT_H__ */
