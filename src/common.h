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

#ifndef INFIDL_COMMON_H
#define INFIDL_COMMON_H
#else
#error redefining INFIDL_COMMON_H
#endif

#include <unistd.h>
#include <limits.h>
#include <libgen.h> /* basename() */
#include <inttypes.h> /* strtoimax(), strtoumax() */
#include <sys/stat.h> /* mkdir(), stat() */

#include "log.h"
#include "progress.h"

#ifndef OFF_T_MAX
#define OFF_T_MAX (off_t)( sizeof(off_t) < sizeof(int64_t) ? INT32_MAX : INT64_MAX )
#endif
#define PCT(x1, x2) ((x1))*100.0/((x2))

void curl_set_ranges(CURL *handle, chunk_s *chunk);

#if !defined(__CYGWIN__) && !defined(__MSYS__) && defined(HAVE_GETMODULEFILENAME)
char* windows_exe_path();
#endif

#define INFIDL_FREE(x) do { free((x)); (x) = NULL; } while(0)

char* infidl_lstrip(char *str);
void* infidl_calloc(size_t nmemb, size_t size);
void* infidl_malloc(size_t size);
void* infidl_realloc(void *ptr, size_t size);
char* infidl_strdup(const char *str);
int infidl_strcmp(const char *s1, const char *s2);
int infidl_strcasecmp(const char *s1, const char *s2);

void infidl_custom_headers_free_all(char **headers);
char** infidl_custom_headers_append(char **headers, char *header);

void infidl_fflush(const char *label, FILE *f);
void infidl_fwrite_fflush(const void *read_buf, size_t size, size_t nmemb, FILE *out_file, const char *out_name, off_t offset_info);
void infidl_fclose(const char *label, FILE *f);
void infidl_fseeko(const char *label, FILE *f, off_t offset, int whence);
off_t infidl_ftello(const char *label, FILE *f);
off_t infidl_fsizeo(const char *label, FILE *f);
off_t infidl_fsize_sys(char *file_path);
time_t infidl_file_mtime(char *file_path);
int infidl_mkdir(const char *path, mode_t mode);

void infidl_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine) (void *), void *arg);
void infidl_pthread_join_accept_einval(pthread_t thread, void **retval);
void infidl_pthread_mutex_lock_retry_deadlock(pthread_mutex_t *mutex);
void infidl_pthread_mutex_unlock(pthread_mutex_t *mutex);

void infidl_block_sig_pth();
void infidl_unblock_sig_pth();

#ifdef HAVE_SIGADDSET
void infidl_sigaddset(sigset_t *set, int signum);
void infidl_sigemptyset(sigset_t *set);
#endif

#ifdef HAVE_SIGACTION
void ignore_sig(int sig, struct sigaction *sa_save);
void restore_sig_handler(int sig, struct sigaction *sa_restore);
void infidl_sigaction(int sig, const struct sigaction *restrict act, struct sigaction *restrict oact);
#else
void infidl_win_signal(int signum, void (*handler)(int));
#endif

void infidl_snprintf(bool allow_truncation, char *str, size_t size, const char *format, ...) __attribute__(( format(INFIDL_PRINTF_FORMAT,4,5) ));
void infidl_fputc(int c, FILE *stream, const char *label);
void infidl_fputs(const char *str, FILE *stream, const char *label);
void infidl_fputs_count(uintmax_t count, const char* str, FILE* stream, const char *label);
double human_size(double size);
const char* human_size_suffix(double size);
size_t s_num_digits(intmax_t num);
size_t u_num_digits(uintmax_t num);
size_t infidl_min(size_t a, size_t b);
size_t infidl_max(size_t a, size_t b);
off_t infidl_max_o(off_t a, off_t b);
size_t infidl_max_z_umax(uintmax_t a, uintmax_t b);
char* infidl_getcwd(char *buf, size_t size);
char* valid_filename(const char *pre_valid);
char* trunc_filename(const char *pre_trunc, int keep_ext);
size_t parse_num_d(const char *num_char);
off_t parse_num_o(const char *num_char, size_t suff_len);
size_t parse_num_z(const char *num_char, size_t suff_len);

/* vim: set filetype=c ts=2 sw=2 et spell foldmethod=syntax: */
