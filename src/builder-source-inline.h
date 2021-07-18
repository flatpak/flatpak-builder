#ifndef __BUILDER_SOURCE_INLINE_H__
#define __BUILDER_SOURCE_INLINE_H__

#include "builder-source.h"

G_BEGIN_DECLS

typedef struct BuilderSourceInline BuilderSourceInline;

#define BUILDER_TYPE_SOURCE_INLINE (builder_source_inline_get_type ())
#define BUILDER_SOURCE_INLINE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), BUILDER_TYPE_SOURCE_INLINE, BuilderSourceInline))
#define BUILDER_IS_SOURCE_INLINE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BUILDER_TYPE_SOURCE_INLINE))

GType builder_source_inline_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (BuilderSourceInline, g_object_unref)

G_END_DECLS

#endif /* __BUILDER_SOURCE_INLINE_H__ */
