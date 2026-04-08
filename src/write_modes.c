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

#include "write_modes.h"
#include "merge.h" /* set_chunk_merged() */

#ifdef HAVE_MMAP
#include <sys/mman.h>
#endif

/* Default (tmp files) mode */
static void prepare_storage_tmpf(chunk_s *chunk, file_s* dir) {
  INFIDL_ASSERT(chunk);
  INFIDL_ASSERT(dir);
  INFIDL_ASSERT(dir->name);

  file_s *tmp_f = infidl_calloc (1, sizeof(file_s));
  tmp_f->name = infidl_calloc(PATH_MAX, sizeof(char));
  infidl_snprintf(false, tmp_f->name, PATH_MAX, "%s/%"SAL_ZU"", dir->name, chunk->idx);

  if (chunk->size_complete) {
    if (! (tmp_f->file = fopen(tmp_f->name, "rb+"))) {
      fatal(FN, "Failed to open %s for read/write: %s", tmp_f->name, strerror(errno));
    }
    infidl_fseeko(tmp_f->name, tmp_f->file, chunk->size_complete, SEEK_SET);
  }
  else {
    if (! (tmp_f->file = fopen(tmp_f->name, "wb+"))) {
      fatal(FN, "Failed to open %s for read/write: %s", tmp_f->name, strerror(errno));
    }
  }

  chunk->storage = tmp_f;
}

static void reset_storage_tmpf(thread_s *thread) {
  INFIDL_ASSERT(thread);
  INFIDL_ASSERT(thread->chunk);

  file_s *storage = thread->chunk->storage;
  INFIDL_ASSERT(storage);
  INFIDL_ASSERT(storage->name);
  INFIDL_ASSERT(storage->file);

  infidl_fflush(storage->name, storage->file);

  off_t size_complete = infidl_max_o(infidl_fsizeo(storage->name, storage->file), 4096) - 4096;
  INFIDL_ASSERT((uintmax_t)size_complete <= SIZE_MAX);
  thread->chunk->size_complete = (size_t)size_complete;

  INFIDL_ASSERT(thread->ehandle);
  curl_set_ranges(thread->ehandle, thread->chunk);

  info_msg(FN, "restarting chunk %s from offset %"SAL_ZU"", storage->name, thread->chunk->size_complete);
  infidl_fseeko(storage->name, storage->file, thread->chunk->size_complete, SEEK_SET);
  thread->chunk->size_complete = 0;
}

static size_t file_write_function(void  *ptr, size_t  size, size_t nmemb, void *data) {
  size_t realsize = size * nmemb;
  file_s *tmp_f = data;

  INFIDL_ASSERT(ptr);
  INFIDL_ASSERT(tmp_f);
  INFIDL_ASSERT(tmp_f->file);
  INFIDL_ASSERT(tmp_f->name);

  infidl_fwrite_fflush(ptr, size, nmemb, tmp_f->file, tmp_f->name, 0);

  return realsize;
}

static int tmpf_write_use_mmap(chunk_s *chunk, info_s *info_ptr, off_t offset) {
  INFIDL_ASSERT(chunk);
  INFIDL_ASSERT(info_ptr);
#ifdef HAVE_MMAP
  file_s *tmp_f = chunk->storage;

  INFIDL_ASSERT(tmp_f);
  INFIDL_ASSERT(tmp_f->file);


  int chunk_fd = fileno(tmp_f->file);
  INFIDL_ASSERT(chunk_fd > 0);

  INFIDL_ASSERT(chunk->size);

  void *tmp_buf = mmap(0, chunk->size, PROT_READ, MAP_SHARED, chunk_fd, 0);
  if (tmp_buf == MAP_FAILED) {
    warn_msg(FN, "mmap()ing chunk file %"SAL_ZU" failed, %s.", chunk->idx, strerror(errno));
    warn_msg(FN, "Falling back to fread()/fwrite() .");
    return -2;
  }

  infidl_fwrite_fflush(tmp_buf, 1, chunk->size, info_ptr->file, info_ptr->part_filename, offset);

  if (munmap(tmp_buf, chunk->size)) {
    warn_msg(FN, "munmap()ing chunk file %"SAL_ZU" failed.", chunk->idx);
  }

  return 0;
#else
  (void) offset;
  return -1;
#endif
}

