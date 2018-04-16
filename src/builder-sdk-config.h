/*
 * Copyright © 2018 Codethink Limited
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
 *       Valentin David <valentin.david@codethink.co.uk>
 */

#ifndef __BUILDER_SDK_CONFIG_H__
#define __BUILDER_SDK_CONFIG_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct BuilderSdkConfig BuilderSdkConfig;

#define BUILDER_TYPE_SDK_CONFIG (builder_sdk_config_get_type ())
#define BUILDER_SDK_CONFIG(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), BUILDER_TYPE_SDK_CONFIG, BuilderSdkConfig))

GType builder_sdk_config_get_type (void);

const char *      builder_sdk_config_get_libdir   (BuilderSdkConfig  *self);
const char *      builder_sdk_config_get_cppflags (BuilderSdkConfig  *self);
const char *      builder_sdk_config_get_cflags   (BuilderSdkConfig  *self);
const char *      builder_sdk_config_get_cxxflags (BuilderSdkConfig  *self);
const char *      builder_sdk_config_get_ldflags  (BuilderSdkConfig  *self);

BuilderSdkConfig *builder_sdk_config_from_file    (GFile             *file,
                                                   GError           **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (BuilderSdkConfig, g_object_unref)

G_END_DECLS

#endif /* __BUILDER_SDK_CONFIG_H__ */
