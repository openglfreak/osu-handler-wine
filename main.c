/* Copyright (C) 2019 Torge Matthies */
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
#include "static_string.h" /* static_strlen, static_endswith, static_startswith */

#include <assert.h> /* assert */
#include <dirent.h> /* struct dent */
#include <errno.h> /* ENOTSUP, errno */
#include <fcntl.h> /* O_DIRECTORY, O_SEARCH, O_RDONLY, openat */
#include <stddef.h> /* size_t, ssize_t */
#include <stdio.h> /* perror */
#include <stdlib.h> /* free, malloc, realloc */
#include <string.h> /* memcmp, memchr */
#include <sys/stat.h> /* fstat, struct stat */
#include <sys/types.h> /* uid_t */
#include <unistd.h> /* close, execve, getuid, read, readlinkat */

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

static inline bool read_environ(int const dirfd, char** const out_environ,
    size_t* const out_environ_size)
{
#define malloc_overhead 32
    int fd;
    size_t bufsize;
    char* buffer;
    size_t pos;
    bool ret;

    fd = openat(dirfd, "environ", O_RDONLY);
    if (fd == -1)
        return false;

    bufsize = 8192 - malloc_overhead;
    buffer = (char*)malloc(sizeof(char) * bufsize);
    if (!buffer)
    {
        close(fd);
        return false;
    }

    pos = 0;
    ret = false;
    while (true)
    {
        ssize_t n;
        char* buffer2;

        n = read(fd, &buffer[pos], bufsize - pos);
        if (n < 0)
        {
            free(buffer);
            break;
        }

        if (n == 0)
        {
            buffer2 = (char*)realloc(buffer, sizeof(char) * pos);
            *out_environ = buffer2 ? buffer2 : buffer;
            *out_environ_size = pos;
            ret = true;
            break;
        }

        pos += (size_t)n;
        if (pos >= bufsize)
        {
            bufsize = (bufsize + malloc_overhead) * 2 - malloc_overhead;
            buffer2 = (char*)realloc(buffer, sizeof(char) * bufsize);
            if (!buffer2)
            {
                free(buffer);
                break;
            }
            buffer = buffer2;
        }
    }

    close(fd);
    return ret;
#undef malloc_overhead
}

static inline attr_const size_t count_envvars(char const* environ,
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

static inline bool construct_envp(char* environ,
    size_t const environ_size, char*** const out_envp)
{
    char* const environ_end = &environ[environ_size];
    size_t n;
    char** envp;
    char* p;
    char** p2;

    n = count_envvars(environ, environ_end);

    envp = (char**)malloc(sizeof(char*) * n);
    if (!envp)
        return false;

    p2 = envp;
    for (p = environ; (p = (char*)memchr(p, '\0', environ_end - p)); environ = p)
    {
        ++p;

        if (static_startswith((size_t)(p - environ), environ,
                "WINELOADERNOEXEC="))
            continue;

        *p2++ = environ;
        assert(n-- > 0);
    }
    *p2++ = 0;

    p2 = (char**)realloc(envp, sizeof(char*) * (p2 - envp));
    *out_envp = p2 ? p2 : envp;
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

    b = test_dir(dirfd, &exe_path);

    b = b && read_environ(dirfd, &environ, &environ_size);

    close(dirfd);

    b = b && construct_envp(environ, environ_size, &envp);

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

static int handle_error(int error)
{
    errno = error;
    perror("error");

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
        if ((error = procdir_next_process(pdhandle, &dent)) != 0 || dent)
            break;
        if ((error = handle_dir(dirfd, dent, argv, &exit_loop)) != 0)
            break;
    } while (!exit_loop);

    close_procdir(pdhandle);

    return error != 0 ? handle_error(error) : 0;
}
