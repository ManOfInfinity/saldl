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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <direct.h>
#define SEG_MKDIR(path) _mkdir(path)
#else
#include <pthread.h>
#include <unistd.h>
#define SEG_MKDIR(path) mkdir(path, 0755)
#endif

#include "segdl.h"
#include "log.h"

/* ========================================================================= */
/*  Input file parser (aria2c -i format)                                     */
/*                                                                           */
/*  Format:                                                                  */
/*    URL                                                                    */
/*    <TAB>dir=<path>                                                        */
/*    <TAB>out=<filename>                                                    */
/*    URL2                                                                   */
/*    ...                                                                    */
/* ========================================================================= */

static char *strip_newline(char *s) {
  size_t len = strlen(s);
  while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r')) s[--len] = '\0';
  return s;
}

static int parse_input(FILE *fp, seg_list_s *list) {
  size_t capacity = 256;
  list->entries = calloc(capacity, sizeof(seg_entry_s));
  list->count = 0;

  char line[8192];
  seg_entry_s *current = NULL;

  while (fgets(line, sizeof(line), fp)) {
    strip_newline(line);

    /* Skip empty lines and comments */
    if (line[0] == '\0' || line[0] == '#') continue;

    if (line[0] == '\t' || line[0] == ' ') {
      /* Option line for current entry */
      if (!current) continue;
      char *opt = line;
      while (*opt == '\t' || *opt == ' ') opt++;

      if (strncmp(opt, "dir=", 4) == 0) {
        free(current->dir);
        current->dir = strdup(opt + 4);
      } else if (strncmp(opt, "out=", 4) == 0) {
        free(current->out);
        current->out = strdup(opt + 4);
      }
      /* Ignore other aria2c options silently */
    } else {
      /* New URL */
      if (list->count >= capacity) {
        capacity *= 2;
        list->entries = realloc(list->entries, capacity * sizeof(seg_entry_s));
      }
      current = &list->entries[list->count];
      memset(current, 0, sizeof(seg_entry_s));
      current->url = strdup(line);
      list->count++;
    }
  }

  return (list->count > 0) ? 0 : -1;
}

static void free_seg_list(seg_list_s *list) {
  for (size_t i = 0; i < list->count; i++) {
    free(list->entries[i].url);
    free(list->entries[i].dir);
    free(list->entries[i].out);
  }
  free(list->entries);
}

/* ========================================================================= */
/*  Progress display                                                         */
/* ========================================================================= */

typedef struct {
  size_t completed;
  size_t failed;
  size_t total;
  size_t total_bytes;
#ifdef _WIN32
  CRITICAL_SECTION lock;
#else
  pthread_mutex_t lock;
#endif
} seg_progress_s;

static void seg_progress_init(seg_progress_s *p, size_t total) {
  memset(p, 0, sizeof(*p));
  p->total = total;
#ifdef _WIN32
  InitializeCriticalSection(&p->lock);
#else
  pthread_mutex_init(&p->lock, NULL);
#endif
}

static void seg_progress_update(seg_progress_s *p, size_t bytes, bool success) {
#ifdef _WIN32
  EnterCriticalSection(&p->lock);
#else
  pthread_mutex_lock(&p->lock);
#endif

  if (success) p->completed++; else p->failed++;
  p->total_bytes += bytes;

  size_t done = p->completed + p->failed;
  double pct = (double)done * 100.0 / (double)p->total;
  int bar_width = 40;
  int filled = (int)(pct * bar_width / 100.0);
  if (filled > bar_width) filled = bar_width;

  fprintf(stderr, "\r [");
  if (done == p->total) {
    fprintf(stderr, "\033[32m");
    for (int i = 0; i < bar_width; i++) fprintf(stderr, "=");
    fprintf(stderr, "\033[0m");
  } else {
    if (filled > 0) {
      fprintf(stderr, "\033[33m");
      for (int i = 0; i < filled - 1; i++) fprintf(stderr, "=");
      fprintf(stderr, ">");
      fprintf(stderr, "\033[0m");
    }
    fprintf(stderr, "\033[90m");
    for (int i = filled; i < bar_width; i++)
      fprintf(stderr, "%s", ((i - filled) % 3 == 2) ? " " : "-");
    fprintf(stderr, "\033[0m");
  }

  double size = (double)p->total_bytes;
  const char *suffix = "B";
  if (size >= 1073741824.0) { size /= 1073741824.0; suffix = "GiB"; }
  else if (size >= 1048576.0) { size /= 1048576.0; suffix = "MiB"; }
  else if (size >= 1024.0) { size /= 1024.0; suffix = "KiB"; }

  const char *color = (done == p->total) ? "\033[32m" : "\033[33m";
  fprintf(stderr, "] %s%5.1f%%\033[0m | %zu/%zu segments | %.1f %s",
      color, pct, done, p->total, size, suffix);
  if (p->failed > 0)
    fprintf(stderr, " | \033[31m%zu failed\033[0m", p->failed);
  fprintf(stderr, "   ");
  fflush(stderr);

#ifdef _WIN32
  LeaveCriticalSection(&p->lock);
#else
  pthread_mutex_unlock(&p->lock);
#endif
}

