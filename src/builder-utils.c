/* builder-utils.c
 *
 * Copyright (C) 2015 Red Hat, Inc
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <stdlib.h>
#include <libelf.h>
#include <gelf.h>
#include <dwarf.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <stdio.h>

#include <string.h>

#include <glib-unix.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixinputstream.h>

#include "builder-flatpak-utils.h"
#include "builder-utils.h"

G_DEFINE_QUARK (builder-curl-error, builder_curl_error)
G_DEFINE_QUARK (builder-yaml-parse-error, builder_yaml_parse_error)

#ifdef FLATPAK_BUILDER_ENABLE_YAML
#include <yaml.h>

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (yaml_parser_t, yaml_parser_delete)
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (yaml_document_t, yaml_document_delete)
#endif

char *
builder_uri_to_filename (const char *uri)
{
  GString *s;
  const char *p;

  s = g_string_new ("");

  for (p = uri; *p != 0; p++)
    {
      if (*p == '/' || *p == ':')
        {
          while (p[1] == '/' || p[1] == ':')
            p++;
          g_string_append_c (s, '_');
        }
      else
        {
          g_string_append_c (s, *p);
        }
    }

  return g_string_free (s, FALSE);
}

static const char *
inplace_basename (const char *path)
{
  const char *last_slash;

  last_slash = strrchr (path, '/');
  if (last_slash)
    path = last_slash + 1;

  return path;
}


/* Adds all matches of path to prefix. There can be multiple, because
 * e.g matching "a/b/c" against "/a" matches both "a/b" and "a/b/c"
 *
 * If pattern starts with a slash, then match on the entire
 * path, otherwise just the basename.
 */
void
flatpak_collect_matches_for_path_pattern (const char *path,
                                          const char *pattern,
                                          const char *add_prefix,
                                          GHashTable *to_remove_ht)
{
  const char *rest;

  if (pattern[0] != '/')
    {
      rest = flatpak_path_match_prefix (pattern, inplace_basename (path));
      if (rest != NULL)
        g_hash_table_insert (to_remove_ht, g_strconcat (add_prefix ? add_prefix : "", path, NULL), GINT_TO_POINTER (1));
    }
  else
    {
      /* Absolute pathname match. This can actually match multiple
       * files, as a prefix match should remove all files below that
       * (in this module) */

      rest = flatpak_path_match_prefix (pattern, path);
      while (rest != NULL)
        {
          const char *slash;
          g_autofree char *prefix = g_strndup (path, rest - path);
          g_hash_table_insert (to_remove_ht, g_strconcat (add_prefix ? add_prefix : "", prefix, NULL), GINT_TO_POINTER (1));
          while (*rest == '/')
            rest++;
          if (*rest == 0)
            break;
          slash = strchr (rest, '/');
          rest = slash ? slash : rest + strlen (rest);
        }
    }


}

gboolean
flatpak_matches_path_pattern (const char *path,
                              const char *pattern)
{
  if (pattern[0] != '/')
    path = inplace_basename (path);

  return flatpak_path_match_prefix (pattern, path) != NULL;
}

gboolean
strip (GError **error,
       ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (NULL, NULL, 0, error, "strip", ap);
  va_end (ap);

  return res;
}

gboolean
eu_strip (GError **error,
          ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (NULL, NULL, 0, error, "eu-strip", ap);
  va_end (ap);

  return res;
}

gboolean
eu_elfcompress (GError **error,
                ...)
{
  gboolean res;
  va_list ap;

  va_start (ap, error);
  res = flatpak_spawn (NULL, NULL, 0, error, "eu-elfcompress", ap);
  va_end (ap);

  return res;
}


static gboolean
elf_has_symtab (Elf *elf)
{
  Elf_Scn *scn;
  GElf_Shdr shdr;

  scn = NULL;
  while ((scn = elf_nextscn (elf, scn)) != NULL)
    {
      if (gelf_getshdr (scn, &shdr) == NULL)
        continue;

      if (shdr.sh_type != SHT_SYMTAB)
        continue;

      return TRUE;
    }

  return FALSE;
}

