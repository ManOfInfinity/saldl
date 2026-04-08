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

#include "infidl.h"
#include "resume.h"
#include "queue.h"
#include "exit.h"

info_s *info_global = NULL; /* Referenced in the signal handler */

void check_libcurl(curl_version_info_data *curl_info) {
  if (curl_info->version_num < 0x073700) { /* < 7.55 */
    fatal(FN, "Loaded libcurl version %s (>= 7.55 required)", curl_info->version);
  }
}

static void infidl_free_all(info_s *info_ptr) {
  infidl_params *params_ptr = info_ptr->params;
  remote_info_s *remote_info = &info_ptr->remote_info;
  remote_info_s *mirror_remote_info = &info_ptr->mirror_remote_info;

  /* Make valgrind happy */
  INFIDL_FREE(info_ptr->threads);
  INFIDL_FREE(info_ptr->chunks);

  infidl_custom_headers_free_all(params_ptr->custom_headers);
  infidl_custom_headers_free_all(params_ptr->proxy_custom_headers);

  INFIDL_FREE(remote_info->effective_url);
  INFIDL_FREE(remote_info->attachment_filename);
  INFIDL_FREE(remote_info->content_type);

  INFIDL_FREE(mirror_remote_info->effective_url);
  INFIDL_FREE(mirror_remote_info->attachment_filename);
  INFIDL_FREE(mirror_remote_info->content_type);

  INFIDL_FREE(params_ptr->start_url);
  INFIDL_FREE(params_ptr->root_dir);
  INFIDL_FREE(params_ptr->filename);
  INFIDL_FREE(params_ptr->referer);
  INFIDL_FREE(params_ptr->date_expr);
  INFIDL_FREE(params_ptr->since_file_mtime);
  INFIDL_FREE(params_ptr->cookie_file);
  INFIDL_FREE(params_ptr->inline_cookies);
  INFIDL_FREE(params_ptr->post);
  INFIDL_FREE(params_ptr->raw_post);
  INFIDL_FREE(params_ptr->user_agent);
  INFIDL_FREE(params_ptr->proxy);
  INFIDL_FREE(params_ptr->tunnel_proxy);
}