static void seg_progress_finish(seg_progress_s *p) {
  fprintf(stderr, "\n");
#ifdef _WIN32
  DeleteCriticalSection(&p->lock);
#else
  pthread_mutex_destroy(&p->lock);
#endif
}

/* ========================================================================= */
/*  Download worker                                                          */
/* ========================================================================= */

typedef struct {
  segdl_params_s *params;
  seg_entry_s *entry;
  char *output_path;
  seg_progress_s *progress;
  int result;
} seg_task_s;

static size_t write_file_cb(void *contents, size_t size, size_t nmemb, void *userp) {
  return fwrite(contents, size, nmemb, (FILE *)userp);
}

static void apply_segdl_curl_opts(CURL *curl, segdl_params_s *params) {
  if (params->proxy && !params->no_proxy)
    curl_easy_setopt(curl, CURLOPT_PROXY, params->proxy);
  if (params->tunnel_proxy && !params->no_proxy) {
    curl_easy_setopt(curl, CURLOPT_PROXY, params->tunnel_proxy);
    curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
  }
  if (params->no_proxy)
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
  if (params->user_agent && !params->no_user_agent)
    curl_easy_setopt(curl, CURLOPT_USERAGENT, params->user_agent);
  if (params->referer)
    curl_easy_setopt(curl, CURLOPT_REFERER, params->referer);
  if (params->tls_no_verify) {
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  if (params->forced_ip_protocol == 4)
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
  else if (params->forced_ip_protocol == 6)
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
  if (params->inline_cookies)
    curl_easy_setopt(curl, CURLOPT_COOKIE, params->inline_cookies);
  if (params->cookie_file)
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, params->cookie_file);

  if (params->custom_headers) {
    struct curl_slist *headers = NULL;
    for (int i = 0; params->custom_headers[i]; i++)
      headers = curl_slist_append(headers, params->custom_headers[i]);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);

#ifdef _WIN32
  curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif
}

static int download_one_segment(segdl_params_s *params, const char *url,
                                const char *output_path, size_t *bytes_out) {
  *bytes_out = 0;
  size_t max_tries = params->max_retries > 0 ? params->max_retries : 15;

  for (size_t attempt = 0; attempt < max_tries; attempt++) {
    if (attempt > 0) {
      size_t wait = params->retry_wait > 0 ? params->retry_wait : 5;
#ifdef _WIN32
      Sleep((DWORD)(wait * 1000));
#else
      sleep((unsigned int)wait);
#endif
    }

    CURL *curl = curl_easy_init();
    if (!curl) continue;

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
      curl_easy_cleanup(curl);
      return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    apply_segdl_curl_opts(curl, params);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    double dl_size = 0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &dl_size);
    *bytes_out = (size_t)dl_size;

    curl_easy_cleanup(curl);
    fclose(fp);

    if (res == CURLE_OK && http_code < 400 && dl_size > 0) {
      return 0;
    }

    /* Log failure */
    if (res != CURLE_OK) {
      fprintf(stderr, "\n[segdl] ERR: %s — %s\n", output_path, curl_easy_strerror(res));
    } else if (http_code >= 400) {
      fprintf(stderr, "\n[segdl] ERR: %s — HTTP %ld\n", output_path, http_code);
    } else {
      fprintf(stderr, "\n[segdl] ERR: %s — 0 bytes received (HTTP %ld)\n", output_path, http_code);
    }

    /* Remove failed partial file */
    remove(output_path);
  }

  return -1;
}

/* ========================================================================= */
/*  Concurrent download engine                                               */
/* ========================================================================= */

typedef struct {
  segdl_params_s *params;
  seg_entry_s *entries;
  size_t *task_indices;   /* indices into entries array for this worker */
  size_t task_count;
  seg_progress_s *progress;
  int result;
} worker_s;

