/*
 * Copyright © 2014 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include "builder-flatpak-utils.h"

#include <glib/gi18n.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>

#include <glib.h>
#include "libglnx/libglnx.h"
#include <libsoup/soup.h>
#include <gio/gunixoutputstream.h>
#include <gio/gunixinputstream.h>


GFile *
flatpak_file_new_tmp_in (GFile *dir,
                         const char *template,
                         GError        **error)
{
  glnx_fd_close int tmp_fd = -1;
  g_autofree char *tmpl = g_build_filename (flatpak_file_get_path_cached (dir), template, NULL);

  tmp_fd = g_mkstemp_full (tmpl, O_RDWR, 0644);
  if (tmp_fd == -1)
    {
      glnx_set_error_from_errno (error);
      return NULL;
    }

  return g_file_new_for_path (tmpl);
}

gboolean
flatpak_write_update_checksum (GOutputStream  *out,
                               gconstpointer   data,
                               gsize           len,
                               gsize          *out_bytes_written,
                               GChecksum     **checksums,
                               gsize           n_checksums,
                               GCancellable   *cancellable,
                               GError        **error)
{
  gsize i;

  if (out)
    {
      if (!g_output_stream_write_all (out, data, len, out_bytes_written,
                                      cancellable, error))
        return FALSE;
    }
  else if (out_bytes_written)
    {
      *out_bytes_written = len;
    }

  for (i = 0; i < n_checksums; i++)
    g_checksum_update (checksums[i], data, len);

  return TRUE;
}

gboolean
flatpak_splice_update_checksum (GOutputStream  *out,
                                GInputStream   *in,
                                GChecksum     **checksums,
                                gsize           n_checksums,
                                FlatpakLoadUriProgress progress,
                                gpointer        progress_data,
                                GCancellable   *cancellable,
                                GError        **error)
{
  gsize bytes_read, bytes_written;
  char buf[32*1024];
  guint64 downloaded_bytes = 0;
  gint64 progress_start;

  progress_start = g_get_monotonic_time ();
  do
    {
      if (!g_input_stream_read_all (in, buf, sizeof buf, &bytes_read, cancellable, error))
        return FALSE;

      if (!flatpak_write_update_checksum (out, buf, bytes_read, &bytes_written,
                                          checksums, n_checksums,
                                          cancellable, error))
        return FALSE;

      downloaded_bytes += bytes_read;

      if (progress &&
          g_get_monotonic_time () - progress_start >  5 * 1000000)
        {
          progress (downloaded_bytes, progress_data);
          progress_start = g_get_monotonic_time ();
        }
    }
  while (bytes_read > 0);

  if (progress)
    progress (downloaded_bytes, progress_data);

  return TRUE;
}

/* Returns end of matching path prefix, or NULL if no match */
const char *
flatpak_path_match_prefix (const char *pattern,
                           const char *string)
{
  char c, test;
  const char *tmp;

  while (*pattern == '/')
    pattern++;

  while (*string == '/')
    string++;

  while (TRUE)
    {
      switch (c = *pattern++)
        {
        case 0:
          if (*string == '/' || *string == 0)
            return string;
          return NULL;

        case '?':
          if (*string == '/' || *string == 0)
            return NULL;
          string++;
          break;

        case '*':
          c = *pattern;

          while (c == '*')
            c = *++pattern;

          /* special case * at end */
          if (c == 0)
            {
              char *tmp = strchr (string, '/');
              if (tmp != NULL)
                return tmp;
              return string + strlen (string);
            }
          else if (c == '/')
            {
              string = strchr (string, '/');
              if (string == NULL)
                return NULL;
              break;
            }

          while ((test = *string) != 0)
            {
              tmp = flatpak_path_match_prefix (pattern, string);
              if (tmp != NULL)
                return tmp;
              if (test == '/')
                break;
              string++;
            }
          return NULL;

        default:
          if (c != *string)
            return NULL;
          string++;
          break;
        }
    }
  return NULL; /* Should not be reached */
}

#if !defined(__i386__) && !defined(__x86_64__) && !defined(__aarch64__) && !defined(__arm__)
static const char *
flatpak_get_kernel_arch (void)
{
  static struct utsname buf;
  static char *arch = NULL;
  char *m;

  if (arch != NULL)
    return arch;

  if (uname (&buf))
    {
      arch = "unknown";
      return arch;
    }

  /* By default, just pass on machine, good enough for most arches */
  arch = buf.machine;

  /* Override for some arches */

  m = buf.machine;
  /* i?86 */
  if (strlen (m) == 4 && m[0] == 'i' && m[2] == '8'  && m[3] == '6')
    {
      arch = "i386";
    }
  else if (g_str_has_prefix (m, "arm"))
    {
      if (g_str_has_suffix (m, "b"))
        arch = "armeb";
      else
        arch = "arm";
    }
  else if (strcmp (m, "mips") == 0)
    {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      arch = "mipsel";
#endif
    }
  else if (strcmp (m, "mips64") == 0)
    {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
      arch = "mips64el";
#endif
    }

  return arch;
}
#endif  /* !__i386__ && !__x86_64__ && !__aarch64__ && !__arm__ */

/* This maps the kernel-reported uname to a single string representing
 * the cpu family, in the sense that all members of this family would
 * be able to understand and link to a binary file with such cpu
 * opcodes. That doesn't necessarily mean that all members of the
 * family can run all opcodes, for instance for modern 32bit intel we
 * report "i386", even though they support instructions that the
 * original i386 cpu cannot run. Still, such an executable would
 * at least try to execute a 386, whereas an arm binary would not.
 */
const char *
flatpak_get_arch (void)
{
  /* Avoid using uname on multiarch machines, because uname reports the kernels
   * arch, and that may be different from userspace. If e.g. the kernel is 64bit and
   * the userspace is 32bit we want to use 32bit by default. So, we take the current build
   * arch as the default. */
#if defined(__i386__)
  return "i386";
#elif defined(__x86_64__)
  return "x86_64";
#elif defined(__aarch64__)
  return "aarch64";
#elif defined(__arm__)
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  return "arm";
#else
  return "armeb";
#endif
#else
  return flatpak_get_kernel_arch ();
#endif
}

gboolean
flatpak_is_in_sandbox (void)
{
  static gsize in_sandbox = 0;

  if (g_once_init_enter (&in_sandbox))
    {
      g_autofree char *path = g_build_filename (g_get_user_runtime_dir (), "flatpak-info", NULL);
      gsize new_in_sandbox;

      new_in_sandbox = 2;
      if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
        new_in_sandbox = 1;

      g_once_init_leave (&in_sandbox, new_in_sandbox);
 }

  return in_sandbox == 1;
}

