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

#ifndef __BUILDER_SOURCE_SVN_H__
#define __BUILDER_SOURCE_SVN_H__

#include "builder-source.h"

G_BEGIN_DECLS

typedef struct BuilderSourceSvn BuilderSourceSvn;

#define BUILDER_TYPE_SOURCE_SVN (builder_source_svn_get_type ())
#define BUILDER_SOURCE_SVN(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), BUILDER_TYPE_SOURCE_SVN, BuilderSourceSvn))
#define BUILDER_IS_SOURCE_SVN(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BUILDER_TYPE_SOURCE_SVN))

GType builder_source_svn_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (BuilderSourceSvn, g_object_unref)

G_END_DECLS

#endif /* __BUILDER_SOURCE_SVN_H__ */
