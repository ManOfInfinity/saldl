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

#ifndef INFIDL_UTILS_H
#define INFIDL_UTILS_H
#else
#error redefining INFIDL_UTILS_H
#endif

#include "write_modes.h"
#include "merge.h"

char* infidl_user_agent();
void chunks_init(info_s*);
void check_remote_file_size(info_s*);
void get_info(info_s*);
void set_info(info_s*);
void print_chunk_info(info_s*);
void check_url(char*);
void global_progress_init(info_s*);
void global_progress_update(info_s *info_ptr, bool init);
void set_params(thread_s *thread, info_s *info_ptr, char *url);
void set_progress_params(thread_s*, info_s*);
void set_single_mode(info_s*);
void check_files_and_dirs(info_s *info_ptr);
void infidl_perform(thread_s*);
void* thread_func(void*);
void curl_cleanup(info_s*);

/* vim: set filetype=c ts=2 sw=2 et spell foldmethod=syntax: */
