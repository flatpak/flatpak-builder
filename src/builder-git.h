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

#ifndef __BUILDER_GIT_H__
#define __BUILDER_GIT_H__

#include "builder-context.h"

G_BEGIN_DECLS

typedef enum {
  FLATPAK_GIT_MIRROR_FLAGS_UPDATE = 1 << 0,
  FLATPAK_GIT_MIRROR_FLAGS_MIRROR_SUBMODULES = 1 << 1,
  FLATPAK_GIT_MIRROR_FLAGS_DISABLE_FSCK = 1 << 2,
  FLATPAK_GIT_MIRROR_FLAGS_DISABLE_SHALLOW = 1 << 3,
  FLATPAK_GIT_MIRROR_FLAGS_WILL_FETCH_FROM = 1 << 4,
} FlatpakGitMirrorFlags;

gboolean builder_git_mirror_repo        (const char      *repo_location,
                                         const char      *destination_path,
                                         FlatpakGitMirrorFlags flags,
                                         const char      *ref,
                                         BuilderContext  *context,
                                         GError         **error);
char *   builder_git_get_current_commit (const char      *repo_location,
                                         const char      *branch,
                                         gboolean        ensure_commit,
                                         BuilderContext  *context,
                                         GError         **error);
gboolean builder_git_checkout           (const char      *repo_location,
                                         const char      *branch,
                                         GFile           *dest,
                                         BuilderContext  *context,
                                         FlatpakGitMirrorFlags mirror_flags,
                                         GError         **error);
gboolean builder_git_shallow_mirror_ref (const char     *repo_location,
                                         const char     *destination_path,
                                         FlatpakGitMirrorFlags flags,
                                         const char     *ref,
                                         BuilderContext *context,
                                         GError        **error);
char *   builder_git_get_default_branch (const char *repo_location);

G_END_DECLS

#endif /* __BUILDER_GIT_H__ */