static void *seg_worker_thread(void *arg) {
  worker_s *w = (worker_s *)arg;
  w->result = 0;

  for (size_t i = 0; i < w->task_count; i++) {
    seg_entry_s *entry = &w->entries[w->task_indices[i]];

    /* Build output path */
    const char *dir = entry->dir ? entry->dir : w->params->default_dir;
    char path[4096];
    if (dir && entry->out) {
      snprintf(path, sizeof(path), "%s/%s", dir, entry->out);
    } else if (entry->out) {
      snprintf(path, sizeof(path), "%s", entry->out);
    } else if (dir) {
      snprintf(path, sizeof(path), "%s/%08zu.ts", dir, w->task_indices[i]);
    } else {
      snprintf(path, sizeof(path), "%08zu.ts", w->task_indices[i]);
    }

    size_t bytes = 0;
    int ret = download_one_segment(w->params, entry->url, path, &bytes);
    seg_progress_update(w->progress, bytes, ret == 0);
    if (ret != 0) w->result = -1;
  }

  return NULL;
}

/* ========================================================================= */
/*  Public API                                                               */
/* ========================================================================= */

static void ensure_dirs(seg_list_s *list, const char *default_dir) {
  /* Create default dir if needed */
  if (default_dir) {
    SEG_MKDIR(default_dir);
  }
  /* Create per-entry dirs */
  for (size_t i = 0; i < list->count; i++) {
    const char *dir = list->entries[i].dir ? list->entries[i].dir : default_dir;
    if (dir) SEG_MKDIR(dir);
  }
}

int segdl_download(segdl_params_s *params) {
  curl_global_init(CURL_GLOBAL_ALL);

  /* Read input */
  FILE *input_fp;
  if (strcmp(params->input_file, "-") == 0) {
    input_fp = stdin;
  } else {
    input_fp = fopen(params->input_file, "r");
    if (!input_fp) {
      fatal("segdl", "Cannot open input file: %s: %s", params->input_file, strerror(errno));
      return -1;
    }
  }

  seg_list_s list = {0};
  if (parse_input(input_fp, &list) != 0) {
    fatal("segdl", "No entries found in input file");
    if (input_fp != stdin) fclose(input_fp);
    return -1;
  }
  if (input_fp != stdin) fclose(input_fp);

  info_msg("segdl", "Segments: %zu", list.count);
  if (params->show_details) {
    fprintf(stderr, "Segments: %zu\n", list.count);
    if (params->default_dir)
      fprintf(stderr, "Output Dir: %s\n", params->default_dir);
  }

  ensure_dirs(&list, params->default_dir);

  /* Progress */
  seg_progress_s progress;
  seg_progress_init(&progress, list.count);

  size_t conns = params->connections;
  if (conns < 1) conns = 6;
  if (conns > list.count) conns = list.count;

  /* Distribute segments across workers round-robin to keep order balanced */
  worker_s *workers = calloc(conns, sizeof(worker_s));
  size_t **indices = calloc(conns, sizeof(size_t *));
  size_t *counts = calloc(conns, sizeof(size_t));

  for (size_t w = 0; w < conns; w++) {
    indices[w] = calloc(list.count, sizeof(size_t)); /* over-allocate */
  }

  for (size_t i = 0; i < list.count; i++) {
    size_t w = i % conns;
    indices[w][counts[w]++] = i;
  }

#ifdef _WIN32
  HANDLE *threads = calloc(conns, sizeof(HANDLE));
#else
  pthread_t *threads = calloc(conns, sizeof(pthread_t));
#endif

  for (size_t w = 0; w < conns; w++) {
    workers[w].params = params;
    workers[w].entries = list.entries;
    workers[w].task_indices = indices[w];
    workers[w].task_count = counts[w];
    workers[w].progress = &progress;
    workers[w].result = 0;

#ifdef _WIN32
    threads[w] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)seg_worker_thread, &workers[w], 0, NULL);
#else
    pthread_create(&threads[w], NULL, seg_worker_thread, &workers[w]);
#endif
  }

  /* Wait for all */
  int fail = 0;
  for (size_t w = 0; w < conns; w++) {
#ifdef _WIN32
    WaitForSingleObject(threads[w], INFINITE);
    CloseHandle(threads[w]);
#else
    pthread_join(threads[w], NULL);
#endif
    if (workers[w].result != 0) fail = 1;
    free(indices[w]);
  }

  seg_progress_finish(&progress);

  if (fail) {
    err_msg("segdl", "Some segments failed to download (%zu/%zu)", progress.failed, progress.total);
  } else {
    info_msg("segdl", "All %zu segments downloaded", list.count);
  }

  free(workers);
  free(threads);
  free(indices);
  free(counts);
  free_seg_list(&list);

  return fail ? -1 : 0;
}

/* vim: set filetype=c ts=2 sw=2 et spell foldmethod=syntax: */
