#include "config.h"

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/statfs.h>

#include "builder-flatpak-utils.h"
#include "builder-utils.h"
#include "builder-source-inline.h"

struct BuilderSourceInline
{
  BuilderSource parent;

  char         *contents;
  gboolean      base64;
  char         *dest_filename;
};

typedef struct
{
  BuilderSourceClass parent_class;
} BuilderSourceInlineClass;

G_DEFINE_TYPE (BuilderSourceInline, builder_source_inline, BUILDER_TYPE_SOURCE);

enum {
  PROP_0,
  PROP_CONTENTS,
  PROP_BASE64,
  PROP_DEST_FILENAME,
  LAST_PROP
};

static void
builder_source_inline_finalize (GObject *object)
{
  BuilderSourceInline *self = (BuilderSourceInline *) object;

  g_free (self->contents);
  g_free (self->dest_filename);

  G_OBJECT_CLASS (builder_source_inline_parent_class)->finalize (object);
}

static void
builder_source_inline_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  BuilderSourceInline *self = BUILDER_SOURCE_INLINE (object);

  switch (prop_id)
    {
    case PROP_CONTENTS:
      g_value_set_string (value, self->contents);
      break;

    case PROP_BASE64:
      g_value_set_boolean (value, self->base64);
      break;

    case PROP_DEST_FILENAME:
      g_value_set_string (value, self->dest_filename);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
builder_source_inline_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  BuilderSourceInline *self = BUILDER_SOURCE_INLINE (object);

  switch (prop_id)
    {
    case PROP_CONTENTS:
      g_free (self->contents);
      self->contents = g_value_dup_string (value);
      break;

    case PROP_BASE64:
      self->base64 = g_value_get_boolean (value);
      break;

    case PROP_DEST_FILENAME:
      g_free (self->dest_filename);
      self->dest_filename = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static gboolean
builder_source_inline_validate (BuilderSource  *source,
                                GError        **error)
{
  BuilderSourceInline *self = BUILDER_SOURCE_INLINE (source);

  if (self->dest_filename != NULL &&
      strchr (self->dest_filename, '/') != NULL)
    return flatpak_fail (error, "No slashes allowed in dest-filename, use dest property for directory");

  return TRUE;
}

static gboolean
builder_source_inline_download (BuilderSource  *source,
                                gboolean        update_vcs,
                                BuilderContext *context,
                                GError        **error)
{
  return TRUE;
}

static gboolean
builder_source_inline_extract (BuilderSource  *source,
                               GFile          *dest,
                               GFile          *source_dir,
                               BuilderOptions *build_options,
                               BuilderContext *context,
                               GError        **error)
{
  BuilderSourceInline *self = BUILDER_SOURCE_INLINE (source);

  g_autoptr(GFile) dest_file = NULL;
  g_autoptr(GFileOutputStream) out = NULL;
  gsize size = 0;

  if (self->dest_filename == NULL)
  {
    g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED, "Property \"dest-filename\" must be set");
    return FALSE;
  }

  dest_file = g_file_get_child (dest, self->dest_filename);

  out = g_file_replace (dest_file, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION, NULL, error);
  if (out == NULL)
    return FALSE;

  if (self->contents == NULL)
    return TRUE;

  if (self->base64)
  {
    g_autofree guchar *contents = NULL;
    contents = g_base64_decode (self->contents, &size);
    if (!g_output_stream_write_all (G_OUTPUT_STREAM (out), contents, size, NULL, NULL, error))
      return FALSE;
  }
  else
  {
    size = strlen (self->contents);
    if (!g_output_stream_write_all (G_OUTPUT_STREAM (out), self->contents, size, NULL, NULL, error))
      return FALSE;
  }

  return TRUE;
}

static gboolean
builder_source_inline_bundle (BuilderSource  *source,
                              BuilderContext *context,
                              GError        **error)
{
  /* no need to bundle anything here as this part
     can be reconstructed from the manifest */
  return TRUE;
}

static void
builder_source_inline_checksum (BuilderSource  *source,
                                BuilderCache   *cache,
                                BuilderContext *context)
{
  BuilderSourceInline *self = BUILDER_SOURCE_INLINE (source);

  builder_cache_checksum_str (cache, self->contents);
  builder_cache_checksum_str (cache, self->dest_filename);
}

static void
builder_source_inline_class_init (BuilderSourceInlineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  BuilderSourceClass *source_class = BUILDER_SOURCE_CLASS (klass);

  object_class->finalize = builder_source_inline_finalize;
  object_class->get_property = builder_source_inline_get_property;
  object_class->set_property = builder_source_inline_set_property;

  source_class->download = builder_source_inline_download;
  source_class->extract = builder_source_inline_extract;
  source_class->bundle = builder_source_inline_bundle;
  source_class->checksum = builder_source_inline_checksum;
  source_class->validate = builder_source_inline_validate;

  g_object_class_install_property (object_class,
                                   PROP_CONTENTS,
                                   g_param_spec_string ("contents",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_BASE64,
                                   g_param_spec_boolean ("base64",
                                                         "",
                                                         "",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
  g_object_class_install_property (object_class,
                                   PROP_DEST_FILENAME,
                                   g_param_spec_string ("dest-filename",
                                                        "",
                                                        "",
                                                        NULL,
                                                        G_PARAM_READWRITE));
}

static void
builder_source_inline_init (BuilderSourceInline *self)
{
}
