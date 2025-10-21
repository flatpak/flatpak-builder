/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2014,2015 Colin Walters <walters@verbum.org>.
 * SPDX-License-Identifier: LGPL-2.0-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#pragma once

#include <glnx-backport-autocleanups.h>
#include <glnx-macros.h>
#include <glnx-errors.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

G_BEGIN_DECLS
 
/**
 * glnx_dirfd_canonicalize:
 * @fd: A directory file descriptor
 *
 * It's often convenient in programs to use `-1` for "unassigned fd",
 * and also because gobject-introspection doesn't support `AT_FDCWD`,
 * libglnx honors `-1` to mean `AT_FDCWD`.  This small inline function
 * canonicalizes `-1 -> AT_FDCWD`.
 */
static inline int
glnx_dirfd_canonicalize (int fd)
{
  if (fd == -1)
    return AT_FDCWD;
  return fd;
}

struct GLnxDirFdIterator {
  gboolean initialized;
  int fd;
  gpointer padding_data[4];
};

typedef struct GLnxDirFdIterator GLnxDirFdIterator;
gboolean glnx_dirfd_iterator_init_at (int dfd, const char *path,
                                    gboolean follow,
                                    GLnxDirFdIterator *dfd_iter, GError **error);
gboolean glnx_dirfd_iterator_init_take_fd (int *dfd, GLnxDirFdIterator *dfd_iter, GError **error);
gboolean glnx_dirfd_iterator_next_dent (GLnxDirFdIterator  *dfd_iter,
                                        struct dirent     **out_dent,
                                        GCancellable       *cancellable,
                                        GError            **error);
gboolean glnx_dirfd_iterator_next_dent_ensure_dtype (GLnxDirFdIterator  *dfd_iter,
                                                     struct dirent     **out_dent,
                                                     GCancellable       *cancellable,
                                                     GError            **error);
void glnx_dirfd_iterator_rewind (GLnxDirFdIterator  *dfd_iter);
void glnx_dirfd_iterator_clear (GLnxDirFdIterator *dfd_iter);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(GLnxDirFdIterator, glnx_dirfd_iterator_clear)

int glnx_opendirat_with_errno (int           dfd,
                               const char   *path,
                               gboolean      follow);

gboolean glnx_opendirat (int             dfd,
                         const char     *path,
                         gboolean        follow,
                         int            *out_fd,
                         GError        **error);

char *glnx_fdrel_abspath (int         dfd,
                          const char *path);

void glnx_gen_temp_name (gchar *tmpl);

/**
 * glnx_ensure_dir:
 * @dfd: directory fd
 * @path: Directory path
 * @mode: Mode
 * @error: Return location for a #GError, or %NULL
 *
 * Wrapper around mkdirat() which adds #GError support, ensures that
 * it retries on %EINTR, and also ignores `EEXIST`.
 *
 * See also `glnx_shutil_mkdir_p_at()` for recursive handling.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
static inline gboolean
glnx_ensure_dir (int           dfd,
                 const char   *path,
                 mode_t        mode,
                 GError      **error)
{
  if (TEMP_FAILURE_RETRY (mkdirat (dfd, path, mode)) != 0)
    {
      if (G_UNLIKELY (errno != EEXIST))
        return glnx_throw_errno_prefix (error, "mkdirat(%s)", path);
    }
  return TRUE;
}

typedef struct {
  gboolean initialized;
  int src_dfd;
  int fd;
  char *path;
} GLnxTmpDir;
gboolean glnx_tmpdir_delete (GLnxTmpDir *tmpf, GCancellable *cancellable, GError **error);
void glnx_tmpdir_unset (GLnxTmpDir *tmpf);
static inline void
glnx_tmpdir_cleanup (GLnxTmpDir *tmpf)
{
  (void)glnx_tmpdir_delete (tmpf, NULL, NULL);
}
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(GLnxTmpDir, glnx_tmpdir_cleanup)

gboolean glnx_mkdtempat (int dfd, const char *tmpl, int mode,
                         GLnxTmpDir *out_tmpdir, GError **error);

gboolean glnx_mkdtemp (const char *tmpl, int      mode,
                       GLnxTmpDir *out_tmpdir, GError **error);

G_END_DECLS
