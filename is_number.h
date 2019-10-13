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

#pragma once
#ifndef __IS_NUMBER_H__
#define __IS_NUMBER_H__

#include "attrs.h" /* attr_const */
#include "bool.h" /* bool */
#include "inline.h" /* inline */

static inline attr_const bool is_digit(char const c)
{
    return (unsigned char)(c - '0') < 10;
}

static inline attr_const bool is_number(char const* str)
{
    if (*str == '\0')
        return false;
    while (is_digit(*++str));
    return *str == '\0';
}

#endif