gboolean
flatpak_break_hardlink (GFile *file, GError **error)
{
  g_autofree char *path = g_file_get_path (file);
  struct stat st_buf;

  if (stat (path, &st_buf) == 0 && st_buf.st_nlink > 1)
    {
      g_autoptr(GFile) dir = g_file_get_parent (file);
      g_autoptr(GFile) tmp = NULL;

      tmp = flatpak_file_new_tmp_in (dir, ".breaklinkXXXXXX", error);
      if (tmp == NULL)
        return FALSE;

      if (!g_file_copy (file, tmp,
                        G_FILE_COPY_OVERWRITE,
                        NULL, NULL, NULL, error))
        return FALSE;

      if (rename (flatpak_file_get_path_cached (tmp), path) != 0)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
is_valid_initial_name_character (gint c, gboolean allow_dash)
{
  return
    (c >= 'A' && c <= 'Z') ||
    (c >= 'a' && c <= 'z') ||
    (c == '_') || (allow_dash && c == '-');
}

static gboolean
is_valid_name_character (gint c, gboolean allow_dash)
{
  return
    is_valid_initial_name_character (c, allow_dash) ||
    (c >= '0' && c <= '9');
}

gboolean
flatpak_has_name_prefix (const char *string,
                         const char *name)
{
  const char *rest;

  if (!g_str_has_prefix (string, name))
    return FALSE;

  rest = string + strlen (name);
  return
    *rest == 0 ||
    *rest == '.' ||
    !is_valid_name_character (*rest, FALSE);
}


/* Dashes are only valid in the last part of the app id, so
   we replace them with underscore so we can suffix the id */
char *
flatpak_make_valid_id_prefix (const char *orig_id)
{
  char *id, *t;

  id = g_strdup (orig_id);
  t = id;
  while (*t != 0 && *t != '/')
    {
      if (*t == '-')
        *t = '_';

      t++;
    }

  return id;
}

char *
flatpak_compose_ref (gboolean    app,
                     const char *name,
                     const char *branch,
                     const char *arch)
{
  if (app)
    return flatpak_build_app_ref (name, branch, arch);
  else
    return flatpak_build_runtime_ref (name, branch, arch);
}

char *
flatpak_build_untyped_ref (const char *runtime,
                           const char *branch,
                           const char *arch)
{
  if (arch == NULL)
    arch = flatpak_get_arch ();

  return g_build_filename (runtime, arch, branch, NULL);
}

char *
flatpak_build_runtime_ref (const char *runtime,
                           const char *branch,
                           const char *arch)
{
  if (branch == NULL)
    branch = "master";

  if (arch == NULL)
    arch = flatpak_get_arch ();

  return g_build_filename ("runtime", runtime, arch, branch, NULL);
}

char *
flatpak_build_app_ref (const char *app,
                       const char *branch,
                       const char *arch)
{
  if (branch == NULL)
    branch = "master";

  if (arch == NULL)
    arch = flatpak_get_arch ();

  return g_build_filename ("app", app, arch, branch, NULL);
}

typedef struct
{
  GError    *error;
  GError    *splice_error;
  GMainLoop *loop;
  int        refs;
} SpawnData;

static void
spawn_data_exit (SpawnData *data)
{
  data->refs--;
  if (data->refs == 0)
    g_main_loop_quit (data->loop);
}

static void
spawn_output_spliced_cb (GObject      *obj,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  SpawnData *data = user_data;

  g_output_stream_splice_finish (G_OUTPUT_STREAM (obj), result, &data->splice_error);
  spawn_data_exit (data);
}

static void
spawn_exit_cb (GObject      *obj,
               GAsyncResult *result,
               gpointer      user_data)
{
  SpawnData *data = user_data;

  g_subprocess_wait_check_finish (G_SUBPROCESS (obj), result, &data->error);
  spawn_data_exit (data);
}

static gboolean
needs_quoting (const char *arg)
{
  while (*arg != 0)
    {
      char c = *arg;
      if (!g_ascii_isalnum (c) &&
          !(c == '-' || c == '/' || c == '~' ||
            c == ':' || c == '.' || c == '_' ||
            c == '='))
        return TRUE;
      arg++;
    }
  return FALSE;
}

char *
flatpak_quote_argv (const char *argv[])
{
  GString *res = g_string_new ("");
  int i;

  for (i = 0; argv[i] != NULL; i++)
    {
      if (i != 0)
        g_string_append_c (res, ' ');

      if (needs_quoting (argv[i]))
        {
          g_autofree char *quoted = g_shell_quote (argv[i]);
          g_string_append (res, quoted);
        }
      else
        g_string_append (res, argv[i]);
    }

  return g_string_free (res, FALSE);
}

gboolean
flatpak_spawn (GFile       *dir,
               char       **output,
               GSubprocessFlags flags,
               GError     **error,
               const gchar *argv0,
               va_list      ap)
{
  GPtrArray *args;
  const gchar *arg;
  gboolean res;

  args = g_ptr_array_new ();
  g_ptr_array_add (args, (gchar *) argv0);
  while ((arg = va_arg (ap, const gchar *)))
    g_ptr_array_add (args, (gchar *) arg);
  g_ptr_array_add (args, NULL);

  res = flatpak_spawnv (dir, output, flags, error, (const gchar * const *) args->pdata);

  g_ptr_array_free (args, TRUE);

  return res;
}

gboolean
flatpak_spawnv (GFile                *dir,
                char                **output,
                GSubprocessFlags      flags,
                GError              **error,
                const gchar * const  *argv)
{
  g_autoptr(GSubprocessLauncher) launcher = NULL;
  g_autoptr(GSubprocess) subp = NULL;
  GInputStream *in;
  g_autoptr(GOutputStream) out = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  SpawnData data = {0};
  g_autofree gchar *commandline = NULL;

  launcher = g_subprocess_launcher_new (0);

  g_subprocess_launcher_setenv (launcher, "LANGUAGE", "C", TRUE);

  if (output)
    flags |= G_SUBPROCESS_FLAGS_STDOUT_PIPE;

  g_subprocess_launcher_set_flags (launcher, flags);

  if (dir)
    {
      g_autofree char *path = g_file_get_path (dir);
      g_subprocess_launcher_set_cwd (launcher, path);
    }

  commandline = flatpak_quote_argv ((const char **)argv);
  g_debug ("Running: %s", commandline);

  subp = g_subprocess_launcher_spawnv (launcher, argv, error);

  if (subp == NULL)
    return FALSE;

  loop = g_main_loop_new (NULL, FALSE);

  data.loop = loop;
  data.refs = 1;

  if (output)
    {
      data.refs++;
      in = g_subprocess_get_stdout_pipe (subp);
      out = g_memory_output_stream_new_resizable ();
      g_output_stream_splice_async (out,
                                    in,
                                    G_OUTPUT_STREAM_SPLICE_NONE,
                                    0,
                                    NULL,
                                    spawn_output_spliced_cb,
                                    &data);
    }

  g_subprocess_wait_async (subp, NULL, spawn_exit_cb, &data);

  g_main_loop_run (loop);

  if (data.error)
    {
      g_propagate_error (error, data.error);
      g_clear_error (&data.splice_error);
      return FALSE;
    }

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

GFile *
flatpak_build_file_va (GFile *base,
                       va_list args)
{
  g_autoptr(GFile) res = g_object_ref (base);
  const gchar *arg;

  while ((arg = va_arg (args, const gchar *)))
    {
      GFile *child = g_file_resolve_relative_path (res, arg);
      g_set_object (&res, child);
    }

  return g_steal_pointer (&res);
}

GFile *
flatpak_build_file (GFile *base, ...)
{
  GFile *res;
  va_list args;

  va_start (args, base);
  res = flatpak_build_file_va (base, args);
  va_end (args);

  return res;
}

const char *
flatpak_file_get_path_cached (GFile *file)
{
  const char *path;
  static GQuark _file_path_quark = 0;

  if (G_UNLIKELY (_file_path_quark == 0))
    _file_path_quark = g_quark_from_static_string ("flatpak-file-path");

  do
    {
      path = g_object_get_qdata ((GObject*)file, _file_path_quark);
      if (path == NULL)
        {
          g_autofree char *new_path = NULL;
          new_path = g_file_get_path (file);
          if (new_path == NULL)
            return NULL;

          if (g_object_replace_qdata ((GObject*)file, _file_path_quark,
                                      NULL, new_path, g_free, NULL))
            path = g_steal_pointer (&new_path);
        }
    }
  while (path == NULL);

  return path;
}

gboolean
flatpak_file_query_exists_nofollow (GFile *file)
{
  GFileInfo *info;

  info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE,
                            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, FALSE, NULL);
  if (info != NULL)
    {
      g_object_unref (info);
      return TRUE;
    }

  return FALSE;
}

/* NOTE: This requires that file exists */
GFile *
flatpak_canonicalize_file (GFile *file, GError **error)
{
  g_autofree char *canonical_path = NULL;

  canonical_path = realpath (flatpak_file_get_path_cached (file), NULL);
  if (canonical_path == NULL)
    {
      glnx_set_error_from_errno (error);
      return NULL;
    }

  return g_file_new_for_path (canonical_path);
}

/* NOTE: This requires both files to exist */
gboolean
flatpak_file_is_in (GFile *file,
                    GFile *toplevel)
{
  g_autoptr(GFile) canonical_file = NULL;
  g_autoptr(GFile) canonical_toplevel = NULL;

  canonical_toplevel = flatpak_canonicalize_file (toplevel, NULL);
  if (canonical_toplevel == NULL)
    return FALSE;

  canonical_file = flatpak_canonicalize_file (file, NULL);
  if (canonical_file == NULL)
    return FALSE;

  return g_file_equal (canonical_file, canonical_toplevel) ||
    g_file_has_prefix (canonical_file, canonical_toplevel);
}

gboolean
flatpak_cp_a (GFile         *src,
              GFile         *dest,
              GFile         *keep_in_toplevel,
              FlatpakCpFlags flags,
              GPtrArray     *skip_files,
              GCancellable  *cancellable,
              GError       **error)
{
  gboolean ret = FALSE;
  GFileEnumerator *enumerator = NULL;
  GFileInfo *src_info = NULL;
  GFile *dest_child = NULL;
  int dest_dfd = -1;
  gboolean merge = (flags & FLATPAK_CP_FLAGS_MERGE) != 0;
  gboolean no_chown = (flags & FLATPAK_CP_FLAGS_NO_CHOWN) != 0;
  gboolean move = (flags & FLATPAK_CP_FLAGS_MOVE) != 0;
  g_autoptr(GFileInfo) child_info = NULL;
  GError *temp_error = NULL;
  int r;

  enumerator = g_file_enumerate_children (src, "standard::type,standard::name,unix::uid,unix::gid,unix::mode",
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                          cancellable, error);
  if (!enumerator)
    goto out;

  src_info = g_file_query_info (src, "standard::name,unix::mode,unix::uid,unix::gid," \
                                     "time::modified,time::modified-usec,time::access,time::access-usec",
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                cancellable, error);
  if (!src_info)
    goto out;

  do
    r = mkdir (flatpak_file_get_path_cached (dest), 0755);
  while (G_UNLIKELY (r == -1 && errno == EINTR));
  if (r == -1)
    {
      if (!merge || errno != EEXIST)
        {
          glnx_set_error_from_errno (error);
          goto out;
        }

      /* When merging, ensure the new dir is inside the toplevel instead of a symlink outside */
      if (keep_in_toplevel != NULL && !flatpak_file_is_in (dest, keep_in_toplevel))
        {
          flatpak_fail (error, "Recursive copy outside destination bounds");
          goto out;
        }
    }

  if (!glnx_opendirat (AT_FDCWD, flatpak_file_get_path_cached (dest), TRUE,
                       &dest_dfd, error))
    goto out;

  if (!no_chown)
    {
      do
        r = fchown (dest_dfd,
                    g_file_info_get_attribute_uint32 (src_info, "unix::uid"),
                    g_file_info_get_attribute_uint32 (src_info, "unix::gid"));
      while (G_UNLIKELY (r == -1 && errno == EINTR));
      if (r == -1)
        {
          glnx_set_error_from_errno (error);
          goto out;
        }
    }

  do
    r = fchmod (dest_dfd, g_file_info_get_attribute_uint32 (src_info, "unix::mode"));
  while (G_UNLIKELY (r == -1 && errno == EINTR));

  if (dest_dfd != -1)
    {
      (void) close (dest_dfd);
      dest_dfd = -1;
    }

  while ((child_info = g_file_enumerator_next_file (enumerator, cancellable, &temp_error)))
    {
      const char *name = g_file_info_get_name (child_info);
      g_autoptr(GFile) src_child = g_file_get_child (src, name);
      gboolean skip = FALSE;
      int i;

      for (i = 0; skip_files != NULL && i < skip_files->len; i++)
        {
          if (g_file_equal (src_child, g_ptr_array_index (skip_files, i)))
            {
              skip = TRUE;
              break;
            }
        }

      if (dest_child)
        g_object_unref (dest_child);
      dest_child = g_file_get_child (dest, name);

      if (skip)
        {
          /* skip src */
        }
      else if (g_file_info_get_file_type (child_info) == G_FILE_TYPE_DIRECTORY)
        {
          if (!flatpak_cp_a (src_child, dest_child, keep_in_toplevel, flags, skip_files,
                             cancellable, error))
            goto out;
        }
      else
        {
          (void) unlink (flatpak_file_get_path_cached (dest_child));
          GFileCopyFlags copyflags = G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS;
          if (!no_chown)
            copyflags |= G_FILE_COPY_ALL_METADATA;
          if (move)
            {
              if (!g_file_move (src_child, dest_child, copyflags,
                                cancellable, NULL, NULL, error))
                goto out;
            }
          else
            {
              if (!g_file_copy (src_child, dest_child, copyflags,
                                cancellable, NULL, NULL, error))
                goto out;
            }
        }

      g_clear_object (&child_info);
    }

  if (temp_error != NULL)
    {
      g_propagate_error (error, temp_error);
      goto out;
    }

  if (move &&
      !g_file_delete (src, NULL, error))
    goto out;

  ret = TRUE;
out:
  if (dest_dfd != -1)
    (void) close (dest_dfd);
  g_clear_object (&src_info);
  g_clear_object (&enumerator);
  g_clear_object (&dest_child);
  return ret;
}

gboolean
flatpak_zero_mtime (int parent_dfd,
                    const char *rel_path,
                    GCancellable  *cancellable,
                    GError       **error)
{
  struct stat stbuf;

  if (TEMP_FAILURE_RETRY (fstatat (parent_dfd, rel_path, &stbuf, AT_SYMLINK_NOFOLLOW)) != 0)
    {
      glnx_set_error_from_errno (error);
      return FALSE;
    }

  if (S_ISDIR (stbuf.st_mode))
    {
      g_auto(GLnxDirFdIterator) dfd_iter = { 0, };
      gboolean inited;

      inited = glnx_dirfd_iterator_init_at (parent_dfd, rel_path, FALSE, &dfd_iter, NULL);

      while (inited)
        {
          struct dirent *dent;

          if (!glnx_dirfd_iterator_next_dent (&dfd_iter, &dent, NULL, NULL) || dent == NULL)
            break;

          if (!flatpak_zero_mtime (dfd_iter.fd, dent->d_name,
                                   cancellable, error))
            return FALSE;
        }

      /* Update stbuf */
      if (TEMP_FAILURE_RETRY (fstat (dfd_iter.fd, &stbuf)) != 0)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }
    }

  /* OSTree checks out to mtime 0, so we do the same */
  if (stbuf.st_mtime != OSTREE_TIMESTAMP)
    {
      const struct timespec times[2] = { { 0, UTIME_OMIT }, { OSTREE_TIMESTAMP, } };

      if (TEMP_FAILURE_RETRY (utimensat (parent_dfd, rel_path, times, AT_SYMLINK_NOFOLLOW)) != 0)
        {
          glnx_set_error_from_errno (error);
          return FALSE;
        }
    }

  return TRUE;
}

/* Make a directory, and its parent. Don't error if it already exists.
 * If you want a failure mode with EEXIST, use g_file_make_directory_with_parents. */
gboolean
flatpak_mkdir_p (GFile         *dir,
                 GCancellable  *cancellable,
                 GError       **error)
{
  return glnx_shutil_mkdir_p_at (AT_FDCWD,
                                 flatpak_file_get_path_cached (dir),
                                 0777,
                                 cancellable,
                                 error);
}

gboolean
flatpak_rm_rf (GFile         *dir,
               GCancellable  *cancellable,
               GError       **error)
{
  return glnx_shutil_rm_rf_at (AT_FDCWD,
                               flatpak_file_get_path_cached (dir),
                               cancellable, error);
}

gboolean flatpak_file_rename (GFile *from,
                              GFile *to,
                              GCancellable  *cancellable,
                              GError       **error)
{
  if (g_cancellable_set_error_if_cancelled (cancellable, error))
    return FALSE;

  if (rename (flatpak_file_get_path_cached (from),
              flatpak_file_get_path_cached (to)) < 0)
    {
      glnx_set_error_from_errno (error);
      return FALSE;
    }

  return TRUE;
}

#define OSTREE_GIO_FAST_QUERYINFO ("standard::name,standard::type,standard::size,standard::is-symlink,standard::symlink-target," \
                                   "unix::device,unix::inode,unix::mode,unix::uid,unix::gid,unix::rdev")

