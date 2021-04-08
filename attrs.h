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

#pragma once
#ifndef __ATTRS_H__
#define __ATTRS_H__

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 96) || (defined(__clang__) && __has_attribute(pure))
#define attr_pure __attribute__ ((pure))
#else
#define attr_pure
#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 5) || (defined(__clang__) && __has_attribute(const))
#define attr_const __attribute__ ((const))
#else
#define attr_const
#endif

#ifndef NO_ATTR_ALIASES
#define pure attr_pure
#define fconst attr_const
#endif

#endif