static int merge_finished_tmpf(chunk_s *chunk, info_s *info_ptr) {
  INFIDL_ASSERT(chunk);
  INFIDL_ASSERT(info_ptr);

  file_s *tmp_f = chunk->storage;

  INFIDL_ASSERT(tmp_f);
  INFIDL_ASSERT(tmp_f->name);
  INFIDL_ASSERT(tmp_f->file);

  INFIDL_ASSERT(info_ptr->params);
  INFIDL_ASSERT(info_ptr->params->chunk_size);

  off_t offset = (off_t)chunk->idx * info_ptr->params->chunk_size;

  INFIDL_ASSERT(info_ptr->file);
  INFIDL_ASSERT(info_ptr->part_filename);

  infidl_fseeko(tmp_f->name, tmp_f->file, 0, SEEK_SET);
  infidl_fflush(tmp_f->name, tmp_f->file);

  if (!info_ptr->params->to_stdout) {
    infidl_fseeko(info_ptr->part_filename, info_ptr->file, offset, SEEK_SET);
  }

  INFIDL_ASSERT(chunk->size);


  if (info_ptr->params->no_mmap || tmpf_write_use_mmap(chunk, info_ptr, offset)) {
    size_t size = chunk->size;
    char *tmp_buf = infidl_calloc(size, sizeof(char));

    size_t f_ret = 0;
    if ( ( f_ret = fread(tmp_buf, 1, size, tmp_f->file) ) != size ) {
      fatal(FN, "Reading from tmp file %s at offset %"SAL_JD" failed, chunk_size=%"SAL_ZU", fread() returned %"SAL_ZU".", tmp_f->name, (intmax_t)offset, size, f_ret);
    }

    infidl_fwrite_fflush(tmp_buf, 1, size, info_ptr->file, info_ptr->part_filename, offset);

    INFIDL_FREE(tmp_buf);
  }

  set_chunk_merged(chunk);
  infidl_fclose(tmp_f->name, tmp_f->file);

  if ( remove(tmp_f->name) ) {
    fatal(FN, "Removing file %s failed: %s", tmp_f->name, strerror(errno));
  }

  INFIDL_FREE(tmp_f->name);
  INFIDL_FREE(tmp_f);

  return 0;
}

/* Memory (buffers) mode */
static void prepare_storage_mem(chunk_s *chunk) {
  INFIDL_ASSERT(chunk);
  INFIDL_ASSERT(chunk->size);

  mem_s *buf = infidl_calloc (1, sizeof(mem_s));
  buf->memory = infidl_calloc(chunk->size, sizeof(char));
  buf->allocated_size = chunk->size;
  chunk->storage = buf;
}

static void reset_storage_mem(thread_s *thread) {
  mem_s *buf = thread->chunk->storage;
  INFIDL_ASSERT(buf);
  buf->size = 0;
}

static size_t  mem_write_function(void  *ptr,  size_t  size, size_t nmemb, void *data) {
  mem_s *mem = data;

  INFIDL_ASSERT(mem);
  INFIDL_ASSERT(mem->memory); // Preallocation failed
  INFIDL_ASSERT(mem->size <= mem->allocated_size);

  size_t realsize = size * nmemb;

  INFIDL_ASSERT(realsize);
  INFIDL_ASSERT(ptr);

  memmove(&(mem->memory[mem->size]), ptr, realsize);
  mem->size += realsize;

  return realsize;
}

static int merge_finished_mem(chunk_s *chunk, info_s *info_ptr) {
  INFIDL_ASSERT(chunk);
  INFIDL_ASSERT(info_ptr);

  size_t size = chunk->size;
  off_t offset = (off_t)chunk->idx * info_ptr->params->chunk_size;

  mem_s *buf = chunk->storage;

  INFIDL_ASSERT(size);
  INFIDL_ASSERT(info_ptr->params);
  INFIDL_ASSERT(info_ptr->params->chunk_size);

  if (!info_ptr->params->to_stdout) {
    infidl_fseeko(info_ptr->part_filename, info_ptr->file, offset, SEEK_SET);
  }

  infidl_fwrite_fflush(buf->memory, 1, size, info_ptr->file, info_ptr->part_filename, offset);

  INFIDL_FREE(buf->memory);
  INFIDL_FREE(buf);

  set_chunk_merged(chunk);

  return 0;
}