/* This allocates and locks a subdir of the tmp dir, using an existing
 * one with the same prefix if it is not in use already. */
gboolean
flatpak_allocate_tmpdir (int           tmpdir_dfd,
                         const char   *tmpdir_relpath,
                         const char   *tmpdir_prefix,
                         char        **tmpdir_name_out,
                         int          *tmpdir_fd_out,
                         GLnxLockFile *file_lock_out,
                         gboolean     *reusing_dir_out,
                         GCancellable *cancellable,
                         GError      **error)
{
  gboolean reusing_dir = FALSE;
  g_autofree char *tmpdir_name = NULL;
  glnx_fd_close int tmpdir_fd = -1;

  g_auto(GLnxDirFdIterator) dfd_iter = { 0, };

  /* Look for existing tmpdir (with same prefix) to reuse */
  if (!glnx_dirfd_iterator_init_at (tmpdir_dfd, tmpdir_relpath ? tmpdir_relpath : ".", FALSE, &dfd_iter, error))
    return FALSE;

  while (tmpdir_name == NULL)
    {
      struct dirent *dent;
      glnx_fd_close int existing_tmpdir_fd = -1;
      g_autoptr(GError) local_error = NULL;
      g_autofree char *lock_name = NULL;

      if (!glnx_dirfd_iterator_next_dent (&dfd_iter, &dent, cancellable, error))
        return FALSE;

      if (dent == NULL)
        break;

      if (!g_str_has_prefix (dent->d_name, tmpdir_prefix))
        continue;

      /* Quickly skip non-dirs, if unknown we ignore ENOTDIR when opening instead */
      if (dent->d_type != DT_UNKNOWN &&
          dent->d_type != DT_DIR)
        continue;

      if (!glnx_opendirat (dfd_iter.fd, dent->d_name, FALSE,
                           &existing_tmpdir_fd, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY))
            {
              continue;
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }

      lock_name = g_strconcat (dent->d_name, "-lock", NULL);

      /* We put the lock outside the dir, so we can hold the lock
       * until the directory is fully removed */
      if (!glnx_make_lock_file (dfd_iter.fd, lock_name, LOCK_EX | LOCK_NB,
                                file_lock_out, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            {
              continue;
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }

      /* Touch the reused directory so that we don't accidentally
       *   remove it due to being old when cleaning up the tmpdir
       */
      (void) futimens (existing_tmpdir_fd, NULL);

      /* We found an existing tmpdir which we managed to lock */
      tmpdir_name = g_strdup (dent->d_name);
      tmpdir_fd = glnx_steal_fd (&existing_tmpdir_fd);
      reusing_dir = TRUE;
    }

  while (tmpdir_name == NULL)
    {
      g_autofree char *tmpdir_name_template = g_strconcat (tmpdir_prefix, "XXXXXX", NULL);
      g_autoptr(GError) local_error = NULL;
      g_autofree char *lock_name = NULL;
      g_auto(GLnxTmpDir) new_tmpdir = { 0, };

      /* No existing tmpdir found, create a new */

      if (!glnx_mkdtempat (dfd_iter.fd, tmpdir_name_template, 0777,
                           &new_tmpdir, error))
        return FALSE;

      lock_name = g_strconcat (new_tmpdir.path, "-lock", NULL);

      /* Note, at this point we can race with another process that picks up this
       * new directory. If that happens we need to retry, making a new directory. */
      if (!glnx_make_lock_file (dfd_iter.fd, lock_name, LOCK_EX | LOCK_NB,
                                file_lock_out, &local_error))
        {
          if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
            {
              glnx_tmpdir_unset (&new_tmpdir); /* Don't delete */
              continue;
            }
          else
            {
              g_propagate_error (error, g_steal_pointer (&local_error));
              return FALSE;
            }
        }

      tmpdir_name = g_strdup (new_tmpdir.path);
      tmpdir_fd = dup (new_tmpdir.fd);
      glnx_tmpdir_unset (&new_tmpdir); /* Don't delete */
    }

  if (tmpdir_name_out)
    *tmpdir_name_out = g_steal_pointer (&tmpdir_name);

  if (tmpdir_fd_out)
    *tmpdir_fd_out = glnx_steal_fd (&tmpdir_fd);

  if (reusing_dir_out)
    *reusing_dir_out = reusing_dir;

  return TRUE;
}


SoupSession *
flatpak_create_soup_session (const char *user_agent)
{
  SoupSession *soup_session;
  const char *http_proxy;

  soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, user_agent,
                                                SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
                                                SOUP_SESSION_USE_THREAD_CONTEXT, TRUE,
                                                SOUP_SESSION_TIMEOUT, 60,
                                                SOUP_SESSION_IDLE_TIMEOUT, 60,
                                                NULL);
  soup_session_remove_feature_by_type (soup_session, SOUP_TYPE_CONTENT_DECODER);
  http_proxy = g_getenv ("http_proxy");
  if (http_proxy)
    {
      g_autoptr(SoupURI) proxy_uri = soup_uri_new (http_proxy);
      if (!proxy_uri)
        g_warning ("Invalid proxy URI '%s'", http_proxy);
      else
        g_object_set (soup_session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
    }

  return soup_session;
}

CURL *
flatpak_create_curl_session (const char *user_agent)
{
  CURL *curl_session;

  curl_global_init (CURL_GLOBAL_DEFAULT);

  curl_session = curl_easy_init ();
  if (curl_session == NULL)
    return NULL;

  curl_easy_setopt (curl_session, CURLOPT_CONNECTTIMEOUT, 60);
  curl_easy_setopt (curl_session, CURLOPT_FAILONERROR, 1);
  curl_easy_setopt (curl_session, CURLOPT_FOLLOWLOCATION, 1);
  curl_easy_setopt (curl_session, CURLOPT_MAXREDIRS, 50);
  curl_easy_setopt (curl_session, CURLOPT_NOPROGRESS, 0);
  curl_easy_setopt (curl_session, CURLOPT_USERAGENT, user_agent);

  return curl_session;
}

typedef enum {
  FLATPAK_POLICY_NONE,
  FLATPAK_POLICY_SEE,
  FLATPAK_POLICY_TALK,
  FLATPAK_POLICY_OWN
} FlatpakPolicy;

typedef enum {
  FLATPAK_CONTEXT_SHARED_NETWORK   = 1 << 0,
  FLATPAK_CONTEXT_SHARED_IPC       = 1 << 1,
} FlatpakContextShares;

/* In numerical order of more privs */
typedef enum {
  FLATPAK_FILESYSTEM_MODE_READ_ONLY    = 1,
  FLATPAK_FILESYSTEM_MODE_READ_WRITE   = 2,
  FLATPAK_FILESYSTEM_MODE_CREATE       = 3,
} FlatpakFilesystemMode;


/* Same order as enum */
const char *flatpak_context_shares[] = {
  "network",
  "ipc",
  NULL
};

typedef enum {
  FLATPAK_CONTEXT_SOCKET_X11         = 1 << 0,
  FLATPAK_CONTEXT_SOCKET_WAYLAND     = 1 << 1,
  FLATPAK_CONTEXT_SOCKET_PULSEAUDIO  = 1 << 2,
  FLATPAK_CONTEXT_SOCKET_SESSION_BUS = 1 << 3,
  FLATPAK_CONTEXT_SOCKET_SYSTEM_BUS  = 1 << 4,
  FLATPAK_CONTEXT_SOCKET_FALLBACK_X11 = 1 << 5, /* For backwards compat, also set SOCKET_X11 */
} FlatpakContextSockets;

/* Same order as enum */
const char *flatpak_context_sockets[] = {
  "x11",
  "wayland",
  "pulseaudio",
  "session-bus",
  "system-bus",
  "fallback-x11",
  NULL
};

const char *dont_mount_in_root[] = {
  ".", "..", "lib", "lib32", "lib64", "bin", "sbin", "usr", "boot", "root",
  "tmp", "etc", "app", "run", "proc", "sys", "dev", "var", NULL
};

/* We don't want to export paths pointing into these, because they are readonly
   (so we can't create mountpoints there) and don't match what's on the host anyway */
const char *dont_export_in[] = {
  "/lib", "/lib32", "/lib64", "/bin", "/sbin", "/usr", "/etc", "/app", NULL
};

typedef enum {
  FLATPAK_CONTEXT_DEVICE_DRI         = 1 << 0,
  FLATPAK_CONTEXT_DEVICE_ALL         = 1 << 1,
  FLATPAK_CONTEXT_DEVICE_KVM         = 1 << 2,
} FlatpakContextDevices;

const char *flatpak_context_devices[] = {
  "dri",
  "all",
  "kvm",
  NULL
};

typedef enum {
  FLATPAK_CONTEXT_FEATURE_DEVEL        = 1 << 0,
  FLATPAK_CONTEXT_FEATURE_MULTIARCH    = 1 << 1,
} FlatpakContextFeatures;

const char *flatpak_context_features[] = {
  "devel",
  "multiarch",
  NULL
};

struct FlatpakContext
{
  FlatpakContextShares  shares;
  FlatpakContextShares  shares_valid;
  FlatpakContextSockets sockets;
  FlatpakContextSockets sockets_valid;
  FlatpakContextDevices devices;
  FlatpakContextDevices devices_valid;
  FlatpakContextFeatures  features;
  FlatpakContextFeatures  features_valid;
  GHashTable           *env_vars;
  GHashTable           *persistent;
  GHashTable           *filesystems;
  GHashTable           *session_bus_policy;
  GHashTable           *system_bus_policy;
  GHashTable           *generic_policy;
};

FlatpakContext *
flatpak_context_new (void)
{
  FlatpakContext *context;

  context = g_slice_new0 (FlatpakContext);
  context->env_vars = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  context->persistent = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  context->filesystems = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  context->session_bus_policy = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  context->system_bus_policy = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  context->generic_policy = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   g_free, (GDestroyNotify)g_strfreev);

  return context;
}

void
flatpak_context_free (FlatpakContext *context)
{
  g_hash_table_destroy (context->env_vars);
  g_hash_table_destroy (context->persistent);
  g_hash_table_destroy (context->filesystems);
  g_hash_table_destroy (context->session_bus_policy);
  g_hash_table_destroy (context->system_bus_policy);
  g_hash_table_destroy (context->generic_policy);
  g_slice_free (FlatpakContext, context);
}

static guint32
flatpak_context_bitmask_from_string (const char *name, const char **names)
{
  guint32 i;

  for (i = 0; names[i] != NULL; i++)
    {
      if (strcmp (names[i], name) == 0)
        return 1 << i;
    }

  return 0;
}

static void
flatpak_context_bitmask_to_args (guint32 enabled, guint32 valid, const char **names,
                                 const char *enable_arg, const char *disable_arg,
                                 GPtrArray *args)
{
  guint32 i;

  for (i = 0; names[i] != NULL; i++)
    {
      guint32 bitmask = 1 << i;
      if (valid & bitmask)
        {
          if (enabled & bitmask)
            g_ptr_array_add (args, g_strdup_printf ("%s=%s", enable_arg, names[i]));
          else
            g_ptr_array_add (args, g_strdup_printf ("%s=%s", disable_arg, names[i]));
        }
    }
}


static FlatpakContextShares
flatpak_context_share_from_string (const char *string, GError **error)
{
  FlatpakContextShares shares = flatpak_context_bitmask_from_string (string, flatpak_context_shares);

  if (shares == 0)
    {
      g_autofree char *values = g_strjoinv (", ", (char **)flatpak_context_shares);
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   _("Unknown share type %s, valid types are: %s"), string, values);
    }

  return shares;
}

