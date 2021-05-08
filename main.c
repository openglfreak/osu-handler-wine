/* Copyright (C) 2019-2021 Torge Matthies */
/*
 * This file is part of osu-handler-wine.
 *
 * osu-handler-wine is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * osu-handler-wine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
/*
 * Author contact info:
 *   E-Mail address: openglfreak@googlemail.com
 *   GPG key fingerprint: 0535 3830 2F11 C888 9032 FAD2 7C95 CD70 C9E8 438D
 */

#define _POSIX_C_SOURCE 200809L /* openat, readlinkat */
#define _DEFAULT_SOURCE /* openat, readlinkat */

#include "attrs.h" /* attr_const */
#include "bool.h" /* bool */
#include "inline.h" /* inline */
#include "procdir.h" /* close_procdir, open_procdir, procdir_dirfd,
                        procdir_handle, procdir_next_process */
#include "notifications.h" /* show_notification */
#include "static_string.h" /* static_strlen, static_endswith, static_startswith */

#include <assert.h> /* assert */
#include <ctype.h> /* toupper */
#include <dirent.h> /* struct dent */
#include <errno.h> /* ENOTSUP, errno */
#include <fcntl.h> /* O_DIRECTORY, O_SEARCH, O_RDONLY, openat */
#include <stddef.h> /* size_t, ssize_t */
#include <stdlib.h> /* free, malloc, realloc */
#include <string.h> /* memcmp, memchr, strerror, strdup */
#include <sys/stat.h> /* fstat, struct stat */
#include <sys/types.h> /* uid_t */
#include <unistd.h> /* close, execve, execvp, getuid, read, readlinkat */

uid_t our_uid;

static inline bool test_uid(int const dirfd)
{
    struct stat buf;

    return fstat(dirfd, &buf) != -1 && buf.st_uid == our_uid;
}

static inline bool test_comm(int const dirfd)
{
    static char wanted_comm[] = "osu!.exe\n";
    int fd;
    char buf[static_strlen(wanted_comm)];
    bool b;

    fd = openat(dirfd, "comm", O_RDONLY);
    if (fd == -1)
        return false;

    b = read(fd, buf, sizeof(buf)) == sizeof(buf);

    close(fd);

    return b && memcmp(buf, wanted_comm, static_strlen(wanted_comm)) == 0;
}

static inline char* get_exe_path(int const dirfd, size_t* const path_len)
{
    size_t bufsize;
    char* buffer;

    for (bufsize = 128;;bufsize *= 2)
    {
        ssize_t link_len;

        buffer = (char*)malloc(sizeof(char) * bufsize);
        if (!buffer)
            break;

        link_len = readlinkat(dirfd, "exe", buffer, bufsize);
        if (link_len == -1)
        {
            free(buffer);
            buffer = 0;
            break;
        }

        if ((size_t)link_len < bufsize)
        {
            buffer[(size_t)link_len] = '\0';
            *path_len = (size_t)link_len;
            break;
        }

        free(buffer);
    }

    return buffer;
}

static inline bool test_exe(int const dirfd, char** const out_exe_path)
{
    char* exe_path;
    size_t path_len;
    char* exe_path2;

    exe_path = get_exe_path(dirfd, &path_len);
    if (!exe_path)
        return false;

    if (!static_endswith((size_t)path_len, exe_path, "/wine-preloader") &&
        !static_endswith((size_t)path_len, exe_path, "/wine64-preloader"))
    {
        free(exe_path);
        return false;
    }

    exe_path2 = (char*)realloc(exe_path, sizeof(char) * (path_len + 1));
    *out_exe_path = exe_path2 ? exe_path2 : exe_path;
    return true;
}

static inline bool test_dir(int const dirfd, char** const out_exe_path)
{
    if (!test_uid(dirfd))
        return false;

    if (!test_comm(dirfd))
        return false;

    if (!test_exe(dirfd, out_exe_path))
        return false;

    return true;
}

static inline bool try_realloc(void** const ptr, size_t const new_size)
{
    void* const new_ptr = realloc(*ptr, new_size);
    if (new_ptr) *ptr = new_ptr;
    return !!new_ptr;
}

#define malloc_overhead 32

static inline bool read_environ_content(int const fd, char** const out_environ,
    size_t* const out_environ_size)
{
    size_t bufsize;
    char* buffer;
    size_t pos;
    ssize_t n;

    bufsize = 8192 - malloc_overhead;
    buffer = (char*)malloc(sizeof(char) * bufsize);
    if (!buffer)
        return false;

    pos = 0;
    while ((n = read(fd, &buffer[pos], bufsize - pos)) > 0)
    {
        pos += (size_t)n;
        if (pos < bufsize)
            continue;

        bufsize = (bufsize + malloc_overhead) * 2 - malloc_overhead;
        if (!try_realloc((void**)&buffer, sizeof(char) * bufsize))
        {
            free(buffer);
            return false;
        }
    }
    if (n < 0)
    {
        free(buffer);
        return false;
    }

    try_realloc((void**)&buffer, sizeof(char) * pos);
    *out_environ = buffer;
    *out_environ_size = pos;
    return true;
}

static inline bool read_environ(int const dirfd, char** const out_environ,
    size_t* const out_environ_size)
{
    int fd;
    bool ret;

    fd = openat(dirfd, "environ", O_RDONLY);
    if (fd == -1)
        return false;

    ret = read_environ_content(fd, out_environ, out_environ_size);

    close(fd);
    return ret;
}

