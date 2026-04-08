/*
    This file is a part of infidl.

    Copyright (C) 2026 ManOfInfinity <https://github.com/ManOfInfinity>
    https://github.com/ManOfInfinity/infidl

    infidl is free software: you can redistribute it and/or modify
    it under the terms of the Affero GNU General Public License as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Affero GNU General Public License for more details.

    You should have received a copy of the Affero GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef INFIDL_INFIDL_DEFAULTS_H
#define INFIDL_INFIDL_DEFAULTS_H
#else
#error redefining INFIDL_INFIDL_DEFAULTS_H
#endif

/* Project Info */
#define INFIDL_NAME "infidl"
#define INFIDL_WWW "https://github.com/ManOfInfinity/infidl"
#define INFIDL_BUG "https://github.com/ManOfInfinity/infidl/issues"

/* Version is defined by the build system via git describe.
 * Falls back to this default if git is not available. */
#ifndef INFIDL_VERSION
#define INFIDL_VERSION "2.5"
#endif

/* Default Params */
#define INFIDL_DEF_STATUS_REFRESH_INTERVAL 0.3

/* Default Params (configurable) */
#ifndef INFIDL_DEF_CHUNK_SIZE
#define INFIDL_DEF_CHUNK_SIZE 1*1024*1024 /* 1.00 MiB */
#endif

#ifndef INFIDL_DEF_NUM_CONNECTIONS
#define INFIDL_DEF_NUM_CONNECTIONS 6
#endif

/* Constants */
#define INFIDL_STATUS_INITIAL_INTERVAL 0.5

/* vim: set filetype=c ts=2 sw=2 et spell foldmethod=syntax: */