static void
flatpak_context_shared_to_args (FlatpakContextShares shares,
                                FlatpakContextShares valid,
                                GPtrArray *args)
{
  return flatpak_context_bitmask_to_args (shares, valid, flatpak_context_shares, "--share", "--unshare", args);
}

static const char *
flatpak_policy_to_string (FlatpakPolicy policy)
{
  if (policy == FLATPAK_POLICY_SEE)
    return "see";
  if (policy == FLATPAK_POLICY_TALK)
    return "talk";
  if (policy == FLATPAK_POLICY_OWN)
    return "own";

  return "none";
}

static gboolean
flatpak_verify_dbus_name (const char *name, GError **error)
{
  const char *name_part;
  g_autofree char *tmp = NULL;

  if (g_str_has_suffix (name, ".*"))
    {
      tmp = g_strndup (name, strlen (name) - 2);
      name_part = tmp;
    }
  else
    {
      name_part = name;
    }

  if (g_dbus_is_name (name_part) && !g_dbus_is_unique_name (name_part))
    return TRUE;

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               _("Invalid dbus name %s\n"), name);
  return FALSE;
}

static FlatpakContextSockets
flatpak_context_socket_from_string (const char *string, GError **error)
{
  FlatpakContextSockets sockets = flatpak_context_bitmask_from_string (string, flatpak_context_sockets);

  if (sockets == 0)
    {
      g_autofree char *values = g_strjoinv (", ", (char **)flatpak_context_sockets);
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   _("Unknown socket type %s, valid types are: %s"), string, values);
    }

  return sockets;
}

