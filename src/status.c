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

#include "events.h"
#include "utime.h"

#define DEF_STATUS_LINES 2
#define BAR_FILL_CHAR "\xe2\x96\xa0"   /* U+25A0 BLACK SQUARE ■ */
#define BAR_EMPTY_CHAR "\xe2\x96\xa1"  /* U+25A1 WHITE SQUARE □ */

size_t num_of_lines(info_s *info_ptr, int cols) {
  (void)info_ptr;
  (void)cols;
  return DEF_STATUS_LINES;
}

static void format_size(char *buf, size_t buflen, double size) {
  if (size >= 1024.0*1024*1024) {
    snprintf(buf, buflen, "%.2f GiB", size / (1024.0*1024*1024));
  } else if (size >= 1024.0*1024) {
    snprintf(buf, buflen, "%.2f MiB", size / (1024.0*1024));
  } else if (size >= 1024.0) {
    snprintf(buf, buflen, "%.2f KiB", size / 1024.0);
  } else {
    snprintf(buf, buflen, "%.0f B", size);
  }
}

static void format_time(char *buf, size_t buflen, double seconds) {
  if (seconds >= 3600) {
    snprintf(buf, buflen, "%dh%02dm%02ds", (int)(seconds/3600), (int)((int)seconds%3600)/60, (int)seconds%60);
  } else if (seconds >= 60) {
    snprintf(buf, buflen, "%dm%02ds", (int)(seconds/60), (int)seconds%60);
  } else {
    snprintf(buf, buflen, "%.0fs", seconds);
  }
}

static void render_progress_bar(info_s *info_ptr, progress_s *p) {
  int cols = tty_width();
  if (cols <= 0) cols = 80;

  double pct = 0;
  if (info_ptr->file_size > 0) {
    pct = (double)p->complete_size * 100.0 / (double)info_ptr->file_size;
    if (pct > 100.0) pct = 100.0;
  }

  char size_done[32], size_total[32], rate_buf[32], eta_buf[32];
  format_size(size_done, sizeof(size_done), (double)p->complete_size);
  format_size(size_total, sizeof(size_total), (double)info_ptr->file_size);
  format_size(rate_buf, sizeof(rate_buf), p->curr_rate > 0 ? p->curr_rate : p->rate);
  format_time(eta_buf, sizeof(eta_buf), p->curr_rem < (double)INT64_MAX ? p->curr_rem : p->rem);

  /* Fixed bar width for consistent display */
  int bar_width = 30;
  if (cols > 120) bar_width = 40;
  else if (cols < 60) bar_width = 15;

  int filled = (int)(pct * bar_width / 100.0);
  if (filled > bar_width) filled = bar_width;

  /* Render bar */
  fprintf(stderr, "\r%s [", erase_after);

  for (int i = 0; i < filled; i++) {
    fprintf(stderr, BAR_FILL_CHAR);
  }

  for (int i = filled; i < bar_width; i++) {
    fprintf(stderr, BAR_EMPTY_CHAR);
  }

  /* Stats with fixed-width fields so display doesn't jump */
  if (p->complete_size == info_ptr->file_size) {
    char dur_buf[32];
    format_time(dur_buf, sizeof(dur_buf), p->dur);
    fprintf(stderr, "] %5.1f%% | %10s / %10s | %10s/s | %s",
        pct, size_done, size_total, rate_buf, dur_buf);
  } else if (p->rate > 0 || p->curr_rate > 0) {
    fprintf(stderr, "] %5.1f%% | %10s / %10s | %10s/s | ETA %8s",
        pct, size_done, size_total, rate_buf, eta_buf);
  } else {
    fprintf(stderr, "] %5.1f%% | %10s / %10s",
        pct, size_done, size_total);
  }

  fflush(stderr);
}

