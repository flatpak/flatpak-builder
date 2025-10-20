/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

#pragma once

/***
  This file is part of systemd.

  Copyright 2011 Lennart Poettering
  Copyright 2015 Colin Walters <walters@verbum.org>
  SPDX-License-Identifier: LGPL-2.1-or-later

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include "glnx-backport-autoptr.h"

typedef struct GLnxLockFile {
        gboolean initialized;
        int dfd;
        char *path;
        int fd;
        int operation;
} GLnxLockFile;

gboolean glnx_make_lock_file(int dfd, const char *p, int operation, GLnxLockFile *ret, GError **error);
void glnx_release_lock_file(GLnxLockFile *f);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(GLnxLockFile, glnx_release_lock_file)