static void
flatpak_context_sockets_to_args (FlatpakContextSockets sockets,
                                 FlatpakContextSockets valid,
                                 GPtrArray *args)
{
  return flatpak_context_bitmask_to_args (sockets, valid, flatpak_context_sockets, "--socket", "--nosocket", args);
}

static FlatpakContextDevices
flatpak_context_device_from_string (const char *string, GError **error)
{
  FlatpakContextDevices devices = flatpak_context_bitmask_from_string (string, flatpak_context_devices);

  if (devices == 0)
    {
      g_autofree char *values = g_strjoinv (", ", (char **)flatpak_context_devices);
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   _("Unknown device type %s, valid types are: %s"), string, values);
    }
  return devices;
}

static void
flatpak_context_devices_to_args (FlatpakContextDevices devices,
                                 FlatpakContextDevices valid,
                                 GPtrArray *args)
{
  return flatpak_context_bitmask_to_args (devices, valid, flatpak_context_devices, "--device", "--nodevice", args);
}

static FlatpakContextFeatures
flatpak_context_feature_from_string (const char *string, GError **error)
{
  FlatpakContextFeatures feature = flatpak_context_bitmask_from_string (string, flatpak_context_features);

  if (feature == 0)
    {
      g_autofree char *values = g_strjoinv (", ", (char **)flatpak_context_features);
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   _("Unknown feature type %s, valid types are: %s"), string, values);
    }

  return feature;
}