void infidl(infidl_params *params_ptr) {
  /* Definitions */
  info_s info = DEF_INFO_S;
  info.params = params_ptr;

  /* Handle signals */
  info_global = &info;
  infidl_handle_signals();

  /* Need to be set as early as possible */
  set_color(&params_ptr->no_color);
  set_verbosity(&params_ptr->verbosity, &params_ptr->libcurl_verbosity);

  /* Check if loaded libcurl is recent enough */
  info.curl_info = curl_version_info(CURLVERSION_NOW);
  check_libcurl(info.curl_info);

  /* Library initializations, should run only once */
  INFIDL_ASSERT(!curl_global_init(CURL_GLOBAL_ALL));
#ifdef _WIN32
  INFIDL_ASSERT(!evthread_use_windows_threads());
#else
  INFIDL_ASSERT(!evthread_use_pthreads());
#endif

  /* get/set initial info */
  if (params_ptr->show_details) main_msg("URL", "%s", params_ptr->start_url);
  check_url(params_ptr->start_url);
  get_info(&info);
  set_info(&info);
  check_remote_file_size(&info);

  /* initialize chunks early for extra_resume() */
  chunks_init(&info);

  if (params_ptr->resume) {
    check_resume(&info);
  }

  print_chunk_info(&info);
  global_progress_init(&info);

  /* if get-info, print the requested info and exit */
  if (params_ptr->get_file_name || params_ptr->get_file_size || params_ptr->get_effective_url) {
    if (params_ptr->get_file_name && params_ptr->filename) {
      fprintf(stdout, "%s\n", params_ptr->filename);
    }

    if (params_ptr->get_file_size) {
      fprintf(stdout, "%"SAL_JU"\n", (uintmax_t)info.file_size);
    }

    if (params_ptr->get_effective_url) {
      if (info.remote_info.effective_url) {
        fprintf(stdout, "%s\n", info.remote_info.effective_url);
      }
      else {
        fprintf(stdout, "%s\n", params_ptr->start_url);
      }
    }

    infidl_free_all(&info);
    finish_msg_and_exit("Getting info done.");
  }

  /* exit here if dry_run was set */
  if ( params_ptr->dry_run  ) {
    infidl_free_all(&info);
    finish_msg_and_exit("Dry-run done.");
  }

  check_files_and_dirs(&info);

  /* Check if download was interrupted after all data was merged */
  if (info.already_finished) {
    goto infidl_all_data_merged;
  }

  /* threads, needed by set_modes() */
  info.threads = infidl_calloc(params_ptr->num_connections, sizeof(thread_s));
  set_modes(&info);

  /* 1st iteration */
  for (size_t counter = 0; counter < params_ptr->num_connections; counter++) {
    queue_next_chunk(&info, counter, 1);
  }

  /* Create event pthreads */
  infidl_pthread_create(&info.trigger_events_pth, NULL, events_trigger_thread, &info);

  if (!params_ptr->read_only && !params_ptr->to_stdout) {
    infidl_pthread_create(&info.sync_ctrl_pth, NULL, sync_ctrl, &info);
  }

  if (info.chunk_count != 1) {
    infidl_pthread_create(&info.status_display_pth, NULL, status_display, &info);
    infidl_pthread_create(&info.queue_next_pth, NULL, queue_next_thread, &info);
    infidl_pthread_create(&info.merger_pth, NULL, merger_thread, &info);
  }

  /* Now that everything is initialized */
  info.session_status = SESSION_IN_PROGRESS;

  /* Avoid race in joining event threads if the session was interrupted, or finishing without downloading if single_mode */
  do {
    usleep(100000);
  } while (params_ptr->single_mode ? info.chunks[0].progress != PRG_FINISHED : info.global_progress.complete_size != info.file_size);

  /* Join event pthreads */
  if (!params_ptr->read_only && !params_ptr->to_stdout) {
    join_event_pth(&info.ev_ctrl ,&info.sync_ctrl_pth);
  }

  if (info.chunk_count !=1) {
    join_event_pth(&info.ev_status, &info.status_display_pth);
    join_event_pth(&info.ev_queue, &info.queue_next_pth);
    join_event_pth(&info.ev_merge, &info.merger_pth);
  }

  info.events_queue_done = true;
  event_queue(&info.ev_trigger, NULL);
  join_event_pth(&info.ev_trigger ,&info.trigger_events_pth);

infidl_all_data_merged:

  /* Remove tmp_dirname */
  if (!params_ptr->read_only && !params_ptr->mem_bufs && !params_ptr->single_mode) {
    if ( rmdir(info.tmp_dirname) ) {
      err_msg(FN, "Failed to delete %s: %s", info.tmp_dirname, strerror(errno) );
    }
  }

  /*** Final Steps ***/

  /* One last check  */
  if (info.file_size && !params_ptr->no_remote_info &&
      !params_ptr->read_only && !params_ptr->to_stdout &&
      (!info.remote_info.content_encoded || params_ptr->no_decompress)) {
    off_t saved_file_size = infidl_fsizeo(info.part_filename, info.file);
    if (saved_file_size != info.file_size) {
      pre_fatal(FN, "Unexpected saved file size (%"SAL_JU"!=%"SAL_JU").", saved_file_size, info.file_size);
      pre_fatal(FN, "This could happen if you're downloading from a dynamic site.");
      pre_fatal(FN, "If that's the case and the download is small, retry with --no-remote-info");
      fatal(FN, "If you think that's a bug in infidl, report it: %s", INFIDL_BUG);
    }
  }
  else {
    debug_msg(FN, "Strict check for finished file size skipped.");
  }

  if (!params_ptr->read_only && !params_ptr->to_stdout) {
    infidl_fclose(info.part_filename, info.file);
    if (rename(info.part_filename, params_ptr->filename) ) {
      err_msg(FN, "Failed to rename now-complete %s to %s: %s", info.part_filename, params_ptr->filename, strerror(errno));
    }

    infidl_fclose(info.ctrl_filename, info.ctrl_file);
    if ( remove(info.ctrl_filename) ) {
      err_msg(FN, "Failed to remove %s: %s", info.ctrl_filename, strerror(errno));
    }
  }

  /* cleanups */
  curl_cleanup(&info);
  infidl_free_all(&info);

  if (params_ptr->show_details) {
    finish_msg_and_exit("Download Finished.");
  }
  exit(EXIT_SUCCESS);
}

/* vim: set filetype=c ts=2 sw=2 et spell foldmethod=syntax: */