gboolean
is_elf_file (const char *path,
             gboolean   *is_shared,
             gboolean   *is_stripped)
{
  g_autofree char *filename = g_path_get_basename (path);
  struct stat stbuf;

  if (lstat (path, &stbuf) == -1)
    return FALSE;

  if (!S_ISREG (stbuf.st_mode))
    return FALSE;

  /* Self-extracting .zip files can be ELF-executables, but shouldn't be
     treated like them - for example, stripping them breaks their
     operation */
  if (g_str_has_suffix (filename, ".zip"))
    return FALSE;

  if ((strstr (filename, ".so.") != NULL ||
       g_str_has_suffix (filename, ".so")) ||
      (stbuf.st_mode & 0111) != 0)
    {
      glnx_fd_close int fd = -1;

      fd = open (path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
      if (fd >= 0)
        {
          Elf *elf;
          GElf_Ehdr ehdr;
          gboolean res = FALSE;

          if (elf_version (EV_CURRENT) == EV_NONE )
            return FALSE;

          elf = elf_begin (fd, ELF_C_READ, NULL);
          if (elf == NULL)
            return FALSE;

          if (elf_kind (elf) == ELF_K_ELF &&
              gelf_getehdr (elf, &ehdr))
            {
              if (is_shared)
                *is_shared = ehdr.e_type == ET_DYN;
              if (is_stripped)
                *is_stripped = !elf_has_symtab (elf);

              res = TRUE;
            }

          elf_end (elf);
          return res;
        }
    }

  return FALSE;
}

gboolean
directory_is_empty (const char *path)
{
  GDir *dir;
  gboolean empty;

  dir = g_dir_open (path, 0, NULL);
  if (g_dir_read_name (dir) == NULL)
    empty = TRUE;
  else
    empty = FALSE;

  g_dir_close (dir);

  return empty;
}

static gboolean
migrate_locale_dir (GFile      *source_dir,
                    GFile      *separate_dir,
                    const char *subdir,
                    GError    **error)
{
  g_autoptr(GFileEnumerator) dir_enum = NULL;
  GFileInfo *next;
  GError *temp_error = NULL;

  dir_enum = g_file_enumerate_children (source_dir, "standard::name,standard::type",
                                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                        NULL, NULL);
  if (!dir_enum)
    return TRUE;

  while ((next = g_file_enumerator_next_file (dir_enum, NULL, &temp_error)))
    {
      g_autoptr(GFileInfo) child_info = next;
      g_autoptr(GFile) locale_subdir = NULL;

      if (g_file_info_get_file_type (child_info) == G_FILE_TYPE_DIRECTORY)
        {
          g_autoptr(GFile) child = NULL;
          const char *name = g_file_info_get_name (child_info);
          g_autofree char *language = g_strdup (name);
          g_autofree char *relative = NULL;
          g_autofree char *target = NULL;
          char *c;

          c = strchr (language, '@');
          if (c != NULL)
            *c = 0;
          c = strchr (language, '_');
          if (c != NULL)
            *c = 0;
          c = strchr (language, '.');
          if (c != NULL)
            *c = 0;

          /* We ship english and C locales always */
          if (strcmp (language, "C") == 0 ||
              strcmp (language, "en") == 0)
            continue;

          child = g_file_get_child (source_dir, g_file_info_get_name (child_info));

          relative = g_build_filename (language, subdir, name, NULL);
          locale_subdir = g_file_resolve_relative_path (separate_dir, relative);
          if (!flatpak_mkdir_p (locale_subdir, NULL, error))
            return FALSE;

          if (!flatpak_cp_a (child, locale_subdir, NULL,
                             FLATPAK_CP_FLAGS_MERGE | FLATPAK_CP_FLAGS_MOVE,
                             NULL, NULL, error))
            return FALSE;

          target = g_build_filename ("../../share/runtime/locale", relative, NULL);

          if (!g_file_make_symbolic_link (child, target,
                                          NULL, error))
            return FALSE;

        }
    }

  if (temp_error != NULL)
    {
      g_propagate_error (error, temp_error);
      return FALSE;
    }

  return TRUE;
}

gboolean
builder_migrate_locale_dirs (GFile   *root_dir,
                             GError **error)
{
  g_autoptr(GFile) separate_dir = NULL;
  g_autoptr(GFile) lib_locale_dir = NULL;
  g_autoptr(GFile) share_locale_dir = NULL;

  lib_locale_dir = g_file_resolve_relative_path (root_dir, "lib/locale");
  share_locale_dir = g_file_resolve_relative_path (root_dir, "share/locale");
  separate_dir = g_file_resolve_relative_path (root_dir, "share/runtime/locale");

  if (!migrate_locale_dir (lib_locale_dir, separate_dir, "lib", error))
    return FALSE;

  if (!migrate_locale_dir (share_locale_dir, separate_dir, "share", error))
    return FALSE;

  return TRUE;
}

#ifdef FLATPAK_BUILDER_ENABLE_YAML

static JsonNode *
parse_yaml_node_to_json (yaml_document_t *doc, yaml_node_t *node)
{
  JsonNode *json = json_node_alloc ();
  const char *scalar = NULL;
  g_autoptr(JsonArray) array = NULL;
  g_autoptr(JsonObject) object = NULL;
  yaml_node_item_t *item = NULL;
  yaml_node_pair_t *pair = NULL;

  switch (node->type)
    {
    case YAML_NO_NODE:
      json_node_init_null (json);
      break;
    case YAML_SCALAR_NODE:
      scalar = (gchar *) node->data.scalar.value;
      if (node->data.scalar.style == YAML_PLAIN_SCALAR_STYLE)
        {
          if (strcmp (scalar, "true") == 0)
            {
              json_node_init_boolean (json, TRUE);
              break;
            }
          else if (strcmp (scalar, "false") == 0)
            {
              json_node_init_boolean (json, FALSE);
              break;
            }
          else if (strcmp (scalar, "null") == 0)
            {
              json_node_init_null (json);
              break;
            }

          if (*scalar != '\0')
            {
              gchar *endptr;
              gint64 num = g_ascii_strtoll (scalar, &endptr, 10);
              if (*endptr == '\0')
                {
                  json_node_init_int (json, num);
                  break;
                }
              // Make sure that N.N, N., and .N (where N is a digit) are picked up as numbers.
              else if (*endptr == '.' && (endptr != scalar || endptr[1] != '\0'))
                {
                  g_ascii_strtoll (endptr + 1, &endptr, 10);
                  if (*endptr == '\0')
                    g_warning ("%zu:%zu: '%s' will be parsed as a number by many YAML parsers",
                               node->start_mark.line + 1, node->start_mark.column + 1, scalar);
                }
            }
        }

      json_node_init_string (json, scalar);
      break;
    case YAML_SEQUENCE_NODE:
      array = json_array_new ();
      for (item = node->data.sequence.items.start; item < node->data.sequence.items.top; item++)
        {
          yaml_node_t *child = yaml_document_get_node (doc, *item);
          if (child != NULL)
            json_array_add_element (array, parse_yaml_node_to_json (doc, child));
        }

      json_node_init_array (json, array);
      break;
    case YAML_MAPPING_NODE:
      object = json_object_new ();
      for (pair = node->data.mapping.pairs.start; pair < node->data.mapping.pairs.top; pair++)
        {
          yaml_node_t *key = yaml_document_get_node (doc, pair->key);
          yaml_node_t *value = yaml_document_get_node (doc, pair->value);

          g_warn_if_fail (key->type == YAML_SCALAR_NODE);
          json_object_set_member (object, (gchar *) key->data.scalar.value,
                                  parse_yaml_node_to_json (doc, value));
        }

      json_node_init_object (json, object);
      break;
    }

  return json;
}

static JsonNode *
parse_yaml_to_json (const gchar *contents,
                    GError      **error)
{
  if (error)
    *error = NULL;

  g_auto(yaml_parser_t) parser = {0};
  g_auto(yaml_document_t) doc = {{0}};

  if (!yaml_parser_initialize (&parser))
    g_error ("yaml_parser_initialize is out of memory.");
  yaml_parser_set_input_string (&parser, (yaml_char_t *) contents, strlen (contents));

  if (!yaml_parser_load (&parser, &doc))
    {
      g_set_error (error, BUILDER_YAML_PARSE_ERROR, parser.error, "%zu:%zu: %s", parser.problem_mark.line + 1,
                   parser.problem_mark.column + 1, parser.problem);
      return NULL;
    }

  yaml_node_t *root = yaml_document_get_root_node (&doc);
  if (root == NULL)
    {
      g_set_error (error, BUILDER_YAML_PARSE_ERROR, YAML_PARSER_ERROR,
                   "Document has no root node.");
      return NULL;
    }

  return parse_yaml_node_to_json (&doc, root);
}

#else // FLATPAK_BUILDER_ENABLE_YAML

static JsonNode *
parse_yaml_to_json (const gchar *contents,
                    GError      **error)
{
  g_set_error (error, BUILDER_YAML_PARSE_ERROR, 0, "flatpak-builder was not compiled with YAML support.");
  return NULL;
}

#endif  // FLATPAK_BUILDER_ENABLE_YAML

JsonNode *
builder_json_node_from_data (const char *relpath,
                             const char *contents,
                             GError    **error)
{
  if (g_str_has_suffix (relpath, ".yaml") || g_str_has_suffix (relpath, ".yml"))
    return parse_yaml_to_json (contents, error);
  else
    return json_from_string (contents, error);
}

GObject *
builder_gobject_from_data (GType       gtype,
                           const char *relpath,
                           const char *contents,
                           GError    **error)
{
  g_autoptr(JsonNode) json = builder_json_node_from_data (relpath, contents, error);
  if (json != NULL)
    return json_gobject_deserialize (gtype, json);
  else
    return NULL;
}

/*
 * This code is based on debugedit.c from rpm, which has this copyright:
 *
 *
 * Copyright (C) 2001, 2002, 2003, 2005, 2007, 2009, 2010, 2011 Red Hat, Inc.
 * Written by Alexander Larsson <alexl@redhat.com>, 2002
 * Based on code by Jakub Jelinek <jakub@redhat.com>, 2001.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */


#define DW_TAG_partial_unit 0x3c
#define DW_FORM_sec_offset 0x17
#define DW_FORM_exprloc 0x18
#define DW_FORM_flag_present 0x19
#define DW_FORM_ref_sig8 0x20

/* keep uptodate with changes to debug_sections */
#define DEBUG_INFO 0
#define DEBUG_ABBREV 1
#define DEBUG_LINE 2
#define DEBUG_ARANGES 3
#define DEBUG_PUBNAMES 4
#define DEBUG_PUBTYPES 5
#define DEBUG_MACINFO 6
#define DEBUG_LOC 7
#define DEBUG_STR 8
#define DEBUG_FRAME 9
#define DEBUG_RANGES 10
#define DEBUG_TYPES 11
#define DEBUG_MACRO 12
#define DEBUG_GDB_SCRIPT 13
#define NUM_DEBUG_SECTIONS 14

static const char * debug_section_names[] = {
  ".debug_info",
  ".debug_abbrev",
  ".debug_line",
  ".debug_aranges",
  ".debug_pubnames",
  ".debug_pubtypes",
  ".debug_macinfo",
  ".debug_loc",
  ".debug_str",
  ".debug_frame",
  ".debug_ranges",
  ".debug_types",
  ".debug_macro",
  ".debug_gdb_scripts",
};


typedef struct
{
  unsigned char *data;
  Elf_Data      *elf_data;
  size_t         size;
  int            sec, relsec;
} debug_section_t;

typedef struct
{
  Elf            *elf;
  GElf_Ehdr       ehdr;
  Elf_Scn       **scns;
  const char     *filename;
  int             lastscn;
  debug_section_t debug_sections[NUM_DEBUG_SECTIONS];
  GElf_Shdr      *shdr;
} DebuginfoData;

typedef struct
{
  unsigned char *ptr;
  uint32_t       addend;
} REL;

#define read_uleb128(ptr) ({            \
    unsigned int ret = 0;                 \
    unsigned int c;                       \
    int shift = 0;                        \
    do                                    \
    {                                   \
      c = *ptr++;                       \
      ret |= (c & 0x7f) << shift;       \
      shift += 7;                       \
    } while (c & 0x80);                 \
                                        \
    if (shift >= 35)                      \
      ret = UINT_MAX;                     \
    ret;                                  \
  })

static uint16_t (*do_read_16)(unsigned char *ptr);
static uint32_t (*do_read_32) (unsigned char *ptr);

static int ptr_size;
static int cu_version;

static inline uint16_t
buf_read_ule16 (unsigned char *data)
{
  return data[0] | (data[1] << 8);
}

static inline uint16_t
buf_read_ube16 (unsigned char *data)
{
  return data[1] | (data[0] << 8);
}

static inline uint32_t
buf_read_ule32 (unsigned char *data)
{
  return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static inline uint32_t
buf_read_ube32 (unsigned char *data)
{
  return data[3] | (data[2] << 8) | (data[1] << 16) | (data[0] << 24);
}

#define read_1(ptr) *ptr++

#define read_16(ptr) ({                                 \
    uint16_t ret = do_read_16 (ptr);                      \
    ptr += 2;                                             \
    ret;                                                  \
  })

#define read_32(ptr) ({                                 \
    uint32_t ret = do_read_32 (ptr);                      \
    ptr += 4;                                             \
    ret;                                                  \
  })

REL *relptr, *relend;
int reltype;

#define do_read_32_relocated(ptr) ({                    \
    uint32_t dret = do_read_32 (ptr);                     \
    if (relptr)                                           \
    {                                                   \
      while (relptr < relend && relptr->ptr < ptr)      \
        ++relptr;                                       \
      if (relptr < relend && relptr->ptr == ptr)        \
      {                                               \
        if (reltype == SHT_REL)                       \
          dret += relptr->addend;                     \
        else                                          \
          dret = relptr->addend;                      \
      }                                               \
    }                                                   \
    dret;                                                 \
  })

#define read_32_relocated(ptr) ({                       \
    uint32_t ret = do_read_32_relocated (ptr);            \
    ptr += 4;                                             \
    ret;                                                  \
  })

struct abbrev_attr
{
  unsigned int attr;
  unsigned int form;
};

struct abbrev_tag
{
  unsigned int       tag;
  int                nattr;
  struct abbrev_attr attr[0];
};

static GHashTable *
read_abbrev (DebuginfoData *data, unsigned char *ptr)
{
  GHashTable *h;
  unsigned int attr, entry, form;
  struct abbrev_tag *t;
  int size;

  h = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                             NULL, g_free);

  while ((attr = read_uleb128 (ptr)) != 0)
    {
      size = 10;
      entry = attr;
      t = g_malloc (sizeof (*t) + size * sizeof (struct abbrev_attr));
      t->tag = read_uleb128 (ptr);
      t->nattr = 0;
      ++ptr; /* skip children flag.  */
      while ((attr = read_uleb128 (ptr)) != 0)
        {
          if (t->nattr == size)
            {
              size += 10;
              t = g_realloc (t, sizeof (*t) + size * sizeof (struct abbrev_attr));
            }
          form = read_uleb128 (ptr);
          if (form == 2 || (form > DW_FORM_flag_present && form != DW_FORM_ref_sig8))
            g_warning ("%s: Unknown DWARF DW_FORM_%d", data->filename, form);

          t->attr[t->nattr].attr = attr;
          t->attr[t->nattr++].form = form;
        }
      if (read_uleb128 (ptr) != 0)
        g_warning ("%s: DWARF abbreviation does not end with 2 zeros", data->filename);
      g_hash_table_insert (h, GINT_TO_POINTER (entry), t);
    }

  return h;
}