static void
flatpak_context_features_to_args (FlatpakContextFeatures features,
                                FlatpakContextFeatures valid,
                                GPtrArray *args)
{
  return flatpak_context_bitmask_to_args (features, valid, flatpak_context_features, "--allow", "--disallow", args);
}

static void
flatpak_context_add_shares (FlatpakContext      *context,
                            FlatpakContextShares shares)
{
  context->shares_valid |= shares;
  context->shares |= shares;
}

static void
flatpak_context_remove_shares (FlatpakContext      *context,
                               FlatpakContextShares shares)
{
  context->shares_valid |= shares;
  context->shares &= ~shares;
}

static void
flatpak_context_add_sockets (FlatpakContext       *context,
                             FlatpakContextSockets sockets)
{
  context->sockets_valid |= sockets;
  context->sockets |= sockets;
}

static void
flatpak_context_remove_sockets (FlatpakContext       *context,
                                FlatpakContextSockets sockets)
{
  context->sockets_valid |= sockets;
  context->sockets &= ~sockets;
}

static void
flatpak_context_add_devices (FlatpakContext       *context,
                             FlatpakContextDevices devices)
{
  context->devices_valid |= devices;
  context->devices |= devices;
}

static void
flatpak_context_remove_devices (FlatpakContext       *context,
                                FlatpakContextDevices devices)
{
  context->devices_valid |= devices;
  context->devices &= ~devices;
}

static void
flatpak_context_add_features (FlatpakContext       *context,
                           FlatpakContextFeatures   features)
{
  context->features_valid |= features;
  context->features |= features;
}

static void
flatpak_context_remove_features (FlatpakContext       *context,
                               FlatpakContextFeatures  features)
{
  context->features_valid |= features;
  context->features &= ~features;
}

static void
flatpak_context_set_env_var (FlatpakContext *context,
                             const char     *name,
                             const char     *value)
{
  g_hash_table_insert (context->env_vars, g_strdup (name), g_strdup (value));
}

static void
flatpak_context_set_session_bus_policy (FlatpakContext *context,
                                        const char     *name,
                                        FlatpakPolicy   policy)
{
  g_hash_table_insert (context->session_bus_policy, g_strdup (name), GINT_TO_POINTER (policy));
}

static void
flatpak_context_set_system_bus_policy (FlatpakContext *context,
                                       const char     *name,
                                       FlatpakPolicy   policy)
{
  g_hash_table_insert (context->system_bus_policy, g_strdup (name), GINT_TO_POINTER (policy));
}

static void
flatpak_context_apply_generic_policy (FlatpakContext *context,
                                      const char     *key,
                                      const char     *value)
{
  GPtrArray *new = g_ptr_array_new ();
  const char **old_v;
  int i;

  g_assert (strchr (key, '.') != NULL);

  old_v = g_hash_table_lookup (context->generic_policy, key);
  for (i = 0; old_v != NULL && old_v[i] != NULL; i++)
    {
      const char *old = old_v[i];
      const char *cmp1 = old;
      const char *cmp2 = value;
      if (*cmp1 == '!')
        cmp1++;
      if (*cmp2 == '!')
        cmp2++;
      if (strcmp (cmp1, cmp2) != 0)
        g_ptr_array_add (new, g_strdup (old));
    }

  g_ptr_array_add (new, g_strdup (value));
  g_ptr_array_add (new, NULL);

  g_hash_table_insert (context->generic_policy, g_strdup (key),
                       g_ptr_array_free (new, FALSE));
}

static void
flatpak_context_set_persistent (FlatpakContext *context,
                                const char     *path)
{
  g_hash_table_insert (context->persistent, g_strdup (path), GINT_TO_POINTER (1));
}

static gboolean
get_xdg_dir_from_prefix (const char *prefix,
                         const char **where,
                         const char **dir)
{
  if (strcmp (prefix, "xdg-data") == 0)
    {
      if (where)
        *where = "data";
      if (dir)
        *dir = g_get_user_data_dir ();
      return TRUE;
    }
  if (strcmp (prefix, "xdg-cache") == 0)
    {
      if (where)
        *where = "cache";
      if (dir)
        *dir = g_get_user_cache_dir ();
      return TRUE;
    }
  if (strcmp (prefix, "xdg-config") == 0)
    {
      if (where)
        *where = "config";
      if (dir)
        *dir = g_get_user_config_dir ();
      return TRUE;
    }
  return FALSE;
}

static gboolean
get_xdg_user_dir_from_string (const char  *filesystem,
                              const char **config_key,
                              const char **suffix,
                              const char **dir)
{
  char *slash;
  const char *rest;
  g_autofree char *prefix = NULL;
  gsize len;

  slash = strchr (filesystem, '/');

  if (slash)
    len = slash - filesystem;
  else
    len = strlen (filesystem);

  rest = filesystem + len;
  while (*rest == '/')
    rest++;

  if (suffix)
    *suffix = rest;

  prefix = g_strndup (filesystem, len);

  if (strcmp (prefix, "xdg-desktop") == 0)
    {
      if (config_key)
        *config_key = "XDG_DESKTOP_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
      return TRUE;
    }
  if (strcmp (prefix, "xdg-documents") == 0)
    {
      if (config_key)
        *config_key = "XDG_DOCUMENTS_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);
      return TRUE;
    }
  if (strcmp (prefix, "xdg-download") == 0)
    {
      if (config_key)
        *config_key = "XDG_DOWNLOAD_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD);
      return TRUE;
    }
  if (strcmp (prefix, "xdg-music") == 0)
    {
      if (config_key)
        *config_key = "XDG_MUSIC_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_MUSIC);
      return TRUE;
    }
  if (strcmp (prefix, "xdg-pictures") == 0)
    {
      if (config_key)
        *config_key = "XDG_PICTURES_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_PICTURES);
      return TRUE;
    }
  if (strcmp (prefix, "xdg-public-share") == 0)
    {
      if (config_key)
        *config_key = "XDG_PUBLICSHARE_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_PUBLIC_SHARE);
      return TRUE;
    }
  if (strcmp (prefix, "xdg-templates") == 0)
    {
      if (config_key)
        *config_key = "XDG_TEMPLATES_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_TEMPLATES);
      return TRUE;
    }
  if (strcmp (prefix, "xdg-videos") == 0)
    {
      if (config_key)
        *config_key = "XDG_VIDEOS_DIR";
      if (dir)
        *dir = g_get_user_special_dir (G_USER_DIRECTORY_VIDEOS);
      return TRUE;
    }
  if (get_xdg_dir_from_prefix (prefix, NULL, dir))
    {
      if (config_key)
        *config_key = NULL;
      return TRUE;
    }
  /* Don't support xdg-run without suffix, because that doesn't work */
  if (strcmp (prefix, "xdg-run") == 0 &&
      *rest != 0)
    {
      if (config_key)
        *config_key = NULL;
      if (dir)
        *dir = g_get_user_runtime_dir ();
      return TRUE;
    }

  return FALSE;
}

