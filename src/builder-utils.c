/* builder-utils.c
 *
 * Copyright (C) 2015 Red Hat, Inc
 * Copyright © 2023 GNOME Foundation Inc.
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
 *       Hubert Figuière <hub@figuiere.net>
 */

#include "config.h"

#include <stdlib.h>
#include <libelf.h>
#include <gelf.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <stdio.h>

#include <string.h>

#include <glib-unix.h>
#include <gio/gunixfdlist.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixinputstream.h>
#include <unistd.h>

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

  if (json == NULL)
    return NULL;

  if (JSON_NODE_TYPE (json) != JSON_NODE_OBJECT)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Unexpected identifier '%s'", json_to_string (json, FALSE));
      return NULL;
    }

  return json_gobject_deserialize (gtype, json);
}

char **
builder_get_debuginfo_file_references (const char *filename, GError **error)
{
  g_autofree char *tmp_path = NULL;
  glnx_autofd int tmp_fd = -1;
  g_autoptr(GSubprocess) subp = NULL;
  g_autoptr(GInputStream) input_stream = NULL;
  g_autoptr(GDataInputStream) data_stream = NULL;
  g_autoptr(GPtrArray) files = NULL;
  const char *debugedit = NULL;
  gboolean debugedit_succeeded = FALSE;

  tmp_path = g_build_filename (g_get_tmp_dir (), "flatpak-debugedit-list.XXXXXX", NULL);
  tmp_fd = g_mkstemp (tmp_path);
  if (tmp_fd == -1)
    {
      glnx_set_prefix_error_from_errno(error, "Creating temp file %s failed", tmp_path);
      return NULL;
    }

  debugedit = g_getenv ("FLATPAK_BUILDER_DEBUGEDIT");
  if (debugedit == NULL)
    debugedit = DEBUGEDIT;

  const char * argv[] = { debugedit, "-l", tmp_path, filename, NULL };

  subp = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_NONE, error);
  debugedit_succeeded = subp != NULL && g_subprocess_wait_check (subp, NULL, error);
  unlink (tmp_path);
  if (!debugedit_succeeded)
    {
      glnx_prefix_error (error, "Running debugedit failed");
      return NULL;
    }

  input_stream = g_unix_input_stream_new (tmp_fd, FALSE);
  data_stream = g_data_input_stream_new (input_stream);
  files = g_ptr_array_new_with_free_func (g_free);

  while (TRUE)
    {
      g_autoptr(GError) local_error = NULL;
      g_autofree char *file = g_data_input_stream_read_upto (data_stream,
                                                             "\0",
                                                             1,
                                                             NULL,
                                                             NULL,
                                                             &local_error);
      if (file == NULL)
        {
          /* Just hit EOF, so break out now */
          if (local_error == NULL)
            break;

          glnx_prefix_error (&local_error, "Reading debuginfo source files failed");
          g_propagate_error (error, local_error);
          return NULL;
        }

      /* Skip the \0 separator. */
      g_data_input_stream_read_byte (data_stream, NULL, NULL);

      if (*file == '\0')
        continue;

      g_ptr_array_add (files, g_steal_pointer (&file));
    }

  g_ptr_array_add (files, NULL);
  return (char**) g_ptr_array_free (g_steal_pointer (&files), FALSE);
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
                     const gchar * const  *argv,
                     const gchar * const  *unresolved_argv)
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

  if (unresolved_argv != NULL)
    commandline = flatpak_quote_argv ((const char **) unresolved_argv);
  else
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
                           const gchar * const  *argv,
                           const gchar * const  *unresolved_argv)
{
  if (flatpak_is_in_sandbox ())
    return builder_host_spawnv (dir, output, flags, error, argv, unresolved_argv);

  return flatpak_spawnv (dir, output, flags, error, argv, unresolved_argv);
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

gboolean
builder_download_uri_buffer (GUri           *uri,
                             const char     *http_referer,
                             gboolean        disable_http_decompression,
                             CURL           *session,
                             GOutputStream  *out,
                             GChecksum     **checksums,
                             gsize           n_checksums,
                             GError        **error)
{
  CURLcode retcode;
  CURLWriteData write_data;
  static gchar error_buffer[CURL_ERROR_SIZE];
  g_autofree gchar *url = g_uri_to_string (uri);

  curl_easy_setopt (session, CURLOPT_URL, url);
  curl_easy_setopt (session, CURLOPT_REFERER, http_referer);
  curl_easy_setopt (session, CURLOPT_WRITEFUNCTION, builder_curl_write_cb);
  curl_easy_setopt (session, CURLOPT_WRITEDATA, &write_data);
  curl_easy_setopt (session, CURLOPT_ERRORBUFFER, error_buffer);
  curl_easy_setopt (session, CURLOPT_NETRC, CURL_NETRC_OPTIONAL);

  if (!disable_http_decompression)
    curl_easy_setopt (session, CURLOPT_ACCEPT_ENCODING, "");

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
builder_download_uri (GUri           *uri,
                      const char     *http_referer,
                      gboolean        disable_http_decompression,
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

  if (!builder_download_uri_buffer (uri,
                                    http_referer,
                                    disable_http_decompression,
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
      !g_str_has_prefix (name, "//") &&
      g_strcmp0 (name, "$schema"))
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
flatpak_xml_new_with_attributes (const gchar *element_name,
                                 const gchar **attribute_names,
                                 const gchar **attribute_values)
{
  FlatpakXml *node = g_new0 (FlatpakXml, 1);

  node->element_name = g_strdup (element_name);
  node->attribute_names = g_strdupv ((char **) attribute_names);
  node->attribute_values = g_strdupv ((char **) attribute_values);

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

  node = flatpak_xml_new_with_attributes (element_name,
                                          attribute_names,
                                          attribute_values);

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

const gchar *
flatpak_xml_attribute (FlatpakXml *node, const gchar* name)
{
  if (node->attribute_names)
    for (int i = 0; node->attribute_names[i] != NULL; i++)
      if (g_strcmp0(node->attribute_names[i], name) == 0)
        return node->attribute_values[i];

  return NULL;
}

gboolean
flatpak_xml_set_attribute (FlatpakXml *node, const gchar* name, const gchar* value)
{
  if (node->attribute_names)
    for (int i = 0; node->attribute_names[i] != NULL; i++)
      if (g_strcmp0(node->attribute_names[i], name) == 0)
        {
          g_free (node->attribute_values[i]);
          node->attribute_values[i] = g_strdup (value);
          return TRUE;
        }

  return FALSE;
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
                  g_autofree char* attr =
                    g_markup_printf_escaped (" %s=\"%s\"",
                                             node->attribute_names[i],
                                             node->attribute_values[i]);
                  g_string_append (res, attr);
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
  return flatpak_xml_find_next (node, type, NULL, prev_child_out);
}

FlatpakXml *
flatpak_xml_find_next (FlatpakXml  *node,
                       const char  *type,
                       FlatpakXml  *sibling,
                       FlatpakXml **prev_child_out)
{
  FlatpakXml *child = NULL;
  FlatpakXml *prev_child = NULL;

  if (!sibling)
    {
      child = node->first_child;
      prev_child = NULL;
    }
  else
    {
      child = sibling->next_sibling;
      prev_child = sibling;
    }
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

gboolean
appstream_version_check (int major,
                         int minor,
                         int micro)
{
  static int as_major = 0;
  static int as_minor = 0;
  static int as_micro = 0;

  if (as_major == 0 &&
      as_minor == 0 &&
      as_micro == 0)
    {
      const char * argv[] = { "appstreamcli", "--version", NULL };
      g_autoptr(GSubprocess) subp = NULL;
      g_autofree char *out = NULL;
      g_auto(GStrv) lines = NULL;

      subp = g_subprocess_newv (argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE, NULL);
      g_subprocess_communicate_utf8 (subp, NULL, NULL, &out, NULL, NULL);

      lines = g_strsplit (out, "\n", -1);

      for (size_t i = 0; lines[i] != NULL; i++)
        {
          /* Only prefer library version over cli version in case of mismatch */
          if (g_str_has_prefix (lines[i], "AppStream library version:"))
            {
              if (sscanf (lines[i], "AppStream library version: %d.%d.%d", &as_major, &as_minor, &as_micro) == 3)
                break;
            }
          else if (g_str_has_prefix (lines[i], "AppStream version:"))
            {
              if (sscanf (lines[i], "AppStream version: %d.%d.%d", &as_major, &as_minor, &as_micro) == 3)
                break;
            }
        }

      if (as_major == 0 && as_minor == 0 && as_micro == 0)
        g_warning ("Failed to find appstream version");

      g_debug ("Found AppStream version %d.%d.%d", as_major, as_minor, as_micro);
    }

  return (as_major > major) ||
         (as_major == major && as_minor > minor) ||
         (as_major == major && as_minor == minor && as_micro >= micro);
}