#define IS_DIR_SEPARATOR(c) ((c) == '/')

static char *
canonicalize_path (const char *s, char *d)
{
  char *rv = d;
  char *droot;

  if (IS_DIR_SEPARATOR (*s))
    {
      *d++ = *s++;
      if (IS_DIR_SEPARATOR (*s) && !IS_DIR_SEPARATOR (s[1]))
        /* Special case for "//foo" meaning a Posix namespace  escape.  */
        *d++ = *s++;
      while (IS_DIR_SEPARATOR (*s))
        s++;
    }
  droot = d;

  while (*s)
    {
      /* At this point, we're always at the beginning of a path segment.  */

      if (s[0] == '.' && (s[1] == 0 || IS_DIR_SEPARATOR (s[1])))
        {
          s++;
          if (*s)
            while (IS_DIR_SEPARATOR (*s))
              ++s;
        }
      else if (s[0] == '.' && s[1] == '.' &&
               (s[2] == 0 || IS_DIR_SEPARATOR (s[2])))
        {
          char *pre = d - 1; /* includes slash */
          while (droot < pre && IS_DIR_SEPARATOR (*pre))
            pre--;
          if (droot <= pre && !IS_DIR_SEPARATOR (*pre))
            {
              while (droot < pre && !IS_DIR_SEPARATOR (*pre))
                pre--;
              /* pre now points to the slash */
              if (droot < pre)
                pre++;
              if (pre + 3 == d && pre[0] == '.' && pre[1] == '.')
                {
                  *d++ = *s++;
                  *d++ = *s++;
                }
              else
                {
                  d = pre;
                  s += 2;
                  if (*s)
                    while (IS_DIR_SEPARATOR (*s))
                      s++;
                }
            }
          else
            {
              *d++ = *s++;
              *d++ = *s++;
            }
        }
      else
        {
          while (*s && !IS_DIR_SEPARATOR (*s))
            *d++ = *s++;
        }

      if (IS_DIR_SEPARATOR (*s))
        {
          *d++ = *s++;
          while (IS_DIR_SEPARATOR (*s))
            s++;
        }
    }
  while (droot < d && IS_DIR_SEPARATOR (d[-1]))
    --d;
  if (d == rv)
    *d++ = '.';
  *d = 0;

  return rv;
}

static gboolean
handle_dwarf2_line (DebuginfoData *data, uint32_t off, char *comp_dir, GHashTable *files, GError **error)
{
  unsigned char *ptr = data->debug_sections[DEBUG_LINE].data, *dir;
  unsigned char **dirt;
  unsigned char *endsec = ptr + data->debug_sections[DEBUG_LINE].size;
  unsigned char *endcu, *endprol;
  unsigned char opcode_base;
  uint32_t value, dirt_cnt;
  size_t comp_dir_len = !comp_dir ? 0 : strlen (comp_dir);


  /* XXX: RhBug:929365, should we error out instead of ignoring? */
  if (ptr == NULL)
    return TRUE;

  ptr += off;

  endcu = ptr + 4;
  endcu += read_32 (ptr);
  if (endcu == ptr + 0xffffffff)
    return flatpak_fail (error, "%s: 64-bit DWARF not supported", data->filename);

  if (endcu > endsec)
    return flatpak_fail (error, "%s: .debug_line CU does not fit into section", data->filename);

  value = read_16 (ptr);
  if (value != 2 && value != 3 && value != 4)
    return flatpak_fail (error, "%s: DWARF version %d unhandled", data->filename, value);

  endprol = ptr + 4;
  endprol += read_32 (ptr);
  if (endprol > endcu)
    return flatpak_fail (error, "%s: .debug_line CU prologue does not fit into CU", data->filename);

  opcode_base = ptr[4 + (value >= 4)];
  ptr = dir = ptr + 4 + (value >= 4) + opcode_base;

  /* dir table: */
  value = 1;
  while (*ptr != 0)
    {
      ptr = (unsigned char *) strchr ((char *) ptr, 0) + 1;
      ++value;
    }

  dirt = (unsigned char **) alloca (value * sizeof (unsigned char *));
  dirt[0] = (unsigned char *) ".";
  dirt_cnt = 1;
  ptr = dir;
  while (*ptr != 0)
    {
      dirt[dirt_cnt++] = ptr;
      ptr = (unsigned char *) strchr ((char *) ptr, 0) + 1;
    }
  ptr++;

  /* file table: */
  while (*ptr != 0)
    {
      char *s, *file;
      size_t file_len, dir_len;

      file = (char *) ptr;
      ptr = (unsigned char *) strchr ((char *) ptr, 0) + 1;
      value = read_uleb128 (ptr);

      if (value >= dirt_cnt)
        return flatpak_fail (error, "%s: Wrong directory table index %u",  data->filename, value);

      file_len = strlen (file);
      dir_len = strlen ((char *) dirt[value]);
      s = g_malloc (comp_dir_len + 1 + file_len + 1 + dir_len + 1);
      if (*file == '/')
        {
          memcpy (s, file, file_len + 1);
        }
      else if (*dirt[value] == '/')
        {
          memcpy (s, dirt[value], dir_len);
          s[dir_len] = '/';
          memcpy (s + dir_len + 1, file, file_len + 1);
        }
      else
        {
          char *p = s;
          if (comp_dir_len != 0)
            {
              memcpy (s, comp_dir, comp_dir_len);
              s[comp_dir_len] = '/';
              p += comp_dir_len + 1;
            }
          memcpy (p, dirt[value], dir_len);
          p[dir_len] = '/';
          memcpy (p + dir_len + 1, file, file_len + 1);
        }
      canonicalize_path (s, s);

      g_hash_table_insert (files, s, NULL);

      (void) read_uleb128 (ptr);
      (void) read_uleb128 (ptr);
    }
  ++ptr;

  return TRUE;
}

static unsigned char *
handle_attributes (DebuginfoData *data, unsigned char *ptr, struct abbrev_tag *t, GHashTable *files, GError **error)
{
  int i;
  uint32_t list_offs;
  int found_list_offs;
  g_autofree char *comp_dir = NULL;

  comp_dir = NULL;
  list_offs = 0;
  found_list_offs = 0;
  for (i = 0; i < t->nattr; ++i)
    {
      uint32_t form = t->attr[i].form;
      size_t len = 0;

      while (1)
        {
          if (t->attr[i].attr == DW_AT_stmt_list)
            {
              if (form == DW_FORM_data4 ||
                  form == DW_FORM_sec_offset)
                {
                  list_offs = do_read_32_relocated (ptr);
                  found_list_offs = 1;
                }
            }

          if (t->attr[i].attr == DW_AT_comp_dir)
            {
              if (form == DW_FORM_string)
                {
                  g_free (comp_dir);
                  comp_dir = g_strdup ((char *) ptr);
                }
              else if (form == DW_FORM_strp &&
                       data->debug_sections[DEBUG_STR].data)
                {
                  char *dir;

                  dir = (char *) data->debug_sections[DEBUG_STR].data
                        + do_read_32_relocated (ptr);

                  g_free (comp_dir);
                  comp_dir = g_strdup (dir);
                }
            }
          else if ((t->tag == DW_TAG_compile_unit ||
                    t->tag == DW_TAG_partial_unit) &&
                   t->attr[i].attr == DW_AT_name &&
                   form == DW_FORM_strp &&
                   data->debug_sections[DEBUG_STR].data)
            {
              char *name;

              name = (char *) data->debug_sections[DEBUG_STR].data
                     + do_read_32_relocated (ptr);
              if (*name == '/' && comp_dir == NULL)
                {
                  char *enddir = strrchr (name, '/');

                  if (enddir != name)
                    {
                      comp_dir = g_malloc (enddir - name + 1);
                      memcpy (comp_dir, name, enddir - name);
                      comp_dir[enddir - name] = '\0';
                    }
                  else
                    {
                      comp_dir = g_strdup ("/");
                    }
                }

            }

          switch (form)
            {
            case DW_FORM_ref_addr:
              if (cu_version == 2)
                ptr += ptr_size;
              else
                ptr += 4;
              break;

            case DW_FORM_flag_present:
              break;

            case DW_FORM_addr:
              ptr += ptr_size;
              break;

            case DW_FORM_ref1:
            case DW_FORM_flag:
            case DW_FORM_data1:
              ++ptr;
              break;

            case DW_FORM_ref2:
            case DW_FORM_data2:
              ptr += 2;
              break;

            case DW_FORM_ref4:
            case DW_FORM_data4:
            case DW_FORM_sec_offset:
              ptr += 4;
              break;

            case DW_FORM_ref8:
            case DW_FORM_data8:
            case DW_FORM_ref_sig8:
              ptr += 8;
              break;

            case DW_FORM_sdata:
            case DW_FORM_ref_udata:
            case DW_FORM_udata:
              (void) read_uleb128 (ptr);
              break;

            case DW_FORM_strp:
              ptr += 4;
              break;

            case DW_FORM_string:
              ptr = (unsigned char *) strchr ((char *) ptr, '\0') + 1;
              break;

            case DW_FORM_indirect:
              form = read_uleb128 (ptr);
              continue;

            case DW_FORM_block1:
              len = *ptr++;
              break;

            case DW_FORM_block2:
              len = read_16 (ptr);
              form = DW_FORM_block1;
              break;

            case DW_FORM_block4:
              len = read_32 (ptr);
              form = DW_FORM_block1;
              break;

            case DW_FORM_block:
            case DW_FORM_exprloc:
              len = read_uleb128 (ptr);
              form = DW_FORM_block1;
              g_assert (len < UINT_MAX);
              break;

            default:
              g_warning ("%s: Unknown DWARF DW_FORM_%d", data->filename, form);
              return NULL;
            }

          if (form == DW_FORM_block1)
            ptr += len;

          break;
        }
    }

  /* Ensure the CU current directory will exist even if only empty.  Source
     filenames possibly located in its parent directories refer relatively to
     it and the debugger (GDB) cannot safely optimize out the missing
     CU current dir subdirectories.  */
  if (comp_dir)
    g_hash_table_insert (files, g_strdup (comp_dir), NULL);

  if (found_list_offs &&
      !handle_dwarf2_line (data, list_offs, comp_dir, files, error))
    return NULL;

  return ptr;
}