static char *
parse_filesystem_flags (const char *filesystem, FlatpakFilesystemMode *mode)
{
  gsize len = strlen (filesystem);

  if (mode)
    *mode = FLATPAK_FILESYSTEM_MODE_READ_WRITE;

  if (g_str_has_suffix (filesystem, ":ro"))
    {
      len -= 3;
      if (mode)
        *mode = FLATPAK_FILESYSTEM_MODE_READ_ONLY;
    }
  else if (g_str_has_suffix (filesystem, ":rw"))
    {
      len -= 3;
      if (mode)
        *mode = FLATPAK_FILESYSTEM_MODE_READ_WRITE;
    }
  else if (g_str_has_suffix (filesystem, ":create"))
    {
      len -= 7;
      if (mode)
        *mode = FLATPAK_FILESYSTEM_MODE_CREATE;
    }

  return g_strndup (filesystem, len);
}

static gboolean
flatpak_context_verify_filesystem (const char *filesystem_and_mode,
                                   GError    **error)
{
  g_autofree char *filesystem = parse_filesystem_flags (filesystem_and_mode, NULL);

  if (strcmp (filesystem, "host") == 0)
    return TRUE;
  if (strcmp (filesystem, "home") == 0)
    return TRUE;
  if (get_xdg_user_dir_from_string (filesystem, NULL, NULL, NULL))
    return TRUE;
  if (g_str_has_prefix (filesystem, "~/"))
    return TRUE;
  if (g_str_has_prefix (filesystem, "/"))
    return TRUE;

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               _("Unknown filesystem location %s, valid locations are: host, home, xdg-*[/...], ~/dir, /dir"), filesystem);
  return FALSE;
}

static void
flatpak_context_add_filesystem (FlatpakContext *context,
                                const char     *what)
{
  FlatpakFilesystemMode mode;
  char *fs = parse_filesystem_flags (what, &mode);

  g_hash_table_insert (context->filesystems, fs, GINT_TO_POINTER (mode));
}

static void
flatpak_context_remove_filesystem (FlatpakContext *context,
                                   const char     *what)
{
  g_hash_table_insert (context->filesystems,
                       parse_filesystem_flags (what, NULL),
                       NULL);
}

static gboolean
option_share_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer     data,
                 GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextShares share;

  share = flatpak_context_share_from_string (value, error);
  if (share == 0)
    return FALSE;

  flatpak_context_add_shares (context, share);

  return TRUE;
}

static gboolean
option_unshare_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer     data,
                   GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextShares share;

  share = flatpak_context_share_from_string (value, error);
  if (share == 0)
    return FALSE;

  flatpak_context_remove_shares (context, share);

  return TRUE;
}

static gboolean
option_socket_cb (const gchar *option_name,
                  const gchar *value,
                  gpointer     data,
                  GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextSockets socket;

  socket = flatpak_context_socket_from_string (value, error);
  if (socket == 0)
    return FALSE;

  if (socket == FLATPAK_CONTEXT_SOCKET_FALLBACK_X11)
    socket |= FLATPAK_CONTEXT_SOCKET_X11;

  flatpak_context_add_sockets (context, socket);

  return TRUE;
}

static gboolean
option_nosocket_cb (const gchar *option_name,
                    const gchar *value,
                    gpointer     data,
                    GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextSockets socket;

  socket = flatpak_context_socket_from_string (value, error);
  if (socket == 0)
    return FALSE;

  if (socket == FLATPAK_CONTEXT_SOCKET_FALLBACK_X11)
    socket |= FLATPAK_CONTEXT_SOCKET_X11;

  flatpak_context_remove_sockets (context, socket);

  return TRUE;
}

static gboolean
option_device_cb (const gchar *option_name,
                  const gchar *value,
                  gpointer     data,
                  GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextDevices device;

  device = flatpak_context_device_from_string (value, error);
  if (device == 0)
    return FALSE;

  flatpak_context_add_devices (context, device);

  return TRUE;
}

static gboolean
option_nodevice_cb (const gchar *option_name,
                    const gchar *value,
                    gpointer     data,
                    GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextDevices device;

  device = flatpak_context_device_from_string (value, error);
  if (device == 0)
    return FALSE;

  flatpak_context_remove_devices (context, device);

  return TRUE;
}

static gboolean
option_allow_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer     data,
                 GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextFeatures feature;

  feature = flatpak_context_feature_from_string (value, error);
  if (feature == 0)
    return FALSE;

  flatpak_context_add_features (context, feature);

  return TRUE;
}

static gboolean
option_disallow_cb (const gchar *option_name,
                    const gchar *value,
                    gpointer     data,
                    GError     **error)
{
  FlatpakContext *context = data;
  FlatpakContextFeatures feature;

  feature = flatpak_context_feature_from_string (value, error);
  if (feature == 0)
    return FALSE;

  flatpak_context_remove_features (context, feature);

  return TRUE;
}

static gboolean
option_filesystem_cb (const gchar *option_name,
                      const gchar *value,
                      gpointer     data,
                      GError     **error)
{
  FlatpakContext *context = data;

  if (!flatpak_context_verify_filesystem (value, error))
    return FALSE;

  flatpak_context_add_filesystem (context, value);
  return TRUE;
}

static gboolean
option_nofilesystem_cb (const gchar *option_name,
                        const gchar *value,
                        gpointer     data,
                        GError     **error)
{
  FlatpakContext *context = data;

  if (!flatpak_context_verify_filesystem (value, error))
    return FALSE;

  flatpak_context_remove_filesystem (context, value);
  return TRUE;
}

static gboolean
option_env_cb (const gchar *option_name,
               const gchar *value,
               gpointer     data,
               GError     **error)
{
  FlatpakContext *context = data;

  g_auto(GStrv) split = g_strsplit (value, "=", 2);

  if (split == NULL || split[0] == NULL || split[0][0] == 0 || split[1] == NULL)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   _("Invalid env format %s"), value);
      return FALSE;
    }

  flatpak_context_set_env_var (context, split[0], split[1]);
  return TRUE;
}

static gboolean
option_own_name_cb (const gchar *option_name,
                    const gchar *value,
                    gpointer     data,
                    GError     **error)
{
  FlatpakContext *context = data;

  if (!flatpak_verify_dbus_name (value, error))
    return FALSE;

  flatpak_context_set_session_bus_policy (context, value, FLATPAK_POLICY_OWN);
  return TRUE;
}

static gboolean
option_talk_name_cb (const gchar *option_name,
                     const gchar *value,
                     gpointer     data,
                     GError     **error)
{
  FlatpakContext *context = data;

  if (!flatpak_verify_dbus_name (value, error))
    return FALSE;

  flatpak_context_set_session_bus_policy (context, value, FLATPAK_POLICY_TALK);
  return TRUE;
}