static inline size_t count_envvars(char const* environ,
    char const* const environ_end)
{
    size_t count = 0;
    while ((environ = (char const*)memchr(environ, '\0', environ_end - environ)))
    {
        ++environ;
        ++count;
    }
    return count;
}

static struct filtered_envvar {
    char const* envar;
    size_t length;
} const filtered_envvars[] = {
#define FENVAR(x) { (x), static_strlen((x)) }
    FENVAR("WINELOADERNOEXEC="),
    FENVAR("WINEPRELOADRESERVE="),
    FENVAR("WINESERVERSOCKET=")
#undef FENVAR
};

static inline bool test_envar(char const* const envar_start,
    char const* const envar_end)
{
    size_t i = 0;
    for (; i < sizeof(filtered_envvars) / sizeof(filtered_envvars[0]); ++i)
    {
        char const* const envar = filtered_envvars[i].envar;
        size_t const length = filtered_envvars[i].length;

        if (envar_end - envar_start - 1 < length)
            continue;
        if (memcmp(envar_start, envar, length) == 0)
            return false;
    }
    return true;
}

static inline void fill_envp_from_environ(char** const envp,
    char*** const out_envp_end, char* environ, char* const environ_end)
{
    char** envp_end = envp;
    char* envar_start = environ;
    char* envar_end = environ;

    while ((envar_end = (char*)memchr(envar_end, '\0', environ_end - envar_end)))
    {
        ++envar_end;
        envar_start = environ;
        environ = envar_end;

        if (test_envar(envar_start, envar_end))
            *envp_end++ = envar_start;
    }

    *envp_end++ = 0;
    *out_envp_end = envp_end;
}

static inline bool construct_envp_from_environ(char* const environ,
    size_t const environ_size, char*** const out_envp)
{
    char* const environ_end = &environ[environ_size];
    size_t envvar_count;
    char** envp;
    char** envp_end;

    envvar_count = count_envvars(environ, environ_end);
    envp = (char**)malloc(sizeof(char*) * envvar_count);
    if (!envp)
        return false;

    fill_envp_from_environ(envp, &envp_end, environ, environ_end);

    try_realloc((void**)&envp, sizeof(envp[0]) * (envp_end - envp));
    *out_envp = envp;
    return true;
}

/* Because POSIX says basename(3) may write to the input string... */
static inline attr_const char const* basename_n(char const* const path,
    size_t const length)
{
    char const* ptr = path + length;
    while (ptr > path && *--ptr != '/');
    return ptr;
}

static inline int handle_dir(int const proc_dirfd,
    struct dirent const* const dent, char* argv[], bool* const out_error)
{
    int dirfd;
    char* exe_path;
    char* environ;
    bool b;
    size_t environ_size;
    char** envp;
    size_t exe_path_length;

#ifdef O_SEARCH
    dirfd = openat(proc_dirfd, dent->d_name, O_SEARCH | O_DIRECTORY);
#else
    dirfd = openat(proc_dirfd, dent->d_name, O_RDONLY | O_DIRECTORY);
#endif
    if (dirfd == -1)
        return (errno == ESRCH || errno == ENOENT) ? 0 : errno;

    if (!test_dir(dirfd, &exe_path))
    {
        close(dirfd);
        return 0;
    }

    b = read_environ(dirfd, &environ, &environ_size);
    close(dirfd);
    b = b && construct_envp_from_environ(environ, environ_size, &envp);
    if (!b)
    {
        *out_error = true;
        return 0;
    }

    exe_path_length = strlen(exe_path);
    exe_path[exe_path_length -= static_strlen("-preloader")] = '\0';
    argv[0] = (char*)basename_n(exe_path, exe_path_length);
    execve(exe_path, argv, envp);
    return errno;
}

static inline int run_launcher(char* argv[])
{
    argv[0] = (char*)"osu";
    execvp("osu", argv);
    return errno;
}

static int handle_error(int error)
{
    char const* error_message;
    char* duplicated_message = 0;

    errno = 0;
    error_message = strerror(error);
    if (errno != 0 || !error_message || !error_message[0])
        error_message = "Unknown error";
    else if ((duplicated_message = strdup(error_message)))
    {
        duplicated_message[0] = toupper(duplicated_message[0]);
        error_message = duplicated_message;
    }
    show_notification(error_message);
    free(duplicated_message);

    error &= 0xFF;
    return error ? error : 1;
}

int main(int argc, char* argv[])
{
    int error;
    procdir_handle pdhandle;
    int dirfd;
    struct dirent* dent;
    bool exit_loop;

    (void)argc;

    our_uid = getuid();

    if ((error = open_procdir(&pdhandle)) != 0)
        return handle_error(error);

    if ((dirfd = procdir_dirfd(pdhandle)) == -1)
        return handle_error(ENOTSUP);

    exit_loop = false;
    do {
        if ((error = procdir_next_process(pdhandle, &dent)) != 0 || !dent)
            break;
        if ((error = handle_dir(dirfd, dent, argv, &exit_loop)) != 0)
            break;
    } while (!exit_loop);

    close_procdir(pdhandle);

    if (error != 0)
        return handle_error(error);

    if (!exit_loop)
        error = run_launcher(argv);

    if (error != 0 && error != ENOENT)
        return handle_error(error);

    if (!exit_loop)
        show_notification("Could not find a running osu! instance");
    return 0;
}