static int
rel_cmp (const void *a, const void *b)
{
  REL *rela = (REL *) a, *relb = (REL *) b;

  if (rela->ptr < relb->ptr)
    return -1;

  if (rela->ptr > relb->ptr)
    return 1;

  return 0;
}

static gboolean
handle_dwarf2_section (DebuginfoData *data, GHashTable *files, GError **error)
{
  Elf_Data *e_data;
  int i;
  debug_section_t *debug_sections;

  ptr_size = 0;

  if (data->ehdr.e_ident[EI_DATA] == ELFDATA2LSB)
    {
      do_read_16 = buf_read_ule16;
      do_read_32 = buf_read_ule32;
    }
  else if (data->ehdr.e_ident[EI_DATA] == ELFDATA2MSB)
    {
      do_read_16 = buf_read_ube16;
      do_read_32 = buf_read_ube32;
    }
  else
    {
      return flatpak_fail (error, "%s: Wrong ELF data encoding", data->filename);
    }

  debug_sections = data->debug_sections;

  if (debug_sections[DEBUG_INFO].data != NULL)
    {
      unsigned char *ptr, *endcu, *endsec;
      uint32_t value;
      struct abbrev_tag *t;
      g_autofree REL *relbuf = NULL;

      if (debug_sections[DEBUG_INFO].relsec)
        {
          Elf_Scn *scn;
          int ndx, maxndx;
          GElf_Rel rel;
          GElf_Rela rela;
          GElf_Sym sym;
          GElf_Addr base = data->shdr[debug_sections[DEBUG_INFO].sec].sh_addr;
          Elf_Data *symdata = NULL;
          int rtype;

          i = debug_sections[DEBUG_INFO].relsec;
          scn = data->scns[i];
          e_data = elf_getdata (scn, NULL);
          g_assert (e_data != NULL && e_data->d_buf != NULL);
          g_assert (elf_getdata (scn, e_data) == NULL);
          g_assert (e_data->d_off == 0);
          g_assert (e_data->d_size == data->shdr[i].sh_size);
          maxndx = data->shdr[i].sh_size / data->shdr[i].sh_entsize;
          relbuf = g_malloc (maxndx * sizeof (REL));
          reltype = data->shdr[i].sh_type;

          symdata = elf_getdata (data->scns[data->shdr[i].sh_link], NULL);
          g_assert (symdata != NULL && symdata->d_buf != NULL);
          g_assert (elf_getdata (data->scns[data->shdr[i].sh_link], symdata) == NULL);
          g_assert (symdata->d_off == 0);
          g_assert (symdata->d_size == data->shdr[data->shdr[i].sh_link].sh_size);

          for (ndx = 0, relend = relbuf; ndx < maxndx; ++ndx)
            {
              if (data->shdr[i].sh_type == SHT_REL)
                {
                  gelf_getrel (e_data, ndx, &rel);
                  rela.r_offset = rel.r_offset;
                  rela.r_info = rel.r_info;
                  rela.r_addend = 0;
                }
              else
                {
                  gelf_getrela (e_data, ndx, &rela);
                }
              gelf_getsym (symdata, ELF64_R_SYM (rela.r_info), &sym);
              /* Relocations against section symbols are uninteresting
                 in REL.  */
              if (data->shdr[i].sh_type == SHT_REL && sym.st_value == 0)
                continue;
              /* Only consider relocations against .debug_str, .debug_line
                 and .debug_abbrev.  */
              if (sym.st_shndx != debug_sections[DEBUG_STR].sec &&
                  sym.st_shndx != debug_sections[DEBUG_LINE].sec &&
                  sym.st_shndx != debug_sections[DEBUG_ABBREV].sec)
                continue;
              rela.r_addend += sym.st_value;
              rtype = ELF64_R_TYPE (rela.r_info);
              switch (data->ehdr.e_machine)
                {
                case EM_SPARC:
                case EM_SPARC32PLUS:
                case EM_SPARCV9:
                  if (rtype != R_SPARC_32 && rtype != R_SPARC_UA32)
                    goto fail;
                  break;

                case EM_386:
                  if (rtype != R_386_32)
                    goto fail;
                  break;

                case EM_PPC:
                case EM_PPC64:
                  if (rtype != R_PPC_ADDR32 && rtype != R_PPC_UADDR32)
                    goto fail;
                  break;

                case EM_S390:
                  if (rtype != R_390_32)
                    goto fail;
                  break;

                case EM_IA_64:
                  if (rtype != R_IA64_SECREL32LSB)
                    goto fail;
                  break;

                case EM_X86_64:
                  if (rtype != R_X86_64_32)
                    goto fail;
                  break;

                case EM_ALPHA:
                  if (rtype != R_ALPHA_REFLONG)
                    goto fail;
                  break;

#if defined(EM_AARCH64) && defined(R_AARCH64_ABS32)
                case EM_AARCH64:
                  if (rtype != R_AARCH64_ABS32)
                    goto fail;
                  break;

#endif
                case EM_68K:
                  if (rtype != R_68K_32)
                    goto fail;
                  break;

                default:
fail:
                  return flatpak_fail (error, "%s: Unhandled relocation %d in .debug_info section",
                                       data->filename, rtype);
                }
              relend->ptr = debug_sections[DEBUG_INFO].data
                            + (rela.r_offset - base);
              relend->addend = rela.r_addend;
              ++relend;
            }
          if (relbuf == relend)
            {
              g_free (relbuf);
              relbuf = NULL;
              relend = NULL;
            }
          else
            {
              qsort (relbuf, relend - relbuf, sizeof (REL), rel_cmp);
            }
        }

      ptr = debug_sections[DEBUG_INFO].data;
      relptr = relbuf;
      endsec = ptr + debug_sections[DEBUG_INFO].size;
      while (ptr != NULL && ptr < endsec)
        {
          g_autoptr(GHashTable) abbrev = NULL;

          if (ptr + 11 > endsec)
            return flatpak_fail (error, "%s: .debug_info CU header too small", data->filename);

          endcu = ptr + 4;
          endcu += read_32 (ptr);
          if (endcu == ptr + 0xffffffff)
            return flatpak_fail (error, "%s: 64-bit DWARF not supported", data->filename);

          if (endcu > endsec)
            return flatpak_fail (error, "%s: .debug_info too small", data->filename);

          cu_version = read_16 (ptr);
          if (cu_version != 2 && cu_version != 3 && cu_version != 4)
            return flatpak_fail (error, "%s: DWARF version %d unhandled", data->filename, cu_version);

          value = read_32_relocated (ptr);
          if (value >= debug_sections[DEBUG_ABBREV].size)
            {
              if (debug_sections[DEBUG_ABBREV].data == NULL)
                return flatpak_fail (error, "%s: .debug_abbrev not present", data->filename);
              else
                return flatpak_fail (error, "%s: DWARF CU abbrev offset too large", data->filename);
            }

          if (ptr_size == 0)
            {
              ptr_size = read_1 (ptr);
              if (ptr_size != 4 && ptr_size != 8)
                return flatpak_fail (error, "%s: Invalid DWARF pointer size %d", data->filename, ptr_size);
            }
          else if (read_1 (ptr) != ptr_size)
            {
              return flatpak_fail (error, "%s: DWARF pointer size differs between CUs", data->filename);
            }

          abbrev = read_abbrev (data,
                                debug_sections[DEBUG_ABBREV].data + value);

          while (ptr < endcu)
            {
              guint entry = read_uleb128 (ptr);
              if (entry == 0)
                continue;
              t = g_hash_table_lookup (abbrev, GINT_TO_POINTER (entry));
              if (t == NULL)
                {
                  g_warning ("%s: Could not find DWARF abbreviation %d", data->filename, entry);
                }
              else
                {
                  ptr = handle_attributes (data, ptr, t, files, error);
                  if (ptr == NULL)
                    return FALSE;
                }
            }
        }
    }

  return TRUE;
}

static const char *
strptr (Elf_Scn **scns, GElf_Shdr *shdr, int sec, off_t offset)
{
  Elf_Scn *scn;
  Elf_Data *data;

  scn = scns[sec];
  if (offset >= 0 && (GElf_Addr) offset < shdr[sec].sh_size)
    {
      data = NULL;
      while ((data = elf_rawdata (scn, data)) != NULL)
        {
          if (data->d_buf &&
              offset >= data->d_off &&
              offset < data->d_off + data->d_size)
            return (const char *) data->d_buf + (offset - data->d_off);
        }
    }

  return NULL;
}