static gboolean
option_system_own_name_cb (const gchar *option_name,
                           const gchar *value,
                           gpointer     data,
                           GError     **error)
{
  FlatpakContext *context = data;

  if (!flatpak_verify_dbus_name (value, error))
    return FALSE;

  flatpak_context_set_system_bus_policy (context, value, FLATPAK_POLICY_OWN);
  return TRUE;
}

static gboolean
option_system_talk_name_cb (const gchar *option_name,
                            const gchar *value,
                            gpointer     data,
                            GError     **error)
{
  FlatpakContext *context = data;

  if (!flatpak_verify_dbus_name (value, error))
    return FALSE;

  flatpak_context_set_system_bus_policy (context, value, FLATPAK_POLICY_TALK);
  return TRUE;
}

static gboolean
option_add_generic_policy_cb (const gchar *option_name,
                              const gchar *value,
                              gpointer     data,
                              GError     **error)
{
  FlatpakContext *context = data;
  char *t;
  g_autofree char *key = NULL;
  const char *policy_value;

  t = strchr (value, '=');
  if (t == NULL)
    return flatpak_fail (error, "--policy arguments must be in the form SUBSYSTEM.KEY=[!]VALUE");
  policy_value = t + 1;
  key = g_strndup (value, t - value);
  if (strchr (key, '.') == NULL)
    return flatpak_fail (error, "--policy arguments must be in the form SUBSYSTEM.KEY=[!]VALUE");

  if (policy_value[0] == '!')
    return flatpak_fail (error, "--policy values can't start with \"!\"");

  flatpak_context_apply_generic_policy (context, key, policy_value);

  return TRUE;
}

static gboolean
option_remove_generic_policy_cb (const gchar *option_name,
                                 const gchar *value,
                                 gpointer     data,
                                 GError     **error)
{
  FlatpakContext *context = data;
  char *t;
  g_autofree char *key = NULL;
  const char *policy_value;
  g_autofree char *extended_value = NULL;

  t = strchr (value, '=');
  if (t == NULL)
    return flatpak_fail (error, "--policy arguments must be in the form SUBSYSTEM.KEY=[!]VALUE");
  policy_value = t + 1;
  key = g_strndup (value, t - value);
  if (strchr (key, '.') == NULL)
    return flatpak_fail (error, "--policy arguments must be in the form SUBSYSTEM.KEY=[!]VALUE");

  if (policy_value[0] == '!')
    return flatpak_fail (error, "--policy values can't start with \"!\"");

  extended_value = g_strconcat ("!", policy_value, NULL);

  flatpak_context_apply_generic_policy (context, key, extended_value);

  return TRUE;
}

static gboolean
option_persist_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer     data,
                   GError     **error)
{
  FlatpakContext *context = data;

  flatpak_context_set_persistent (context, value);
  return TRUE;
}

static GOptionEntry context_options[] = {
  { "share", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_share_cb, N_("Share with host"), N_("SHARE") },
  { "unshare", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_unshare_cb, N_("Unshare with host"), N_("SHARE") },
  { "socket", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_socket_cb, N_("Expose socket to app"), N_("SOCKET") },
  { "nosocket", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_nosocket_cb, N_("Don't expose socket to app"), N_("SOCKET") },
  { "device", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_device_cb, N_("Expose device to app"), N_("DEVICE") },
  { "nodevice", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_nodevice_cb, N_("Don't expose device to app"), N_("DEVICE") },
  { "allow", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_allow_cb, N_("Allow feature"), N_("FEATURE") },
  { "disallow", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_disallow_cb, N_("Don't allow feature"), N_("FEATURE") },
  { "filesystem", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_filesystem_cb, N_("Expose filesystem to app (:ro for read-only)"), N_("FILESYSTEM[:ro]") },
  { "nofilesystem", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_nofilesystem_cb, N_("Don't expose filesystem to app"), N_("FILESYSTEM") },
  { "env", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_env_cb, N_("Set environment variable"), N_("VAR=VALUE") },
  { "own-name", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_own_name_cb, N_("Allow app to own name on the session bus"), N_("DBUS_NAME") },
  { "talk-name", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_talk_name_cb, N_("Allow app to talk to name on the session bus"), N_("DBUS_NAME") },
  { "system-own-name", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_system_own_name_cb, N_("Allow app to own name on the system bus"), N_("DBUS_NAME") },
  { "system-talk-name", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_system_talk_name_cb, N_("Allow app to talk to name on the system bus"), N_("DBUS_NAME") },
  { "add-policy", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_add_generic_policy_cb, N_("Add generic policy option"), N_("SUBSYSTEM.KEY=VALUE") },
  { "remove-policy", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_remove_generic_policy_cb, N_("Remove generic policy option"), N_("SUBSYSTEM.KEY=VALUE") },
  { "persist", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_CALLBACK, &option_persist_cb, N_("Persist home directory"), N_("FILENAME") },
  { NULL }
};

GOptionGroup  *
flatpak_context_get_options (FlatpakContext *context)
{
  GOptionGroup *group;

  group = g_option_group_new ("environment",
                              "Runtime Environment",
                              "Runtime Environment",
                              context,
                              NULL);
  g_option_group_set_translation_domain (group, GETTEXT_PACKAGE);

  g_option_group_add_entries (group, context_options);

  return group;
}

void
flatpak_context_to_args (FlatpakContext *context,
                         GPtrArray *args)
{
  GHashTableIter iter;
  gpointer key, value;

  flatpak_context_shared_to_args (context->shares, context->shares_valid, args);
  flatpak_context_sockets_to_args (context->sockets, context->sockets_valid, args);
  flatpak_context_devices_to_args (context->devices, context->devices_valid, args);
  flatpak_context_features_to_args (context->features, context->features_valid, args);

  g_hash_table_iter_init (&iter, context->env_vars);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_ptr_array_add (args, g_strdup_printf ("--env=%s=%s", (char *)key, (char *)value));

  g_hash_table_iter_init (&iter, context->persistent);
  while (g_hash_table_iter_next (&iter, &key, &value))
    g_ptr_array_add (args, g_strdup_printf ("--persist=%s", (char *)key));

  g_hash_table_iter_init (&iter, context->session_bus_policy);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *name = key;
      FlatpakPolicy policy = GPOINTER_TO_INT (value);

      g_ptr_array_add (args, g_strdup_printf ("--%s-name=%s", flatpak_policy_to_string (policy), name));
    }

  g_hash_table_iter_init (&iter, context->system_bus_policy);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *name = key;
      FlatpakPolicy policy = GPOINTER_TO_INT (value);

      g_ptr_array_add (args, g_strdup_printf ("--system-%s-name=%s", flatpak_policy_to_string (policy), name));
    }

  g_hash_table_iter_init (&iter, context->filesystems);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      FlatpakFilesystemMode mode = GPOINTER_TO_INT (value);

      if (mode == FLATPAK_FILESYSTEM_MODE_READ_ONLY)
        g_ptr_array_add (args, g_strdup_printf ("--filesystem=%s:ro", (char *)key));
      else if (mode == FLATPAK_FILESYSTEM_MODE_READ_WRITE)
        g_ptr_array_add (args, g_strdup_printf ("--filesystem=%s", (char *)key));
      else if (mode == FLATPAK_FILESYSTEM_MODE_CREATE)
        g_ptr_array_add (args, g_strdup_printf ("--filesystem=%s:create", (char *)key));
      else
        g_ptr_array_add (args, g_strdup_printf ("--nofilesystem=%s", (char *)key));
    }
}
