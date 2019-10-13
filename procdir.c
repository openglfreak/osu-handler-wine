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

#define _POSIX_C_SOURCE 200809L /* dirfd */
#define _DEFAULT_SOURCE /* DT_DIR, DT_UNKNOWN, dirfd */

#include "is_number.h" /* is_number */

#include <dirent.h> /* DIR, DT_DIR, DT_UNKNOWN, _DIRENT_HAVE_D_TYPE, closedir,
                       struct dirent, dirfd, opendir, readdir */
#include <errno.h> /* ENOMEM, errno */
#include <stdlib.h> /* free, malloc */

typedef struct procdir_struct {
    DIR* dirptr;
    int dirfd;
} procdir_struct;

int open_procdir(procdir_struct** out_pdhandle)
{
    procdir_struct* p;

    p = (procdir_struct*)malloc(sizeof(procdir_struct));
    if (!p)
        return ENOMEM;

    p->dirptr = opendir("/proc");
    p->dirfd = -1;

    if (!p->dirptr)
    {
        free(p);
        return errno;
    }

    *out_pdhandle = p;
    return 0;
}

int procdir_dirfd(procdir_struct* p)
{
    if (p->dirfd == -1)
        p->dirfd = dirfd(p->dirptr);

    return p->dirfd;
}

int procdir_next_process(procdir_struct* p, struct dirent** out_dent)
{
    errno = 0;

    do {
        *out_dent = readdir(p->dirptr);
#ifdef _DIRENT_HAVE_D_TYPE
    } while (errno == 0 && *out_dent && ((*out_dent)->d_type == DT_DIR ||
        (*out_dent)->d_type == DT_UNKNOWN) && !is_number((*out_dent)->d_name));
#else
    } while (errno == 0 && *out_dent && !is_number((*out_dent)->d_name));
#endif

    return errno;
}

void close_procdir(procdir_struct* p)
{
    closedir(p->dirptr);
    free(p);
}