char **
builder_get_debuginfo_file_references (const char *filename, GError **error)
{
  Elf *elf = NULL;
  GElf_Ehdr ehdr;
  int i, j;
  glnx_fd_close int fd = -1;
  DebuginfoData data = { 0 };
  g_autofree GElf_Shdr *shdr = NULL;
  g_autofree Elf_Scn **scns = NULL;
  debug_section_t *debug_sections;

  g_autoptr(GHashTable) files = NULL;
  char **res;

  fd = open (filename, O_RDONLY);
  if (fd == -1)
    {
      glnx_set_error_from_errno (error);
      return NULL;
    }

  elf = elf_begin (fd, ELF_C_RDWR_MMAP, NULL);
  if (elf == NULL)
    {
      flatpak_fail (error, "cannot open ELF file: %s", elf_errmsg (-1));
      return NULL;
    }

  if (elf_kind (elf) != ELF_K_ELF)
    {
      flatpak_fail (error, "\"%s\" is not an ELF file", filename);
      return NULL;
    }

  if (gelf_getehdr (elf, &ehdr) == NULL)
    {
      flatpak_fail (error, "cannot get the ELF header: %s", elf_errmsg (-1));
      return NULL;
    }

  if (ehdr.e_type != ET_DYN && ehdr.e_type != ET_EXEC && ehdr.e_type != ET_REL)
    {
      flatpak_fail (error, "\"%s\" is not a shared library", filename);
      return NULL;
    }

  elf_flagelf (elf, ELF_C_SET, ELF_F_LAYOUT);

  shdr = g_new0 (GElf_Shdr, ehdr.e_shnum);
  scns =  g_new0 (Elf_Scn *, ehdr.e_shnum);

  for (i = 0; i < ehdr.e_shnum; ++i)
    {
      scns[i] = elf_getscn (elf, i);
      gelf_getshdr (scns[i], &shdr[i]);
    }

  data.elf = elf;
  data.ehdr = ehdr;
  data.shdr = shdr;
  data.scns = scns;
  data.filename = filename;

  /* Locate all debug sections */
  debug_sections = data.debug_sections;
  for (i = 1; i < ehdr.e_shnum; ++i)
    {
      if (!(shdr[i].sh_flags & (SHF_ALLOC | SHF_WRITE | SHF_EXECINSTR)) && shdr[i].sh_size)
        {
          const char *name = strptr (scns, shdr, ehdr.e_shstrndx, shdr[i].sh_name);

          if (g_str_has_prefix (name, ".debug_"))
            {
              for (j = 0; j < NUM_DEBUG_SECTIONS; ++j)
                {
                  if (strcmp (name, debug_section_names[j]) == 0)
                    {
                      Elf_Scn *scn = scns[i];
                      Elf_Data *e_data;

                      if (debug_sections[j].data)
                        g_warning ("%s: Found two copies of %s section", filename, name);

                      e_data = elf_rawdata (scn, NULL);
                      g_assert (e_data != NULL && e_data->d_buf != NULL);
                      g_assert (elf_rawdata (scn, e_data) == NULL);
                      g_assert (e_data->d_off == 0);
                      g_assert (e_data->d_size == shdr[i].sh_size);
                      debug_sections[j].data = e_data->d_buf;
                      debug_sections[j].elf_data = e_data;
                      debug_sections[j].size = e_data->d_size;
                      debug_sections[j].sec = i;
                      break;
                    }
                }

              if (j == NUM_DEBUG_SECTIONS)
                g_warning ("%s: Unknown debugging section %s", filename, name);
            }
          else if (ehdr.e_type == ET_REL &&
                   ((shdr[i].sh_type == SHT_REL && g_str_has_prefix (name, ".rel.debug_")) ||
                    (shdr[i].sh_type == SHT_RELA && g_str_has_prefix (name, ".rela.debug_"))))
            {
              for (j = 0; j < NUM_DEBUG_SECTIONS; ++j)
                if (strcmp (name + sizeof (".rel") - 1
                            + (shdr[i].sh_type == SHT_RELA),
                            debug_section_names[j]) == 0)
                  {
                    debug_sections[j].relsec = i;
                    break;
                  }
            }
        }
    }

  files = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  if (!handle_dwarf2_section (&data, files, error))
    return NULL;

  if (elf_end (elf) < 0)
    g_warning ("elf_end failed: %s", elf_errmsg (elf_errno ()));

  res = (char **) g_hash_table_get_keys_as_array (files, NULL);
  g_hash_table_steal_all (files);
  return res;
}

typedef struct {
  GDBusConnection *connection;
  GMainLoop *loop;
  GError    *splice_error;
  guint32 client_pid;
  guint32 exit_status;
  int refs;
} HostCommandCallData;

static void
host_command_call_exit (HostCommandCallData *data)
{
  data->refs--;
  if (data->refs == 0)
    g_main_loop_quit (data->loop);
}

static void
output_spliced_cb (GObject      *obj,
                   GAsyncResult *result,
                   gpointer      user_data)
{
  HostCommandCallData *data = user_data;

  g_output_stream_splice_finish (G_OUTPUT_STREAM (obj), result, &data->splice_error);
  host_command_call_exit (data);
}

static void
host_command_exited_cb (GDBusConnection *connection,
                        const gchar     *sender_name,
                        const gchar     *object_path,
                        const gchar     *interface_name,
                        const gchar     *signal_name,
                        GVariant        *parameters,
                        gpointer         user_data)
{
  guint32 client_pid, exit_status;
  HostCommandCallData *data = (HostCommandCallData *)user_data;

  if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(uu)")))
    return;

  g_variant_get (parameters, "(uu)", &client_pid, &exit_status);

  if (client_pid == data->client_pid)
    {
      g_debug ("host_command_exited_cb %d %d\n", client_pid, exit_status);
      data->exit_status = exit_status;
      host_command_call_exit (data);
    }
}

static gboolean
sigterm_handler (gpointer user_data)
{
  HostCommandCallData *data = (HostCommandCallData *)user_data;

  g_dbus_connection_call_sync (data->connection,
                               "org.freedesktop.Flatpak",
                               "/org/freedesktop/Flatpak/Development",
                               "org.freedesktop.Flatpak.Development",
                               "HostCommandSignal",
                               g_variant_new ("(uub)", data->client_pid, SIGTERM, TRUE),
                               NULL,
                               G_DBUS_CALL_FLAGS_NONE, -1,
                               NULL, NULL);

  kill (getpid (), SIGKILL);
  return TRUE;
}

static gboolean
sigint_handler (gpointer user_data)
{
  HostCommandCallData *data = (HostCommandCallData *)user_data;

  g_dbus_connection_call_sync (data->connection,
                               "org.freedesktop.Flatpak",
                               "/org/freedesktop/Flatpak/Development",
                               "org.freedesktop.Flatpak.Development",
                               "HostCommandSignal",
                               g_variant_new ("(uub)", data->client_pid, SIGINT, TRUE),
                               NULL,
                               G_DBUS_CALL_FLAGS_NONE, -1,
                               NULL, NULL);

  kill (getpid (), SIGKILL);
  return TRUE;
}