static void status_update_cb(evutil_socket_t fd, short what, void *arg) {
  info_s *info_ptr = arg;
  infidl_params *params_ptr = info_ptr->params;

  progress_s *p = &(info_ptr->global_progress);
  status_s *status_ptr = &info_ptr->status;
  event_s *ev_status = &info_ptr->ev_status;

  double params_refresh = params_ptr->status_refresh_interval;
  double refresh_interval = params_refresh ? params_refresh : INFIDL_DEF_STATUS_REFRESH_INTERVAL;

  /* Update number of lines in case tty width changes */
  int cols = tty_width() >= 0 ? tty_width() : 0;
  status_ptr->lines = num_of_lines(info_ptr, cols);

  debug_event_msg(FN, "callback no. %"SAL_JU" for triggered event %s, with what %d", ++ev_status->num_of_calls, str_EVENT_FD(fd) , what);

  /* We check if the merge loop is already de-initialized to not lose status of any merged chunks */
  if ( (info_ptr->session_status == SESSION_INTERRUPTED || !exist_prg(info_ptr, PRG_MERGED, false) ) && info_ptr->ev_merge.event_status < EVENT_INIT) {
    events_deactivate(ev_status);
  }

  p->curr = infidl_utime();
  p->dur = p->curr - p->start;
  p->curr_dur = p->curr - p->prev;
  global_progress_update(info_ptr, false);

  /* Skip if --no-status */
  if (info_ptr->params->no_status) {
    return;
  }

  off_t session_complete_size = infidl_max_o(p->complete_size - p->initial_complete_size, 0);
  off_t rem_size = info_ptr->file_size - p->complete_size;

  /* Calculate rates, remaining times */
  if (p->dur >= INFIDL_STATUS_INITIAL_INTERVAL) {
    p->rate = session_complete_size/p->dur;
    p->rem = p->rate ? rem_size/p->rate : (double)INT64_MAX;
  }

  if (p->curr_dur >= refresh_interval ||
      (p->dur  >= INFIDL_STATUS_INITIAL_INTERVAL && p->dur < refresh_interval) ||
      p->complete_size == info_ptr->file_size) {
    off_t curr_complete_size = infidl_max_o(p->complete_size - p->dlprev, 0);
    p->curr_rate = curr_complete_size/p->curr_dur;
    p->curr_rem = p->curr_rate ? rem_size/p->curr_rate : (double)INT64_MAX;

    p->prev = p->curr;
    p->dlprev = p->complete_size;

    render_progress_bar(info_ptr, p);
  }
}

void* status_display(void *void_info_ptr) {
  /* prep  */
  info_s *info_ptr = void_info_ptr;
  status_s *status_ptr = &info_ptr->status;

  /* Thread entered */
  INFIDL_ASSERT(info_ptr->ev_status.event_status == EVENT_NULL);
  info_ptr->ev_status.event_status = EVENT_THREAD_STARTED;

  /* initialize status */
  status_ptr->c_char_size = 1;
  status_ptr->chunks_status = infidl_calloc(1, sizeof(char));

  int cols = tty_width() >= 0 ? tty_width() : 0;
  status_ptr->lines = num_of_lines(info_ptr, cols);

  /* event loop */
  events_init(&info_ptr->ev_status, status_update_cb, info_ptr, EVENT_STATUS);

  INFIDL_ASSERT(info_ptr->global_progress.initialized);

  if (info_ptr->session_status != SESSION_INTERRUPTED && exist_prg(info_ptr, PRG_MERGED, false)) {
    debug_msg(FN, "Start ev_status loop.");
    events_activate(&info_ptr->ev_status);
  }

  /* Event loop exited */
  events_deinit(&info_ptr->ev_status);

  /* finalize and cleanup */
  if (!info_ptr->params->no_status) {
    fprintf(stderr, "\n");
  }

  INFIDL_FREE(status_ptr->chunks_status);
  return info_ptr;
}

/* vim: set filetype=c ts=2 sw=2 et spell foldmethod=syntax: */