/* Single mode */
static void prepare_storage_single(chunk_s *chunk, file_s *part_file) {
  INFIDL_ASSERT(chunk);
  INFIDL_ASSERT(part_file);
  INFIDL_ASSERT(part_file->file);
  INFIDL_ASSERT(part_file->name);

  if (chunk->size_complete) {
    infidl_fseeko(part_file->name, part_file->file, chunk->size_complete, SEEK_SET);
  }

  chunk->storage = part_file;
}

static void reset_storage_single(thread_s *thread) {
  INFIDL_ASSERT(thread);
  INFIDL_ASSERT(thread->chunk);

  file_s *storage = thread->chunk->storage;

  INFIDL_ASSERT(storage);
  INFIDL_ASSERT(storage->name);
  INFIDL_ASSERT(storage->file);

  off_t offset = infidl_max_o(infidl_fsizeo(storage->name, storage->file), 4096) - 4096;

  INFIDL_ASSERT(thread->ehandle);
  curl_easy_setopt(thread->ehandle, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)offset);

  infidl_fseeko(storage->name, storage->file, offset, SEEK_SET);
  info_msg(FN, "restarting from offset %"SAL_JD"", (intmax_t)offset);
}

static void reset_storage_single_pipe(thread_s *thread) {
  (void)thread;
  fatal(FN, "Handling non-fatal failures requires seeking.");
}

static size_t single_write_function(void  *ptr, size_t  size, size_t nmemb, void *data) {
  return file_write_function(ptr, size, nmemb, data);
}

static int merge_finished_single() {
  return 0;
}

/* Null (read-only) mode */
static void prepare_storage_null() {
}

static void reset_storage_null() {
}

static size_t null_write_function(void  *ptr,  size_t  size, size_t nmemb, void *data) {
  (void)ptr;

  if (data) {
    /* Remember: This is why getting info with GET works, this causes error 26 */
    return 0;
  }

  return size*nmemb;
}

static int merge_finished_null(chunk_s *chunk) {
  set_chunk_merged(chunk);
  return 0;
}

/* Setters */

void set_modes(info_s *info_ptr) {
  INFIDL_ASSERT(info_ptr);
  INFIDL_ASSERT(info_ptr->params);

  infidl_params *params_ptr = info_ptr->params;
  file_s *storage_info_ptr = &info_ptr->storage_info;
  void(*reset_storage)();

  if (params_ptr->read_only) {
    info_ptr->prepare_storage = &prepare_storage_null;
    info_ptr->merge_finished = &merge_finished_null;
    reset_storage = &reset_storage_null;
  }
  else if (params_ptr->single_mode) {
    info_msg(FN, "single mode, writing to %s directly.", info_ptr->part_filename);
    storage_info_ptr->name = info_ptr->part_filename;
    storage_info_ptr->file = info_ptr->file;
    info_ptr->prepare_storage = &prepare_storage_single;
    info_ptr->merge_finished = &merge_finished_single;
    if (params_ptr->to_stdout) {
      reset_storage = &reset_storage_single_pipe;
    }
    else {
      reset_storage = &reset_storage_single;
    }
  }
  else if (params_ptr->mem_bufs) {
    info_ptr->prepare_storage = &prepare_storage_mem;
    info_ptr->merge_finished = &merge_finished_mem;
    reset_storage = &reset_storage_mem;
  }
  else {
    storage_info_ptr->name = info_ptr->tmp_dirname;
    info_ptr->prepare_storage = &prepare_storage_tmpf;
    info_ptr->merge_finished = &merge_finished_tmpf;
    reset_storage = &reset_storage_tmpf;
  }

  /* set *reset_storage() in thread struct instances */
  for (size_t counter = 0; counter < params_ptr->num_connections; counter++) {
    info_ptr->threads[counter].reset_storage = reset_storage;
  }
}

void set_write_opts(CURL* handle, void* storage, infidl_params *params_ptr, bool no_body) {
  INFIDL_ASSERT(handle);
  INFIDL_ASSERT(params_ptr);

  curl_easy_setopt(handle, CURLOPT_WRITEDATA, storage);

  if (params_ptr->read_only || !storage) {
    curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION, null_write_function);
    if (no_body) {
      /* We are only getting info */
      curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)1); // setting to not NULL
    }
  }
  else if (params_ptr->single_mode) {
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, single_write_function);
  }
  else if (params_ptr->mem_bufs) {
    curl_easy_setopt(handle,CURLOPT_WRITEFUNCTION,mem_write_function);
  }
  else {
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, file_write_function);
  }
}

/* vim: set filetype=c ts=2 sw=2 et spell foldmethod=syntax: */