gboolean
builder_host_spawnv (GFile                *dir,
                     char                **output,
                     GSubprocessFlags      flags,
                     GError              **error,
                     const gchar * const  *argv)
{
  static FlatpakHostCommandFlags cmd_flags = FLATPAK_HOST_COMMAND_FLAGS_CLEAR_ENV | FLATPAK_HOST_COMMAND_FLAGS_WATCH_BUS;
  guint32 client_pid;
  GVariantBuilder *fd_builder = g_variant_builder_new (G_VARIANT_TYPE("a{uh}"));
  GVariantBuilder *env_builder = g_variant_builder_new (G_VARIANT_TYPE("a{ss}"));
  g_autoptr(GUnixFDList) fd_list = g_unix_fd_list_new ();
  gint stdout_handle, stdin_handle, stderr_handle = -1;
  g_autoptr(GDBusConnection) connection = NULL;
  g_autoptr(GVariant) ret = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_auto(GStrv) env_vars = NULL;
  guint subscription;
  HostCommandCallData data = { NULL };
  guint sigterm_id = 0, sigint_id = 0;
  g_autofree gchar *commandline = NULL;
  g_autoptr(GOutputStream) out = NULL;
  g_autoptr(GFile) cwd = NULL;
  g_autoptr(GError) local_error = NULL;
  glnx_fd_close int blocking_stdin_fd = -1;
  int pipefd[2];
  int stdin_fd;
  int i;

  if (error == NULL)
    error = &local_error;

  if (dir == NULL)
    {
      g_autofree char *current_dir = g_get_current_dir ();
      cwd = g_file_new_for_path (current_dir);
      dir = cwd;
    }

  commandline = flatpak_quote_argv ((const char **) argv);
  g_debug ("Running '%s' on host", commandline);

  connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
  if (connection == NULL)
    return FALSE;

  loop = g_main_loop_new (NULL, FALSE);
  data.connection = connection;
  data.loop = loop;
  data.refs = 1;

  subscription = g_dbus_connection_signal_subscribe (connection,
                                                     NULL,
                                                     "org.freedesktop.Flatpak.Development",
                                                     "HostCommandExited",
                                                     "/org/freedesktop/Flatpak/Development",
                                                     NULL,
                                                     G_DBUS_SIGNAL_FLAGS_NONE,
                                                     host_command_exited_cb,
                                                     &data, NULL);

  if ((flags & G_SUBPROCESS_FLAGS_STDIN_INHERIT) != 0)
    stdin_fd = 0;
  else
    {
      blocking_stdin_fd = open ("/dev/null", O_RDONLY| O_CLOEXEC);
      if (blocking_stdin_fd == -1)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }
      stdin_fd = blocking_stdin_fd;
    }

  stdin_handle = g_unix_fd_list_append (fd_list, stdin_fd, error);
  if (stdin_handle == -1)
    return FALSE;

  if (output)
    {
      g_autoptr(GInputStream) in = NULL;

      if (pipe2 (pipefd, O_CLOEXEC) != 0)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }

      data.refs++;
      in = g_unix_input_stream_new (pipefd[0], TRUE);
      out = g_memory_output_stream_new_resizable ();
      g_output_stream_splice_async (out,
                                    in,
                                    G_OUTPUT_STREAM_SPLICE_NONE,
                                    0,
                                    NULL,
                                    output_spliced_cb,
                                    &data);
      stdout_handle = g_unix_fd_list_append (fd_list, pipefd[1], error);
      close (pipefd[1]);
      if (stdout_handle == -1)
        return FALSE;
    }
  else
    {
      stdout_handle = g_unix_fd_list_append (fd_list, 1, error);
      if (stdout_handle == -1)
        return FALSE;
    }

  g_variant_builder_add (fd_builder, "{uh}", 0, stdin_handle);
  g_variant_builder_add (fd_builder, "{uh}", 1, stdout_handle);

  if ((flags & G_SUBPROCESS_FLAGS_STDERR_SILENCE) == 0)
    {
      stderr_handle = g_unix_fd_list_append (fd_list, 2, error);
      if (stderr_handle == -1)
        return FALSE;
      g_variant_builder_add (fd_builder, "{uh}", 2, stderr_handle);
    }

  env_vars = g_listenv ();
  for (i = 0; env_vars[i] != NULL; i++)
    {
      const char *env_var = env_vars[i];
      if (strcmp (env_var, "LANGUAGE") != 0)
        g_variant_builder_add (env_builder, "{ss}", env_var, g_getenv (env_var));
    }
  g_variant_builder_add (env_builder, "{ss}", "LANGUAGE", "C");

  sigterm_id = g_unix_signal_add (SIGTERM, sigterm_handler, &data);
  sigint_id = g_unix_signal_add (SIGINT, sigint_handler, &data);

try_again:
  ret = g_dbus_connection_call_with_unix_fd_list_sync (connection,
                                                       "org.freedesktop.Flatpak",
                                                       "/org/freedesktop/Flatpak/Development",
                                                       "org.freedesktop.Flatpak.Development",
                                                       "HostCommand",
                                                       g_variant_new ("(^ay^aay@a{uh}@a{ss}u)",
                                                                      dir ? flatpak_file_get_path_cached (dir) : "",
                                                                      argv,
                                                                      g_variant_builder_end (fd_builder),
                                                                      g_variant_builder_end (env_builder),
                                                                      cmd_flags),
                                                       G_VARIANT_TYPE ("(u)"),
                                                       G_DBUS_CALL_FLAGS_NONE, -1,
                                                       fd_list, NULL,
                                                       NULL, error);

  if (ret == NULL)
    {
      /* If we are talking to a session-helper that is pre-1.2 we wont have
       * access to FLATPAK_HOST_COMMAND_FLAGS_WATCH_BUS and will get an
       * invalid-args reply. Try again without the flag.
       */
      if ((cmd_flags & FLATPAK_HOST_COMMAND_FLAGS_WATCH_BUS) != 0 &&
          g_error_matches (*error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS))
        {
          cmd_flags &= ~FLATPAK_HOST_COMMAND_FLAGS_WATCH_BUS;
          g_clear_error (error);
          goto try_again;
        }

      return FALSE;
    }


  g_variant_get (ret, "(u)", &client_pid);
  data.client_pid = client_pid;

  /* Drop the FDList immediately or splice_async() may not
   * complete when the peer process exists, causing us to hang.
   */
  g_clear_object (&fd_list);

  g_main_loop_run (loop);

  g_source_remove (sigterm_id);
  g_source_remove (sigint_id);
  g_dbus_connection_signal_unsubscribe (connection, subscription);

  if (!g_spawn_check_exit_status (data.exit_status, error))
    return FALSE;

  if (out)
    {
      if (data.splice_error)
        {
          g_propagate_error (error, data.splice_error);
          return FALSE;
        }

      /* Null terminate */
      g_output_stream_write (out, "\0", 1, NULL, NULL);
      g_output_stream_close (out, NULL, NULL);
      *output = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (out));
    }

  return TRUE;
}

/* Similar to flatpak_spawnv, except uses the session helper HostCommand operation
   if in a sandbox */
gboolean
builder_maybe_host_spawnv (GFile                *dir,
                           char                **output,
                           GSubprocessFlags      flags,
                           GError              **error,
                           const gchar * const  *argv)
{
  if (flatpak_is_in_sandbox ())
    return builder_host_spawnv (dir, output, flags, error, argv);

  return flatpak_spawnv (dir, output, flags, error, argv);
}

/**
 * builder_get_all_checksums:
 *
 * Collect all the non-empty/null checksums into a single array with
 * their type.
 *
 * The checksum are returned in order such that the first one is the
 * one to use by default if using a single one. That is typically the
 * longest checksum except it defaults to sha256 if set because
 * that was historically the one flatpak-builder used
 */
gsize
builder_get_all_checksums (const char *checksums[BUILDER_CHECKSUMS_LEN],
                           GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN],
                           const char *md5,
                           const char *sha1,
                           const char *sha256,
                           const char *sha512)
{
  gsize i = 0;


  if (sha256 != NULL && *sha256 != 0)
    {
      g_assert (i < BUILDER_CHECKSUMS_LEN);
      checksums[i] = sha256;
      checksums_type[i] = G_CHECKSUM_SHA256;
      i++;
    }

  if (sha512 != NULL && *sha512 != 0)
    {
      g_assert (i < BUILDER_CHECKSUMS_LEN);
      checksums[i] = sha512;
      checksums_type[i] = G_CHECKSUM_SHA512;
      i++;
    }

  if (sha1 != NULL && *sha1 != 0)
    {
      g_assert (i < BUILDER_CHECKSUMS_LEN);
      checksums[i] = sha1;
      checksums_type[i] = G_CHECKSUM_SHA1;
      i++;
    }

  if (md5 != NULL && *md5 != 0)
    {
      g_assert (i < BUILDER_CHECKSUMS_LEN);
      checksums[i] = md5;
      checksums_type[i] = G_CHECKSUM_MD5;
      i++;
    }

  g_assert (i < BUILDER_CHECKSUMS_LEN);
  checksums[i++] = 0;

  return i;
}

static gboolean
compare_checksum (const char *name,
                  const char *expected_checksum,
                  GChecksumType checksum_type,
                  const char *measured_checksum,
                  GError **error)
{
  const char *type_names[] = { "md5", "sha1", "sha256", "sha512", "sha384" }; /* In GChecksumType order */
  const char *type_name;

  if (checksum_type < G_N_ELEMENTS (type_names))
    type_name = type_names[checksum_type];
  else
    type_name = "unknown";

  if (strcmp (expected_checksum, measured_checksum) != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Wrong %s checksum for %s, expected \"%s\", was \"%s\"", type_name, name,
                   expected_checksum, measured_checksum);
      return FALSE;
    }

  return TRUE;
}

#define GET_BUFFER_SIZE 8192

gboolean
builder_verify_checksums (const char *name,
                          GFile *file,
                          const char *checksums[BUILDER_CHECKSUMS_LEN],
                          GChecksumType checksums_type[BUILDER_CHECKSUMS_LEN],
                          GError **error)
{
  gsize i;

  for (i = 0; checksums[i] != NULL; i++)
    {
      g_autoptr(GFileInputStream) stream = NULL;

      stream = g_file_read (file, NULL, error);

      if (stream == NULL)
        return FALSE;

      gssize bytes_read;
      guchar buffer[GET_BUFFER_SIZE];
      GChecksum *checksum = NULL;
      const char *checksum_string = NULL;
      gboolean is_valid;

      checksum = g_checksum_new (checksums_type[i]);

      while ((bytes_read = g_input_stream_read (G_INPUT_STREAM(stream),
                                                buffer, GET_BUFFER_SIZE,
                                                NULL, error)) > 0)
        {
          g_checksum_update (checksum, buffer, bytes_read);
        }

      if (bytes_read < 0)
        {
          is_valid = FALSE;
        }
      else
        {
          checksum_string = g_checksum_get_string (checksum);
          is_valid = compare_checksum (name, checksums[i], checksums_type[i],
                                       checksum_string, error);
        }

      g_checksum_free (checksum);

      if (!is_valid)
        return FALSE;
    }

  return TRUE;
}

typedef struct {
  GOutputStream  *out;
  GChecksum     **checksums;
  gsize           n_checksums;
  GError        **error;
} CURLWriteData;

static gsize
builder_curl_write_cb (gpointer *buffer,
                       gsize     size,
                       gsize     nmemb,
                       gpointer *userdata)
{
  gsize bytes_written;
  CURLWriteData *write_data = (CURLWriteData *) userdata;

  flatpak_write_update_checksum (write_data->out, buffer, size * nmemb, &bytes_written,
                                 write_data->checksums, write_data->n_checksums,
                                 NULL, write_data->error);

  return bytes_written;
}

