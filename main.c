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

#include "bool.h" /* bool */
#include "inline.h" /* inline */
#include "procdir.h" /* close_procdir, open_procdir, procdir_dirfd,
                        procdir_handle, procdir_next_process */

#include <assert.h> /* assert */
#include <dirent.h> /* struct dent */
#include <errno.h> /* ENOTSUP, errno */
#include <fcntl.h> /* O_DIRECTORY, O_SEARCH, O_RDONLY, openat */
#include <libgen.h> /* basename */
#include <stddef.h> /* size_t, ssize_t */
#include <stdio.h> /* perror */
#include <stdlib.h> /* free, malloc, realloc */
#include <string.h> /* memcmp */
#include <sys/stat.h> /* fstat, struct stat */
#include <sys/types.h> /* uid_t */
#include <unistd.h> /* close, execve, getuid, read, readlinkat */

uid_t our_uid;

static inline bool test_uid(int const dirfd)
{
    struct stat buf;

    return fstat(dirfd, &buf) != -1 && buf.st_uid == our_uid;
}

#define static_strlen(s) (sizeof(s) - 1)

static inline bool test_comm(int const dirfd)
{
#define wanted_comm "osu!.exe\n"
    int fd;
    char buf[static_strlen(wanted_comm)];
    bool b;

    fd = openat(dirfd, "comm", O_RDONLY);

    if (fd == -1)
        return false;

    b = read(fd, buf, sizeof(buf)) == sizeof(buf);

    close(fd);

    return b && memcmp(buf, wanted_comm, static_strlen(wanted_comm)) == 0;
#undef wanted_comm
}

#define static_endswith(n, m, e) \
    ((n) >= static_strlen(e) && \
    memcmp((m) + ((n) - static_strlen(e)), (e), static_strlen(e)) == 0)

static inline bool test_exe(int const dirfd, char** const out_exe_path)
{
#define wanted_exe_name_1 "wine-preloader"
#define wanted_exe_name_2 "wine64-preloader"
    size_t bufsize;
    char* buffer,* buffer2;
    ssize_t link_len;

    bufsize = 128;

    do {
        buffer = (char*)malloc(sizeof(char) * bufsize);
        if (!buffer)
            return false;

        link_len = readlinkat(dirfd, "exe", buffer, bufsize);

        if (link_len < 0)
        {
            free(buffer);
            return false;
        }

        if ((size_t)link_len >= bufsize)
        {
            free(buffer);
            bufsize *= 2;
            continue;
        }

        if (static_endswith((size_t)link_len, buffer, "/" wanted_exe_name_1) ||
            static_endswith((size_t)link_len, buffer, "/" wanted_exe_name_2))
        {
            buffer[link_len] = '\0';
            break;
        }

        free(buffer);
        return false;
    } while (true);

    buffer2 = (char*)realloc(buffer, sizeof(char) * (link_len + 1));
    if (buffer2)
        *out_exe_path = buffer2;
    else
        *out_exe_path = buffer;

    return true;
#undef wanted_exe_name_1
#undef wanted_exe_name_2
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
    int fd;
    size_t bufsize;
    char* buffer,* buffer2;
    size_t pos;

    bufsize = 4096;
    pos = 0;

    fd = openat(dirfd, "environ", O_RDONLY);
    if (fd == -1)
        return false;

    buffer = (char*)malloc(sizeof(char) * bufsize);
    if (!buffer)
        return false;

    do {
        ssize_t n;
        n = read(fd, &buffer[pos], bufsize - pos);
        pos += (size_t)n;

        if (n < 0)
        {
            free(buffer);
            return false;
        }

        if (pos < bufsize)
        {
            n = read(fd, &buffer[pos], bufsize - pos);
            pos += (size_t)n;

            if (n < 0)
            {
                free(buffer);
                return false;
            }

            if (n == 0)
                break;
        }

        if (pos >= bufsize)
        {
            bufsize *= 2;
            buffer2 = (char*)realloc(buffer, sizeof(char) * bufsize);
            if (!buffer2)
            {
                free(buffer);
                return false;
            }
            buffer = buffer2;
        }
    } while (true);

    buffer2 = (char*)realloc(buffer, sizeof(char) * pos);
    if (buffer2)
        *out_environ = buffer2;
    else
        *out_environ = buffer;
    *out_environ_size = pos;

    return true;
}

#define static_startswith(n, m, e) \
    ((n) >= static_strlen(e) && memcmp((m), (e), static_strlen(e)) == 0)

/* Don't look at this function too closely. */
static inline bool construct_envp(char* environ,
    size_t const environ_size, char*** const out_envp)
{
    char* const environ_end = &environ[environ_size];
    char** envp;
    char* p;
    char** p2;
    size_t n;

    p = environ;
    n = 0;
    for (; p != environ_end; ++p)
        if (*p == '\0')
            ++n;

    envp = (char**)malloc(sizeof(char*) * n);
    if (!envp)
        return false;

    p = environ;
    p2 = envp;
    while (p != environ_end)
    {
        while (p != environ_end && *p++ != '\0');

        if (!static_startswith((size_t)(p - environ), environ,
                "WINELOADERNOEXEC="))
        {
            *p2++ = environ;
            assert(n-- > 0);
        }

        environ = p;
    }

    *out_envp = envp;
    return true;
}

static inline void handle_dir(int const proc_dirfd,
    struct dirent const* const dent, char* argv[])
{
    int dirfd;
    char* exe_path;
    char* environ;
    bool b;
    size_t environ_size;
    char** envp;

#ifdef O_SEARCH
    dirfd = openat(proc_dirfd, dent->d_name, O_SEARCH | O_DIRECTORY);
#else
    dirfd = openat(proc_dirfd, dent->d_name, O_RDONLY | O_DIRECTORY);
#endif
    if (dirfd == -1)
        return;

    b = test_dir(dirfd, &exe_path);

    b = b && read_environ(dirfd, &environ, &environ_size);

    close(dirfd);

    b = b && construct_envp(environ, environ_size, &envp);

    if (!b)
        return;

    exe_path[strlen(exe_path) - static_strlen("-preloader")] = '\0';
    argv[0] = basename(exe_path);
    execve(exe_path, argv, envp);
}

static int handle_error(int error)
{
    errno = error;
    perror("error");

    error &= 0xFF;
    error |= !!error;
    return error;
}

int main(int argc, char* argv[])
{
    int error;
    procdir_handle pdhandle;
    struct dirent* dent;
    int dirfd;

    (void)argc;

    our_uid = getuid();

    if ((error = open_procdir(&pdhandle)) != 0)
        return handle_error(error);

    if ((dirfd = procdir_dirfd(pdhandle)) == -1)
        return handle_error(ENOTSUP);

    while ((error = procdir_next_process(pdhandle, &dent)) == 0 && dent)
        handle_dir(dirfd, dent, argv);

    close_procdir(pdhandle);

    if (error != 0)
        return handle_error(error);

    return 0;
}