static gboolean
builder_download_uri_curl (SoupURI        *uri,
                           CURL           *session,
                           GOutputStream  *out,
                           GChecksum     **checksums,
                           gsize           n_checksums,
                           GError        **error)
{
  CURLcode retcode;
  CURLWriteData write_data;
  static gchar error_buffer[CURL_ERROR_SIZE];
  g_autofree gchar *url = soup_uri_to_string (uri, FALSE);

  curl_easy_setopt (session, CURLOPT_URL, url);
  curl_easy_setopt (session, CURLOPT_WRITEFUNCTION, builder_curl_write_cb);
  curl_easy_setopt (session, CURLOPT_WRITEDATA, &write_data);
  curl_easy_setopt (session, CURLOPT_ERRORBUFFER, error_buffer);

  write_data.out = out;
  write_data.checksums = checksums;
  write_data.n_checksums = n_checksums;
  write_data.error = error;

  *error_buffer = '\0';
  retcode = curl_easy_perform (session);

  if (retcode != CURLE_OK)
    {
      g_set_error_literal (error, BUILDER_CURL_ERROR, retcode,
                           *error_buffer ? error_buffer : curl_easy_strerror (retcode));
      return FALSE;
    }

  return TRUE;
}

gboolean
builder_download_uri (SoupURI        *uri,
                      GFile          *dest,
                      const char     *checksums[BUILDER_CHECKSUMS_LEN],
                      GChecksumType   checksums_type[BUILDER_CHECKSUMS_LEN],
                      CURL           *curl_session,
                      GError        **error)
{
  g_autoptr(GFileOutputStream) out = NULL;
  g_autoptr(GFile) tmp = NULL;
  g_autoptr(GFile) dir = NULL;
  g_autoptr(GPtrArray) checksum_array = g_ptr_array_new_with_free_func ((GDestroyNotify)g_checksum_free);
  g_autofree char *basename = g_file_get_basename (dest);
  g_autofree char *template = g_strconcat (".", basename, "XXXXXX", NULL);
  gsize i;

  for (i = 0; checksums[i] != NULL; i++)
    g_ptr_array_add (checksum_array,
                     g_checksum_new (checksums_type[i]));

  dir = g_file_get_parent (dest);
  g_mkdir_with_parents (flatpak_file_get_path_cached (dir), 0755);

  tmp = flatpak_file_new_tmp_in (dir, template, error);
  if (tmp == NULL)
    return FALSE;

  out = g_file_replace (tmp, NULL, FALSE, G_FILE_CREATE_REPLACE_DESTINATION,
                        NULL, error);
  if (out == NULL)
    return FALSE;

  if (!builder_download_uri_curl (uri,
                                  curl_session,
                                  G_OUTPUT_STREAM (out),
                                  (GChecksum **)checksum_array->pdata,
                                  checksum_array->len,
                                  error))
    {
      unlink (flatpak_file_get_path_cached (tmp));
      return FALSE;
    }

  /* Manually close to flush and detect write errors */
  if (!g_output_stream_close (G_OUTPUT_STREAM (out), NULL, error))
    {
      unlink (flatpak_file_get_path_cached (tmp));
      return FALSE;
    }

  for (i = 0; checksums[i] != NULL; i++)
    {
      const char *checksum = g_checksum_get_string (g_ptr_array_index (checksum_array, i));
      if (!compare_checksum (basename, checksums[i], checksums_type[i], checksum, error))
        {
          unlink (flatpak_file_get_path_cached (tmp));
          return FALSE;
        }
    }

  if (rename (flatpak_file_get_path_cached (tmp), flatpak_file_get_path_cached (dest)) != 0)
    {
      glnx_set_error_from_errno (error);
      return FALSE;
    }

  return TRUE;
}

typedef struct {
  GParamSpec *pspec;
  JsonNode *data;
} BuilderXProperty;

static BuilderXProperty *
builder_x_property_new (const char *name)
{
  BuilderXProperty *property = g_new0 (BuilderXProperty, 1);
  property->pspec = g_param_spec_boxed (name, "", "", JSON_TYPE_NODE, G_PARAM_READWRITE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);

  return property;
}

static void
builder_x_property_free (BuilderXProperty *prop)
{
  g_param_spec_unref (prop->pspec);
  if (prop->data)
    json_node_unref (prop->data);
  g_free (prop);
}

static const char *
builder_x_property_get_name (BuilderXProperty *prop)
{
  return g_param_spec_get_name (prop->pspec);
}

GParamSpec *
builder_serializable_find_property (JsonSerializable *serializable,
                                    const char       *name)
{
  GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (serializable), name);

  if (pspec == NULL &&
      g_str_has_prefix (name, "x-"))
    {
      GHashTable *x_props = g_object_get_data (G_OBJECT (serializable), "flatpak-x-props");
      BuilderXProperty *prop;

      if (x_props == NULL)
        {
          x_props = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)builder_x_property_free);
          g_object_set_data_full (G_OBJECT (serializable), "flatpak-x-props", x_props, (GDestroyNotify)g_hash_table_unref);
        }

      prop = g_hash_table_lookup (x_props, name);
      if (prop == NULL)
        {
          prop = builder_x_property_new (name);
          g_hash_table_insert (x_props, (char *)builder_x_property_get_name (prop), prop);
        }

      pspec = prop->pspec;
    }

  if (pspec == NULL &&
      !g_str_has_prefix (name, "__") &&
      !g_str_has_prefix (name, "//"))
    g_warning ("Unknown property %s for type %s", name, g_type_name_from_instance ((GTypeInstance *)serializable));

  return pspec;
}

GParamSpec **
builder_serializable_list_properties (JsonSerializable *serializable,
                                      guint            *n_pspecs)
{
  GPtrArray *res = g_ptr_array_new ();
  guint n_normal, i;
  g_autofree GParamSpec **normal = NULL;
  GHashTable *x_props;

  normal = g_object_class_list_properties (G_OBJECT_GET_CLASS (serializable), &n_normal);

  for (i = 0; i < n_normal; i++)
    g_ptr_array_add (res, normal[i]);

  x_props = g_object_get_data (G_OBJECT (serializable), "flatpak-x-props");
  if (x_props)
    {
      GLNX_HASH_TABLE_FOREACH_V (x_props, BuilderXProperty *, prop)
        {
          g_ptr_array_add (res, prop->pspec);
        }
    }

  if (n_pspecs)
    *n_pspecs = res->len;

  g_ptr_array_add (res, NULL);

  return (GParamSpec **)g_ptr_array_free (res, FALSE);
}

gboolean
builder_serializable_deserialize_property (JsonSerializable *serializable,
                                           const gchar      *property_name,
                                           GValue           *value,
                                           GParamSpec       *pspec,
                                           JsonNode         *property_node)
{
  GHashTable *x_props = g_object_get_data (G_OBJECT (serializable), "flatpak-x-props");

  if (x_props)
    {
      BuilderXProperty *prop = g_hash_table_lookup (x_props, property_name);
      if (prop)
        {
          g_value_set_boxed (value, property_node);
          return TRUE;
        }
    }

  return json_serializable_default_deserialize_property (serializable, property_name, value, pspec, property_node);
}

JsonNode *
builder_serializable_serialize_property (JsonSerializable *serializable,
                                         const gchar      *property_name,
                                         const GValue     *value,
                                         GParamSpec       *pspec)
{
  GHashTable *x_props = g_object_get_data (G_OBJECT (serializable), "flatpak-x-props");

  if (x_props)
    {
      BuilderXProperty *prop = g_hash_table_lookup (x_props, property_name);
      if (prop)
        return g_value_dup_boxed (value);
    }

  return json_serializable_default_serialize_property (serializable, property_name, value, pspec);
}

void
builder_serializable_set_property (JsonSerializable *serializable,
                                   GParamSpec       *pspec,
                                   const GValue     *value)
{
  GHashTable *x_props = g_object_get_data (G_OBJECT (serializable), "flatpak-x-props");

  if (x_props)
    {
      BuilderXProperty *prop = g_hash_table_lookup (x_props, g_param_spec_get_name (pspec));
      if (prop)
        {
          prop->data = g_value_dup_boxed (value);
          return;
        }
    }

  g_object_set_property (G_OBJECT (serializable), pspec->name, value);
}

void
builder_serializable_get_property (JsonSerializable *serializable,
                                   GParamSpec       *pspec,
                                   GValue           *value)
{
  GHashTable *x_props = g_object_get_data (G_OBJECT (serializable), "flatpak-x-props");

  if (x_props)
    {
      BuilderXProperty *prop = g_hash_table_lookup (x_props, g_param_spec_get_name (pspec));
      if (prop)
        {
          g_value_set_boxed (value, prop->data);
          return;
        }
    }

  g_object_get_property (G_OBJECT (serializable), pspec->name, value);
}

void
builder_set_term_title (const gchar *format,
                        ...)
{
  g_autofree gchar *message = NULL;
  va_list args;

  if (isatty (STDOUT_FILENO) != 1)
    return;

  va_start (args, format);
  message = g_strdup_vprintf (format, args);
  va_end (args);

  g_print ("\033]2;flatpak-builder: %s\007", message);
}

typedef struct
{
  FlatpakXml *current;
} XmlData;

FlatpakXml *
flatpak_xml_new (const gchar *element_name)
{
  FlatpakXml *node = g_new0 (FlatpakXml, 1);

  node->element_name = g_strdup (element_name);
  return node;
}

FlatpakXml *
flatpak_xml_new_text (const gchar *text)
{
  FlatpakXml *node = g_new0 (FlatpakXml, 1);

  node->text = g_strdup (text);
  return node;
}

void
flatpak_xml_add (FlatpakXml *parent, FlatpakXml *node)
{
  node->parent = parent;

  if (parent->first_child == NULL)
    parent->first_child = node;
  else
    parent->last_child->next_sibling = node;
  parent->last_child = node;
}

static void
xml_start_element (GMarkupParseContext *context,
                   const gchar         *element_name,
                   const gchar        **attribute_names,
                   const gchar        **attribute_values,
                   gpointer             user_data,
                   GError             **error)
{
  XmlData *data = user_data;
  FlatpakXml *node;

  node = flatpak_xml_new (element_name);
  node->attribute_names = g_strdupv ((char **) attribute_names);
  node->attribute_values = g_strdupv ((char **) attribute_values);

  flatpak_xml_add (data->current, node);
  data->current = node;
}

static void
xml_end_element (GMarkupParseContext *context,
                 const gchar         *element_name,
                 gpointer             user_data,
                 GError             **error)
{
  XmlData *data = user_data;

  data->current = data->current->parent;
}

static void
xml_text (GMarkupParseContext *context,
          const gchar         *text,
          gsize                text_len,
          gpointer             user_data,
          GError             **error)
{
  XmlData *data = user_data;
  FlatpakXml *node;

  node = flatpak_xml_new (NULL);
  node->text = g_strndup (text, text_len);
  flatpak_xml_add (data->current, node);
}

static void
xml_passthrough (GMarkupParseContext *context,
                 const gchar         *passthrough_text,
                 gsize                text_len,
                 gpointer             user_data,
                 GError             **error)
{
}

static GMarkupParser xml_parser = {
  xml_start_element,
  xml_end_element,
  xml_text,
  xml_passthrough,
  NULL
};

void
flatpak_xml_free (FlatpakXml *node)
{
  FlatpakXml *child;

  if (node == NULL)
    return;

  child = node->first_child;
  while (child != NULL)
    {
      FlatpakXml *next = child->next_sibling;
      flatpak_xml_free (child);
      child = next;
    }

  g_free (node->element_name);
  g_free (node->text);
  g_strfreev (node->attribute_names);
  g_strfreev (node->attribute_values);
  g_free (node);
}


void
flatpak_xml_to_string (FlatpakXml *node, GString *res)
{
  int i;
  FlatpakXml *child;

  if (node->parent == NULL)
    g_string_append (res, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

  if (node->element_name)
    {
      if (node->parent != NULL)
        {
          g_string_append (res, "<");
          g_string_append (res, node->element_name);
          if (node->attribute_names)
            {
              for (i = 0; node->attribute_names[i] != NULL; i++)
                {
                  g_string_append_printf (res, " %s=\"%s\"",
                                          node->attribute_names[i],
                                          node->attribute_values[i]);
                }
            }
          if (node->first_child == NULL)
            g_string_append (res, "/>");
          else
            g_string_append (res, ">");
        }

      child = node->first_child;
      while (child != NULL)
        {
          flatpak_xml_to_string (child, res);
          child = child->next_sibling;
        }
      if (node->parent != NULL)
        {
          if (node->first_child != NULL)
            g_string_append_printf (res, "</%s>", node->element_name);
        }

    }
  else if (node->text)
    {
      g_autofree char *escaped = g_markup_escape_text (node->text, -1);
      g_string_append (res, escaped);
    }
}

FlatpakXml *
flatpak_xml_unlink (FlatpakXml *node,
                    FlatpakXml *prev_sibling)
{
  FlatpakXml *parent = node->parent;

  if (parent == NULL)
    return node;

  if (parent->first_child == node)
    parent->first_child = node->next_sibling;

  if (parent->last_child == node)
    parent->last_child = prev_sibling;

  if (prev_sibling)
    prev_sibling->next_sibling = node->next_sibling;

  node->parent = NULL;
  node->next_sibling = NULL;

  return node;
}

FlatpakXml *
flatpak_xml_find (FlatpakXml  *node,
                  const char  *type,
                  FlatpakXml **prev_child_out)
{
  FlatpakXml *child = NULL;
  FlatpakXml *prev_child = NULL;

  child = node->first_child;
  prev_child = NULL;
  while (child != NULL)
    {
      FlatpakXml *next = child->next_sibling;

      if (g_strcmp0 (child->element_name, type) == 0)
        {
          if (prev_child_out)
            *prev_child_out = prev_child;
          return child;
        }

      prev_child = child;
      child = next;
    }

  return NULL;
}


FlatpakXml *
flatpak_xml_parse (GInputStream *in,
                   gboolean      compressed,
                   GCancellable *cancellable,
                   GError      **error)
{
  g_autoptr(GInputStream) real_in = NULL;
  g_autoptr(FlatpakXml) xml_root = NULL;
  XmlData data = { 0 };
  char buffer[32 * 1024];
  gssize len;
  g_autoptr(GMarkupParseContext) ctx = NULL;

  if (compressed)
    {
      g_autoptr(GZlibDecompressor) decompressor = NULL;
      decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
      real_in = g_converter_input_stream_new (in, G_CONVERTER (decompressor));
    }
  else
    {
      real_in = g_object_ref (in);
    }

  xml_root = flatpak_xml_new ("root");
  data.current = xml_root;

  ctx = g_markup_parse_context_new (&xml_parser,
                                    G_MARKUP_PREFIX_ERROR_POSITION,
                                    &data,
                                    NULL);

  while ((len = g_input_stream_read (real_in, buffer, sizeof (buffer),
                                     cancellable, error)) > 0)
    {
      if (!g_markup_parse_context_parse (ctx, buffer, len, error))
        return NULL;
    }

  if (len < 0)
    return NULL;

  return g_steal_pointer (&xml_root);
}

GBytes *
flatpak_read_stream (GInputStream *in,
                     gboolean      null_terminate,
                     GError      **error)
{
  g_autoptr(GOutputStream) mem_stream = NULL;

  mem_stream = g_memory_output_stream_new_resizable ();
  if (g_output_stream_splice (mem_stream, in,
                              0, NULL, error) < 0)
    return NULL;

  if (null_terminate)
    {
      if (!g_output_stream_write (G_OUTPUT_STREAM (mem_stream), "\0", 1, NULL, error))
        return NULL;
    }

  if (!g_output_stream_close (G_OUTPUT_STREAM (mem_stream), NULL, error))
    return NULL;

  return g_memory_output_stream_steal_as_bytes (G_MEMORY_OUTPUT_STREAM (mem_stream));
}

GVariant *
flatpak_variant_uncompress (GVariant *variant,
                            const GVariantType *type)
{
  g_autoptr(GInputStream) input_stream = NULL;
  g_autoptr(GZlibDecompressor) decompressor = NULL;
  g_autoptr(GInputStream) converter = NULL;
  g_autoptr(GBytes) decompressed_bytes = NULL;
  const guint8 *compressed;
  gsize compressed_size;

  g_assert (g_variant_is_of_type (variant, G_VARIANT_TYPE_BYTESTRING));

  compressed = g_variant_get_data (variant);
  compressed_size = g_variant_get_size (variant);

  input_stream = g_memory_input_stream_new_from_data (compressed, compressed_size, NULL);
  decompressor = g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP);
  converter = g_converter_input_stream_new (G_INPUT_STREAM (input_stream), G_CONVERTER (decompressor));
  decompressed_bytes = flatpak_read_stream (converter, FALSE, NULL);
  return g_variant_ref_sink (g_variant_new_from_bytes (type, decompressed_bytes, TRUE));
}

GVariant *
flatpak_variant_compress (GVariant *variant)
{
  g_autoptr(GInputStream) input_stream = NULL;
  g_autoptr(GZlibCompressor) compressor = NULL;
  g_autoptr(GInputStream) converter = NULL;
  g_autoptr(GBytes) compressed_bytes = NULL;
  const guint8 *decompressed;
  gsize decompressed_size;

  decompressed = g_variant_get_data (variant);
  decompressed_size = g_variant_get_size (variant);

  input_stream = g_memory_input_stream_new_from_data (decompressed, decompressed_size, NULL);
  compressor = g_zlib_compressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP, -1);
  converter = g_converter_input_stream_new (G_INPUT_STREAM (input_stream), G_CONVERTER (compressor));
  compressed_bytes = flatpak_read_stream (converter, FALSE, NULL);

  return g_variant_ref_sink (g_variant_new_from_bytes (G_VARIANT_TYPE_BYTESTRING, compressed_bytes, TRUE));
}

gboolean
flatpak_version_check (int major,
                       int minor,
                       int micro)
{
  static int flatpak_major = 0;
  static int flatpak_minor = 0;
  static int flatpak_micro = 0;

  if (flatpak_major == 0 &&
      flatpak_minor == 0 &&
      flatpak_micro == 0)
    {
      const char * argv[] = { "flatpak", "--version", NULL };
      g_autoptr(GSubprocess) subp = NULL;
      g_autofree char *out = NULL;

      subp = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL);
      g_subprocess_communicate_utf8 (subp, NULL, NULL, &out, NULL, NULL);

      if (sscanf (out, "Flatpak %d.%d.%d", &flatpak_major, &flatpak_minor, &flatpak_micro) != 3)
        g_warning ("Failed to get flatpak version");

      g_debug ("Using Flatpak version %d.%d.%d", flatpak_major, flatpak_minor, flatpak_micro);
    }

  if (flatpak_major > major)
    return TRUE;
  if (flatpak_major < major)
    return FALSE;
  if (flatpak_minor > minor)
    return TRUE;
  if (flatpak_minor < minor)
    return FALSE;
  if (flatpak_micro >= micro)
    return TRUE;

  return FALSE;
}
