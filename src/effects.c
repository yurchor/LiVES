// effects.c
// LiVES (lives-exe)
// (c) G. Finch <salsaman+lives@gmail.com> 2003 - 2020
// Released under the GPL 3 or later
// see file ../COPYING for licensing details

#include "main.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "effects.h"
#include "effects-weed.h"
#include "interface.h"
#include "paramwindow.h"
#include "cvirtual.h"
#include "resample.h"
#include "ce_thumbs.h"
#include "callbacks.h"

//////////// Effects ////////////////

#ifdef HAVE_YUV4MPEG
#include "lives-yuv4mpeg.h"
#endif

#include "rte_window.h"

static weed_plant_t *resize_instance = NULL;

static boolean apply_audio_fx;

///////////////////////////////////////////////////

// generic


char *lives_fx_cat_to_text(lives_fx_cat_t cat, boolean plural) {
  // return value should be free'd after use
  switch (cat) {
  // main categories
  case LIVES_FX_CAT_VIDEO_GENERATOR:
    if (!plural) return ((_("generator")));
    else return ((_("Generators")));
  case LIVES_FX_CAT_AUDIO_GENERATOR:
    if (!plural) return ((_("audio generator")));
    else return ((_("Audio Generators")));
  case LIVES_FX_CAT_AV_GENERATOR:
    if (!plural) return ((_("audio/video generator")));
    else return ((_("Audio/Video Generators")));
  case LIVES_FX_CAT_DATA_GENERATOR:
    if (!plural) return ((_("data generator")));
    else return ((_("Data Generators")));
  case LIVES_FX_CAT_DATA_VISUALISER:
    if (!plural) return ((_("data visualiser")));
    else return ((_("Data Visualisers")));
  case LIVES_FX_CAT_DATA_PROCESSOR:
    if (!plural) return ((_("data processor")));
    else return ((_("Data Processors")));
  case LIVES_FX_CAT_DATA_SOURCE:
    if (!plural) return ((_("data source")));
    else return ((_("Data Sources")));
  case LIVES_FX_CAT_TRANSITION:
    if (!plural) return ((_("transition")));
    else return ((_("Transitions")));
  case LIVES_FX_CAT_EFFECT:
    if (!plural) return ((_("effect")));
    else return ((_("Effects")));
  case LIVES_FX_CAT_UTILITY:
    if (!plural) return ((_("utility")));
    else return ((_("Utilities")));
  case LIVES_FX_CAT_COMPOSITOR:
    if (!plural) return ((_("compositor")));
    else return ((_("Compositors")));
  case LIVES_FX_CAT_TAP:
    if (!plural) return ((_("tap")));
    else return ((_("Taps")));
  case LIVES_FX_CAT_SPLITTER:
    if (!plural) return ((_("splitter")));
    else return ((_("Splitters")));
  case LIVES_FX_CAT_CONVERTER:
    if (!plural) return ((_("converter")));
    else return ((_("Converters")));
  case LIVES_FX_CAT_ANALYSER:
    if (!plural) return ((_("analyser")));
    else return ((_("Analysers")));

  // subcategories
  case LIVES_FX_CAT_AV_TRANSITION:
    if (!plural) return ((_("audio/video")));
    else return ((_("Audio/Video Transitions")));
  case LIVES_FX_CAT_VIDEO_TRANSITION:
    if (!plural) return ((_("video only")));
    else return ((_("Video only Transitions")));
  case LIVES_FX_CAT_AUDIO_TRANSITION:
    if (!plural) return ((_("audio only")));
    else return ((_("Audio only Transitions")));
  case LIVES_FX_CAT_AUDIO_MIXER:
    if (!plural) return ((_("audio")));
    else return ((_("Audio Mixers")));
  case LIVES_FX_CAT_AUDIO_EFFECT:
    if (!plural) return ((_("audio")));
    else return ((_("Audio Effects")));
  case LIVES_FX_CAT_VIDEO_EFFECT:
    if (!plural) return ((_("video")));
    else return ((_("Video Effects")));
  case LIVES_FX_CAT_AUDIO_VOL:
    if (!plural) return ((_("audio volume controller")));
    else return ((_("Audio Volume Controllers")));
  case LIVES_FX_CAT_VIDEO_ANALYSER:
    if (!plural) return ((_("video analyser")));
    else return ((_("Video Analysers")));
  case LIVES_FX_CAT_AUDIO_ANALYSER:
    if (!plural) return ((_("audio analyser")));
    else return ((_("Audio Analysers")));

  default:
    return ((_("unknown")));
  }
}


// Rendered effects

boolean do_effect(lives_rfx_t *rfx, boolean is_preview) {
  // apply a rendered effect to the current file

  // returns FALSE if the user cancelled
  // leave_info_file is set if a preview turned into actual processing: ie. no params were changed after the preview
  // preview generates .pre files instead of .mgk, so needs special post-processing

  int oundo_start = cfile->undo_start;
  int oundo_end = cfile->undo_end;
  char effectstring[128];
  double old_pb_fps = cfile->pb_fps;

  char *text;
  char *fxcommand = NULL, *cmd;
  int current_file = mainw->current_file;

  int new_file = current_file;
  int ldfile;

  boolean got_no_frames = FALSE;
  char *tmp;

  if (!CURRENT_CLIP_IS_VALID) return FALSE;

  if (rfx->num_in_channels == 0 && !is_preview) current_file = mainw->pre_src_file;

  if (is_preview) {
    // generators start at 1, even though they have no initial frames
    cfile->progress_start = cfile->undo_start = cfile->start;
    cfile->progress_end = cfile->undo_end = cfile->end;
  } else if (rfx->num_in_channels != 2) {
    cfile->progress_start = cfile->undo_start = cfile->start;
    cfile->progress_end = cfile->undo_end = cfile->end;
  }

  if (!mainw->internal_messaging && !mainw->keep_pre) {
    char *pdefault;
    char *plugin_name;

    if (rfx->status == RFX_STATUS_BUILTIN) plugin_name = lives_build_filename(prefs->lib_dir, PLUGIN_EXEC_DIR,
          PLUGIN_RENDERED_EFFECTS_BUILTIN, rfx->name, NULL);
    else plugin_name = lives_strdup(rfx->name);

    if (rfx->num_in_channels == 2) {
      // transition has a few extra bits
      pdefault = lives_strdup_printf("%s %d %d %d %d %d %s %f %s %d \"%s/%s\"", cfile->handle, rfx->status,
                                     cfile->progress_start, cfile->progress_end, cfile->hsize, cfile->vsize,
                                     get_image_ext_for_type(cfile->img_type), cfile->fps,
                                     get_image_ext_for_type(clipboard->img_type),
                                     clipboard->start, prefs->workdir, clipboard->handle);
    } else {
      pdefault = lives_strdup_printf("%s %d %d %d %d %d %s %f", cfile->handle, rfx->status, cfile->progress_start,
                                     cfile->progress_end, cfile->hsize, cfile->vsize, get_image_ext_for_type(cfile->img_type), cfile->fps);
    }
    // and append params
    if (is_preview) {
      cmd = lives_strdup("pfxrender");
      mainw->show_procd = FALSE;
    } else cmd = lives_strdup("fxrender");
    fxcommand = lives_strconcat(prefs->backend, " \"", cmd, "_", plugin_name, "\" ", pdefault,
                                (tmp = param_marshall(rfx, FALSE)), NULL);
    lives_free(plugin_name);
    lives_free(cmd);
    lives_free(pdefault);
    lives_free(tmp);
  }

  if (!mainw->keep_pre) lives_rm(cfile->info_file);

  if (!mainw->internal_messaging && !mainw->keep_pre) {
    if (cfile->frame_index_back != NULL) {
      lives_free(cfile->frame_index_back);
      cfile->frame_index_back = NULL;
    }
    lives_system(fxcommand, FALSE);
    lives_free(fxcommand);
  } else {
    if (mainw->num_tr_applied > 0 && mainw->blend_file > 0 && mainw->files[mainw->blend_file] != NULL &&
        mainw->files[mainw->blend_file]->clip_type != CLIP_TYPE_GENERATOR) {
      mainw->files[mainw->blend_file]->frameno = mainw->files[mainw->blend_file]->start - 1;
    }
  }
  mainw->effects_paused = FALSE;

  if (cfile->clip_type == CLIP_TYPE_FILE && rfx->status != RFX_STATUS_WEED) {
    // start decoding frames for the rendered effect plugins to start processing
    cfile->fx_frame_pump = cfile->start;
    if (!cfile->pumper) {
      cfile->pumper = lives_proc_thread_create(LIVES_THRDATTR_NONE, (lives_funcptr_t)virtual_to_images,
                      -1, "iiibV", mainw->current_file,
                      cfile->undo_start, cfile->undo_end, FALSE, NULL);
    }
  } else cfile->fx_frame_pump = 0;

  if (is_preview) {
    cfile->undo_start = oundo_start;
    cfile->undo_end = oundo_end;
    mainw->current_file = current_file;
    return TRUE;
  }

  if (rfx->props & RFX_PROPS_MAY_RESIZE) {
    tmp = (_("%s all frames..."));
    text = lives_strdup_printf(tmp, _(rfx->action_desc));
  } else {
    if (rfx->num_in_channels == 2) {
      tmp = (_("%s clipboard into frames %d to %d..."));
      text = lives_strdup_printf(tmp, _(rfx->action_desc), cfile->progress_start, cfile->progress_end);
    } else {
      if (rfx->num_in_channels == 0) {
        mainw->no_switch_dprint = TRUE;
        if (mainw->gen_to_clipboard) {
          tmp = (_("%s to clipboard..."));
          text = lives_strdup_printf(tmp, _(rfx->action_desc));
        } else {
          tmp = (_("%s to new clip..."));
          text = lives_strdup_printf(tmp, _(rfx->action_desc));
        }
      } else {
        tmp = (_("%s frames %d to %d..."));
        text = lives_strdup_printf(tmp, _(rfx->action_desc), cfile->start, cfile->end);
      }
    }
  }

  if (!mainw->no_switch_dprint) d_print(""); // force switch text
  ldfile = mainw->last_dprint_file;

  d_print(text);
  lives_free(text);
  lives_free(tmp);
  mainw->last_dprint_file = ldfile;

  cfile->redoable = cfile->undoable = FALSE;
  lives_widget_set_sensitive(mainw->redo, FALSE);
  lives_widget_set_sensitive(mainw->undo, FALSE);

  cfile->undo_action = UNDO_EFFECT;

  if (rfx->props & RFX_PROPS_MAY_RESIZE) {
    cfile->ohsize = cfile->hsize;
    cfile->ovsize = cfile->vsize;
    mainw->resizing = TRUE;
    cfile->nokeep = TRUE;
  }

  // 'play' as fast as we possibly can
  cfile->pb_fps = 1000000.;

  if (rfx->num_in_channels == 2) {
    tmp = (_("%s clipboard with selection"));
    lives_snprintf(effectstring, 128, tmp, _(rfx->action_desc));
  } else if (rfx->num_in_channels == 0) {
    if (mainw->gen_to_clipboard) {
      tmp = (_("%s to clipboard"));
      lives_snprintf(effectstring, 128, tmp, _(rfx->action_desc));
    } else {
      tmp = (_("%s to new clip"));
      lives_snprintf(effectstring, 128, tmp, _(rfx->action_desc));
    }
  } else {
    tmp = (_("%s frames %d to %d"));
    lives_snprintf(effectstring, 128, tmp, _(rfx->action_desc), cfile->undo_start, cfile->undo_end);
  }
  lives_free(tmp);

  if (rfx->props & RFX_PROPS_MAY_RESIZE || rfx->num_in_channels == 0) {
    if (rfx->status == RFX_STATUS_WEED) {
      // set out_channel dimensions for resizers / generators
      int error;
      weed_plant_t *first_out = get_enabled_channel((weed_plant_t *)rfx->source, 0, FALSE);
      weed_plant_t *first_ot = weed_get_plantptr_value(first_out, WEED_LEAF_TEMPLATE, &error);
      weed_set_int_value(first_out, WEED_LEAF_WIDTH, weed_get_int_value(first_ot, WEED_LEAF_HOST_WIDTH, &error));
      weed_set_int_value(first_out, WEED_LEAF_HEIGHT, weed_get_int_value(first_ot, WEED_LEAF_HOST_HEIGHT, &error));
    }
  }

  if (!do_progress_dialog(TRUE, TRUE, effectstring) || mainw->error) {
    if (cfile->pumper) {
      lives_nanosleep_until_nonzero(lives_proc_thread_cancel(cfile->pumper));
      weed_plant_free(cfile->pumper);
      cfile->pumper = NULL;
    }
    mainw->last_dprint_file = ldfile;
    mainw->show_procd = TRUE;
    mainw->keep_pre = FALSE;
    if (mainw->error) {
      widget_opts.non_modal = TRUE;
      if (mainw->cancelled != CANCEL_ERROR) do_error_dialog(mainw->msg);
      d_print_failed();
      mainw->last_dprint_file = ldfile;
    }
    widget_opts.non_modal = FALSE;
    if (mainw->cancelled != CANCEL_KEEP) {
      cfile->undo_start = oundo_start;
      cfile->undo_end = oundo_end;
    }
    cfile->pb_fps = old_pb_fps;
    mainw->internal_messaging = FALSE;
    mainw->resizing = FALSE;
    cfile->nokeep = FALSE;
    cfile->fx_frame_pump = 0;

    if (cfile->start == 0) {
      cfile->start = 1;
      cfile->end = cfile->frames;
    }

    if (rfx->num_in_channels == 0 && mainw->current_file != current_file) {
      mainw->suppress_dprint = TRUE;
      close_current_file(current_file);
      mainw->suppress_dprint = FALSE;
    } else {
      mainw->current_file = current_file;
      do_rfx_cleanup(rfx);
    }

    mainw->is_generating = FALSE;
    mainw->no_switch_dprint = FALSE;

    if (mainw->multitrack != NULL) {
      mainw->pre_src_file = -1;
    }

    return FALSE;
  }

  if (cfile->start == 0) {
    cfile->start = 1;
    cfile->end = cfile->frames;
  }

  do_rfx_cleanup(rfx);

  mainw->resizing = FALSE;
  cfile->nokeep = FALSE;
  cfile->fx_frame_pump = 0;

  if (!mainw->gen_to_clipboard) {
    lives_widget_set_sensitive(mainw->undo, TRUE);
    if (rfx->num_in_channels > 0) cfile->undoable = TRUE;
    cfile->pb_fps = old_pb_fps;
    mainw->internal_messaging = FALSE;
    if (rfx->num_in_channels > 0) lives_widget_set_sensitive(mainw->select_last, TRUE);
    if (rfx->num_in_channels > 0) set_undoable(rfx->menu_text, TRUE);
  }

  mainw->show_procd = TRUE;

  if (rfx->status != RFX_STATUS_WEED) {
    int numtok = get_token_count(mainw->msg, '|');
    if (numtok > 1) {
      char **array = lives_strsplit(mainw->msg, "|", numtok);
      // [0] is "completed"
      cfile->end = cfile->progress_end = cfile->start + atoi(array[4]) - 1;
      if (rfx->props & RFX_PROPS_MAY_RESIZE || rfx->num_in_channels == 0) {
        // get new frame size
        uint64_t verhash = make_version_hash(rfx->rfx_version);
        if (verhash >= 1008003) {
          cfile->hsize = atoi(array[1]);
          cfile->vsize = atoi(array[2]);
        } else {
          cfile->hsize = atoi(array[5]);
          cfile->vsize = atoi(array[6]);
        }
        if (rfx->num_in_channels == 0) {
          cfile->fps = cfile->pb_fps = strtod(array[3], NULL);
          if (cfile->fps == 0.) cfile->fps = cfile->pb_fps = prefs->default_fps;
          cfile->end = cfile->frames = atoi(array[4]);
          cfile->bpp = cfile->img_type == IMG_TYPE_JPEG ? 24 : 32;
        }
      }
      lives_strfreev(array);
    }
    if (rfx->num_in_channels == 0) {
      cfile->progress_start = 1;
      cfile->progress_end = cfile->frames;
    }
  } else {
    int error;
    weed_plant_t *first_out = get_enabled_channel((weed_plant_t *)rfx->source, 0, FALSE);
    weed_plant_t *first_ot = weed_get_plantptr_value(first_out, WEED_LEAF_TEMPLATE, &error);
    cfile->hsize = weed_get_int_value(first_ot, WEED_LEAF_HOST_WIDTH, &error);
    cfile->vsize = weed_get_int_value(first_ot, WEED_LEAF_HOST_HEIGHT, &error);
  }

  if (rfx->num_in_channels > 0) {
    if (cfile->hsize == cfile->ohsize && cfile->vsize == cfile->ovsize) cfile->undo_action = UNDO_EFFECT;
    else {
      boolean bad_header = FALSE;
      if (!save_clip_value(mainw->current_file, CLIP_DETAILS_WIDTH, &cfile->hsize)) bad_header = TRUE;
      if (!save_clip_value(mainw->current_file, CLIP_DETAILS_HEIGHT, &cfile->vsize)) bad_header = TRUE;
      cfile->undo_action = UNDO_RESIZABLE;
      if (bad_header) do_header_write_error(mainw->current_file);
    }
  }

  mainw->cancelled = CANCEL_NONE;
  mainw->error = FALSE;

  if (mainw->keep_pre) {
    // this comes from a preview which then turned into processing
    // the processing is the same as usual, except we use a different file extension (.pre instead of .mgk)
    char *com = lives_strdup_printf("%s mv_pre \"%s\" %d %d \"%s\"", prefs->backend_sync, cfile->handle, cfile->progress_start,
                                    cfile->progress_end, get_image_ext_for_type(cfile->img_type));

    lives_rm(cfile->info_file);
    mainw->cancelled = CANCEL_NONE;
    lives_system(com, FALSE);
    lives_free(com);
    mainw->keep_pre = FALSE;

    check_backend_return(cfile);

    if (mainw->error) {
      if (!mainw->cancelled) {
        widget_opts.non_modal = TRUE;
        do_info_dialog(_("\nNo frames were generated.\n"));
        widget_opts.non_modal = FALSE;
        d_print_failed();
      } else if (mainw->cancelled != CANCEL_ERROR) d_print_cancelled();
      else d_print_failed();

      if (rfx->num_in_channels == 0) {
        mainw->is_generating = FALSE;

        if (mainw->current_file != current_file) {
          mainw->suppress_dprint = TRUE;
          close_current_file(current_file);
          mainw->suppress_dprint = FALSE;
        }

        mainw->current_file = current_file;
        mainw->last_dprint_file = ldfile;

        if (mainw->multitrack != NULL) {
          mainw->current_file = mainw->multitrack->render_file;
        }
      }
      mainw->no_switch_dprint = FALSE;
      return FALSE;
    }
  }

  if (rfx->num_in_channels == 0) {
    if (rfx->props & RFX_PROPS_BATCHG) {
      // batch mode generators need some extra processing
      char *imgdir = lives_build_path(prefs->workdir, cfile->handle, NULL);
      int img_file = mainw->current_file;

      mainw->suppress_dprint = TRUE;
      open_file_sel(imgdir, 0, 0);
      lives_free(imgdir);
      new_file = mainw->current_file;

      if (new_file != img_file) {
        mainw->current_file = img_file;

        lives_snprintf(mainw->files[new_file]->name, CLIP_NAME_MAXLEN, "%s", cfile->name);
        lives_snprintf(mainw->files[new_file]->file_name, PATH_MAX, "%s", cfile->file_name);

        lives_menu_item_set_text(mainw->files[new_file]->menuentry, cfile->name, FALSE);

        mainw->files[new_file]->fps = mainw->files[new_file]->pb_fps = cfile->fps;
      } else got_no_frames = TRUE;

      close_current_file(current_file);
      mainw->suppress_dprint = FALSE;

      if (!got_no_frames) mainw->current_file = new_file;
    } else {
      // TODO - use check_clip_intergity()
      char *tfile = make_image_file_name(cfile, cfile->frames, get_image_ext_for_type(cfile->img_type));

      if (!lives_file_test(tfile, LIVES_FILE_TEST_EXISTS)) {
        cfile->frames = get_frame_count(mainw->current_file, 1);
        cfile->end = cfile->frames;
      }
      lives_free(tfile);
    }

    if (got_no_frames || cfile->frames <= 0) {
      mainw->is_generating = FALSE;
      if (!mainw->cancelled) {
        widget_opts.non_modal = TRUE;
        do_info_dialog(_("\nNo frames were generated.\n"));
        widget_opts.non_modal = FALSE;
        d_print_failed();
      } else d_print_cancelled();
      if (!got_no_frames) {
        mainw->suppress_dprint = TRUE;
        close_current_file(current_file);
        mainw->suppress_dprint = FALSE;
      }
      mainw->last_dprint_file = ldfile;
      mainw->no_switch_dprint = FALSE;
      if (mainw->multitrack != NULL) mainw->current_file = mainw->multitrack->render_file;
      return FALSE;
    }

    if (mainw->gen_to_clipboard) {
      // here we will copy all values to the clipboard, including the handle
      // then close the current file without deleting the frames

      init_clipboard();

      lives_memcpy(clipboard, cfile, sizeof(lives_clip_t));
      cfile->is_loaded = TRUE;
      mainw->suppress_dprint = TRUE;
      mainw->close_keep_frames = TRUE;

      close_current_file(current_file);

      mainw->suppress_dprint = FALSE;
      mainw->close_keep_frames = FALSE;

      new_file = current_file;

      mainw->untitled_number--;
    } else {
      if (!(rfx->props & RFX_PROPS_BATCHG)) {
        // gen to new file
        cfile->is_loaded = TRUE;
        add_to_clipmenu();
        if (!save_clip_values(new_file)) {
          close_current_file(current_file);
          return FALSE;
        }

        if (prefs->crash_recovery) add_to_recovery_file(cfile->handle);

        if (mainw->multitrack != NULL) {
          mt_init_clips(mainw->multitrack, mainw->current_file, TRUE);
          mt_clip_select(mainw->multitrack, TRUE);
        }

      }
      lives_notify(LIVES_OSC_NOTIFY_CLIP_OPENED, "");
    }
    mainw->is_generating = FALSE;
  }

  if (!mainw->gen_to_clipboard) cfile->changed = TRUE;
  if (mainw->multitrack == NULL) {
    if (new_file != -1) {
      lives_sync(1);
      switch_clip(1, new_file, TRUE);
    }
  } else {
    mainw->current_file = mainw->multitrack->render_file;
    mainw->pre_src_file = -1;
  }

  d_print_done();
  mainw->no_switch_dprint = FALSE;
  mainw->gen_to_clipboard = FALSE;
  mainw->last_dprint_file = ldfile;

  return TRUE;
}


// realtime fx

lives_render_error_t realfx_progress(boolean reset) {
  static lives_render_error_t write_error;

  LiVESError *error = NULL;

  char oname[PATH_MAX];

  LiVESPixbuf *pixbuf;

  ticks_t frameticks;

  weed_layer_t *layer;

  char *com, *tmp;

  static int i;

  int weed_error;
  int layer_palette;
  int retval;

  // this is called periodically from do_processing_dialog for internal effects

  if (reset) {
    i = cfile->start;
    clear_mainw_msg();

    if (cfile->clip_type == CLIP_TYPE_FILE) {
      if (cfile->frame_index_back != NULL) lives_free(cfile->frame_index_back);
      cfile->frame_index_back = frame_index_copy(cfile->frame_index, cfile->frames, 0);
    }
    write_error = LIVES_RENDER_ERROR_NONE;
    return LIVES_RENDER_READY;
  }

  if (mainw->effects_paused) return LIVES_RENDER_EFFECTS_PAUSED;

  // sig_progress...
  lives_snprintf(mainw->msg, MAINW_MSG_SIZE, "%d", i);
  // load, effect, save frame

  // skip resizing virtual frames
  if (resize_instance != NULL && is_virtual_frame(mainw->current_file, i)) {
    if (++i > cfile->end) {
      mainw->internal_messaging = FALSE;
      lives_snprintf(mainw->msg, 9, "completed");
      return LIVES_RENDER_COMPLETE;
    }
    return LIVES_RENDER_PROCESSING;
  }

  if (has_video_filters(FALSE) || resize_instance != NULL) {
    frameticks = (i - cfile->start + 1.) / cfile->fps * TICKS_PER_SECOND;
    THREADVAR(rowstride_alignment_hint) = 4;
    layer = lives_layer_new_for_frame(mainw->current_file, i);
    if (!pull_frame(layer, get_image_ext_for_type(cfile->img_type), frameticks)) {
      // do_read_failed_error_s() cannot be used here as we dont know the filename
      lives_snprintf(mainw->msg, MAINW_MSG_SIZE, "error|missing image %d", i);
      return LIVES_RENDER_WARNING_READ_FRAME;
    }

    layer = on_rte_apply(layer, 0, 0, (weed_timecode_t)frameticks);

    if (!has_video_filters(TRUE) || resize_instance != NULL) {
      layer_palette = weed_get_int_value(layer, WEED_LEAF_CURRENT_PALETTE, &weed_error);

      if (resize_instance == NULL) resize_layer(layer, cfile->hsize, cfile->vsize, LIVES_INTERP_BEST, layer_palette, 0);
      if (cfile->img_type == IMG_TYPE_JPEG && layer_palette != WEED_PALETTE_RGB24 && layer_palette != WEED_PALETTE_RGBA32) {
        convert_layer_palette(layer, WEED_PALETTE_RGB24, 0);
        layer_palette = WEED_PALETTE_RGB24;
      } else if (cfile->img_type == IMG_TYPE_PNG && layer_palette != WEED_PALETTE_RGBA32) {
        convert_layer_palette(layer, WEED_PALETTE_RGBA32, 0);
        layer_palette = WEED_PALETTE_RGBA32;
      }

      pixbuf = layer_to_pixbuf(layer, TRUE, FALSE);
      weed_plant_free(layer);

      tmp = make_image_file_name(cfile, i, LIVES_FILE_EXT_MGK);
      lives_snprintf(oname, PATH_MAX, "%s", tmp);
      lives_free(tmp);

      do {
        retval = 0;
        lives_pixbuf_save(pixbuf, oname, cfile->img_type, 100, cfile->hsize, cfile->vsize, &error);

        if (error != NULL) {
          retval = do_write_failed_error_s_with_retry(oname, error->message);
          lives_error_free(error);
          error = NULL;
          if (retval != LIVES_RESPONSE_RETRY) write_error = LIVES_RENDER_ERROR_WRITE_FRAME;
        }
      } while (retval == LIVES_RESPONSE_RETRY);

      lives_widget_object_unref(pixbuf);

      if (cfile->clip_type == CLIP_TYPE_FILE) {
        cfile->frame_index[i - 1] = -1;
      }
    } else weed_plant_free(layer);
  }
  if (apply_audio_fx) {
    if (!apply_rte_audio((double)cfile->arate / (double)cfile->fps + (double)rand() / .5 / (double)(RAND_MAX))) {
      return LIVES_RENDER_ERROR_WRITE_AUDIO;
    }
  }

  if (++i > cfile->end) {
    if (resize_instance != NULL || (has_video_filters(FALSE) && !has_video_filters(TRUE))) {
      mainw->error = FALSE;
      mainw->cancelled = CANCEL_NONE;
      com = lives_strdup_printf("%s mv_mgk \"%s\" %d %d \"%s\"", prefs->backend, cfile->handle, cfile->start,
                                cfile->end, get_image_ext_for_type(cfile->img_type));
      lives_system(com, FALSE);
      lives_free(com);
      mainw->internal_messaging = FALSE;

      check_backend_return(cfile);

      if (mainw->error) write_error = LIVES_RENDER_ERROR_WRITE_FRAME;
      //cfile->may_be_damaged=TRUE;
      else {
        if (cfile->clip_type == CLIP_TYPE_FILE) {
          if (!check_if_non_virtual(mainw->current_file, 1, cfile->frames)) save_frame_index(mainw->current_file);
        }
        return LIVES_RENDER_COMPLETE;
      }
    } else {
      sprintf(mainw->msg, "%s", "completed");
      return LIVES_RENDER_COMPLETE;
    }
  }
  if (write_error) return write_error;
  return LIVES_RENDER_PROCESSING;
}


boolean on_realfx_activate_inner(int type, lives_rfx_t *rfx) {
  // type can be 0 - apply current realtime effects
  // 1 - resize (using weed filter)
  boolean retval;

  boolean has_new_audio = FALSE;

  apply_audio_fx = FALSE;

  if (type == 0 && ((cfile->achans > 0 && prefs->audio_src == AUDIO_SRC_INT && has_audio_filters(AF_TYPE_ANY)) ||
                    mainw->agen_key != 0)) {
    if (mainw->agen_key != 0 && cfile->achans == 0) {
      // apply audio gen to clip with no audio - prompt for audio settings
      resaudw = create_resaudw(2, NULL, NULL);
      /* lives_widget_context_update(); */
      /* lives_xwindow_raise(lives_widget_get_xwindow(resaudw->dialog)); */

      if (lives_dialog_run(LIVES_DIALOG(resaudw->dialog)) != LIVES_RESPONSE_OK) return FALSE;
      if (mainw->error) {
        mainw->error = FALSE;
        return FALSE;
      }
      has_new_audio = TRUE;
    }
    apply_audio_fx = TRUE;
    if (!apply_rte_audio_init()) return FALSE;

  }

  if (type == 1) resize_instance = (weed_plant_t *)rfx->source;
  else resize_instance = NULL;

  mainw->internal_messaging = TRUE;

  mainw->progress_fn = &realfx_progress;
  mainw->progress_fn(TRUE);

  weed_reinit_all();

  retval = do_effect(rfx, FALSE);

  if (apply_audio_fx) {
    apply_rte_audio_end(!retval);

    if (retval) {
      if (!has_video_filters(FALSE) || !has_video_filters(TRUE)) cfile->undo_action = UNDO_NEW_AUDIO;

      cfile->undo_achans = cfile->achans;
      cfile->undo_arate = cfile->arate;
      cfile->undo_arps = cfile->arps;
      cfile->undo_asampsize = cfile->asampsize;
      cfile->undo_signed_endian = cfile->signed_endian;

    } else {
      if (has_new_audio) cfile->achans = cfile->asampsize = cfile->arate = cfile->arps = 0;
      else {
        char *com = lives_strdup_printf("%s undo_audio %s", prefs->backend_sync, cfile->handle);
        lives_rm(cfile->info_file);
        lives_system(com, FALSE);
        lives_free(com);
      }
    }
    reget_afilesize(mainw->current_file);
  }

  mainw->internal_messaging = FALSE;
  resize_instance = NULL;
  return retval;
}


void on_realfx_activate(LiVESMenuItem *menuitem, livespointer xrfx) {
  lives_rfx_t *rfx = (lives_rfx_t *)xrfx;
  uint32_t chk_mask = 0;
  int type = 1;

  // type can be 0 - apply current realtime effects
  // 1 - resize (using weed filter) [menuitem == NULL]


  if (menuitem != NULL) {
    int i;
    for (i = 0; i < FX_KEYS_MAX_VIRTUAL; i++) {
      if (rte_key_valid(i + 1, TRUE)) {
        if (rte_key_is_enabled(1 + i)) {
          weed_plant_t *filter = rte_keymode_get_filter(i + 1, rte_key_getmode(i + 1));
          if (is_pure_audio(filter, TRUE)) {
            chk_mask |= WARN_MASK_LAYOUT_ALTER_AUDIO;
          } else chk_mask = WARN_MASK_LAYOUT_ALTER_FRAMES;
        }
      }
    }
    if (chk_mask > 0) {
      if (!check_for_layout_errors(NULL, mainw->current_file, cfile->start, cfile->end, &chk_mask)) {
        return;
      }
    }
    type = 0;
  }

  if (!on_realfx_activate_inner(type, rfx)) {
    unbuffer_lmap_errors(FALSE);
    return;
  }
  if (chk_mask != 0) popup_lmap_errors(NULL, LIVES_INT_TO_POINTER(chk_mask));
}


weed_plant_t *on_rte_apply(weed_layer_t *layer, int opwidth, int opheight, weed_timecode_t tc) {
  // apply realtime effects to a layer
  // mainw->filter_map is used as a guide
  // mainw->pchains holds the parameter values for interpolation
  // creates a temporary mix layer from mainw->blend_file (correcting its value if necessary)

  // returns the effected layer

  weed_plant_t **layers, *retlayer;
  int i;

  if (mainw->foreign) return NULL;

  layers = (weed_plant_t **)lives_malloc(3 * sizeof(weed_plant_t *));
  layers[2] = NULL;
  layers[0] = layer;

  if (mainw->blend_file > -1 && mainw->num_tr_applied > 0 && (mainw->files[mainw->blend_file] == NULL ||
      (mainw->files[mainw->blend_file]->clip_type == CLIP_TYPE_DISK &&
       (!mainw->files[mainw->blend_file]->frames ||
        !mainw->files[mainw->blend_file]->is_loaded)))) {
    // invalid blend file
    mainw->blend_file = mainw->current_file;
  }

  if (mainw->num_tr_applied && mainw->blend_file != mainw->current_file &&
      IS_VALID_CLIP(mainw->blend_file) && resize_instance == NULL) {
    layers[1] = get_blend_layer(tc);
  } else layers[1] = NULL;

  if (resize_instance != NULL) {
    lives_filter_error_t ret;
    weed_plant_t *init_event = weed_plant_new(WEED_PLANT_EVENT);
    weed_set_int_value(init_event, WEED_LEAF_IN_TRACKS, 0);
    weed_set_int_value(init_event, WEED_LEAF_OUT_TRACKS, 0);

    (void)(ret = weed_apply_instance(resize_instance, init_event, layers, 0, 0, tc));

    retlayer = layers[0];
    weed_plant_free(init_event);
  } else {
    retlayer = weed_apply_effects(layers, mainw->filter_map, tc, opwidth, opheight, mainw->pchains);
  }

  // all our pixel_data will have been free'd already
  for (i = 0; layers[i] != NULL; i++) {
    if (layers[i] != retlayer) weed_layer_free(layers[i]);
  }
  lives_free(layers);
  return retlayer;
}


void deinterlace_frame(weed_layer_t *layer, weed_timecode_t tc) {
  weed_plant_t **layers;

  weed_plant_t *deint_filter, *deint_instance, *next_inst, *init_event, *orig_instance;

  int deint_idx, error;

  if (mainw->fx_candidates[FX_CANDIDATE_DEINTERLACE].delegate == -1) return;

  deint_idx = LIVES_POINTER_TO_INT(lives_list_nth_data(mainw->fx_candidates[FX_CANDIDATE_DEINTERLACE].list,
                                   mainw->fx_candidates[FX_CANDIDATE_DEINTERLACE].delegate));

  deint_filter = get_weed_filter(deint_idx);

  orig_instance = deint_instance = weed_instance_from_filter(deint_filter);

  layers = (weed_plant_t **)lives_malloc(2 * sizeof(weed_plant_t *));

  layers[1] = NULL;

  layers[0] = layer;

  init_event = weed_plant_new(WEED_PLANT_EVENT);
  weed_set_int_value(init_event, WEED_LEAF_IN_TRACKS, 0);
  weed_set_int_value(init_event, WEED_LEAF_OUT_TRACKS, 0);

deint1:

  weed_apply_instance(deint_instance, init_event, layers, 0, 0, tc);

  if (weed_plant_has_leaf(deint_instance, WEED_LEAF_HOST_NEXT_INSTANCE)) next_inst = weed_get_plantptr_value(deint_instance,
        WEED_LEAF_HOST_NEXT_INSTANCE, &error);
  else next_inst = NULL;

  weed_call_deinit_func(deint_instance);
  weed_instance_unref(deint_instance);

  if (next_inst != NULL) {
    deint_instance = next_inst;
    goto deint1;
  }

  weed_instance_unref(orig_instance);

  weed_plant_free(init_event);

  lives_free(layers);
}


weed_plant_t *get_blend_layer(weed_timecode_t tc) {
  static weed_timecode_t blend_tc = 0;
  lives_clip_t *blend_file;
  weed_timecode_t ntc = tc;

  if (mainw->blend_file == -1 || mainw->files[mainw->blend_file] == NULL) return NULL;
  blend_file = mainw->files[mainw->blend_file];

  if (mainw->blend_file != mainw->last_blend_file) {
    // mainw->last_blend_file is set to -1 on playback start
    mainw->last_blend_file = mainw->blend_file;
    blend_file->last_frameno = blend_file->frameno;
    blend_tc = tc;
  }

  if (!cfile->play_paused) {
    blend_file->frameno = calc_new_playback_position(mainw->blend_file, blend_tc, (ticks_t *)&ntc);
    blend_file->last_frameno = blend_file->frameno;
    blend_tc = ntc;
  }

  /* if (is_virtual_frame(mainw->blend_file, blend_file->frameno)) */
  /*   mainw->blend_layer = lives_layer_new_for_frame(mainw->blend_file, blend_file->frame_index[blend_file->frameno]); */
  /* else */
  mainw->blend_layer = lives_layer_new_for_frame(mainw->blend_file, blend_file->frameno);
  pull_frame_threaded(mainw->blend_layer, get_image_ext_for_type(blend_file->img_type), blend_tc, 0, 0);
  return mainw->blend_layer;
}

////////////////////////////////////////////////////////////////////
// keypresses
// TODO - we should mutex lock mainw->rte

boolean rte_on_off_callback(LiVESAccelGroup *group, LiVESWidgetObject *obj, uint32_t keyval, LiVESXModifierType mod,
                            livespointer user_data) {
  // this is the callback which happens when a rte is keyed
  // key is 1 based, but if < 0 then this indicates auto mode (set via data connection)
  // in automode we don't add the effect parameters in ce_thumbs mode
  // if non-automode, the user overrides effect toggling

  int key = LIVES_POINTER_TO_INT(user_data);
  uint64_t new_rte;

  if (mainw->go_away) return TRUE;
  if (!LIVES_IS_INTERACTIVE && group != NULL) return TRUE;

  mainw->fx_is_auto = FALSE;

  mainw->osc_block = TRUE;

  if (key < 0) {
    mainw->fx_is_auto = TRUE;
    key = -key;
  }

  if (key == EFFECT_NONE) {
    // switch up/down keys to default (fps change)
    weed_deinit_all(FALSE);
  } else {
    // the idea here is this gets set if a generator starts play, because in weed_init_effect() we will run playback
    // and then we come out of there and do not wish to set the key on
    key--;
    new_rte = GU641 << (key);
    mainw->gen_started_play = FALSE;

    if (!(mainw->rte & new_rte)) {
      // switch is ON
      // WARNING - if we start playing because a generator was started, we block here
      filter_mutex_lock(key);
      //if (!LIVES_IS_PLAYING) {
      if (!(weed_init_effect(key))) {
        // ran out of instance slots, no effect assigned, or some other error
        // or gen started playback and then stopped
        pthread_mutex_lock(&mainw->event_list_mutex);
        if (mainw->rte & new_rte) mainw->rte ^= new_rte;
        pthread_mutex_unlock(&mainw->event_list_mutex);
        if (rte_window != NULL) rtew_set_keych(key, FALSE);
        if (mainw->ce_thumbs) ce_thumbs_set_keych(key, FALSE);
        mainw->osc_block = FALSE;
        filter_mutex_unlock(key);
        return TRUE;
      }

      if (!mainw->gen_started_play) {
        pthread_mutex_lock(&mainw->event_list_mutex);
        if (!(mainw->rte & new_rte)) mainw->rte |= new_rte;
        pthread_mutex_unlock(&mainw->event_list_mutex);

        mainw->last_grabbable_effect = key;
        if (rte_window != NULL) rtew_set_keych(key, TRUE);
        if (mainw->ce_thumbs) {
          ce_thumbs_set_keych(key, TRUE);

          // if effect was auto (from ACTIVATE data connection), leave all param boxes
          // otherwise, remove any which are not "pinned"
          if (!mainw->fx_is_auto) ce_thumbs_add_param_box(key, !mainw->fx_is_auto);
        }
      }
      filter_mutex_unlock(key);
    } else {
      // effect is OFF
      filter_mutex_lock(key);
      if (weed_deinit_effect(key)) {
        pthread_mutex_lock(&mainw->event_list_mutex);
        if (mainw->rte & new_rte) mainw->rte ^= new_rte;
        pthread_mutex_unlock(&mainw->event_list_mutex);
      }
      filter_mutex_unlock(key);
      if (rte_window != NULL) rtew_set_keych(key, FALSE);
      if (mainw->ce_thumbs) ce_thumbs_set_keych(key, FALSE);
    }
  }

  mainw->osc_block = FALSE;

  if (mainw->rendered_fx) {
    if (mainw->rendered_fx[0].menuitem && LIVES_IS_WIDGET(mainw->rendered_fx[0].menuitem)) {
      if (!LIVES_IS_PLAYING
          && mainw->current_file > 0 && ((has_video_filters(FALSE) && !has_video_filters(TRUE))
                                         || (cfile->achans > 0 && prefs->audio_src == AUDIO_SRC_INT
                                             && has_audio_filters(AF_TYPE_ANY)) ||
                                         mainw->agen_key != 0)) {
        lives_widget_set_sensitive(mainw->rendered_fx[0].menuitem, TRUE);
      } else lives_widget_set_sensitive(mainw->rendered_fx[0].menuitem, FALSE);
    }
  }

  if (key > 0 && !mainw->fx_is_auto) {
    // user override any ACTIVATE data connection
    override_if_active_input(key);

    // if this is an outlet for ACTIVATE, disable the override now
    end_override_if_activate_output(key);
  }

  if (LIVES_IS_PLAYING && CURRENT_CLIP_IS_VALID && cfile->play_paused) {
    mainw->force_show = TRUE;
  }

  mainw->fx_is_auto = FALSE;
  return TRUE;
}


boolean rte_on_off_callback_hook(LiVESToggleButton *button, livespointer user_data) {
  rte_on_off_callback(NULL, NULL, 0, (LiVESXModifierType)0, user_data);
  return TRUE;
}


boolean grabkeys_callback(LiVESAccelGroup *group, LiVESWidgetObject *obj, uint32_t keyval, LiVESXModifierType mod,
                          livespointer user_data) {
  // assign the keys to the last key-grabbable effect
  int fx = LIVES_POINTER_TO_INT(user_data);
  if (fx != -1) {
    mainw->last_grabbable_effect = fx;
  }
  mainw->rte_keys = mainw->last_grabbable_effect;
  mainw->osc_block = TRUE;
  if (rte_window != NULL) {
    if (group != NULL) rtew_set_keygr(mainw->rte_keys);
  }
  if (mainw->rte_keys == -1) {
    mainw->osc_block = FALSE;
    return TRUE;
  }
  filter_mutex_lock(mainw->rte_keys);
  mainw->blend_factor = weed_get_blend_factor(mainw->rte_keys);
  filter_mutex_unlock(mainw->rte_keys);
  mainw->osc_block = FALSE;
  return TRUE;
}


boolean textparm_callback(LiVESAccelGroup *group, LiVESWidgetObject *obj, uint32_t keyval, LiVESXModifierType mod,
                          livespointer user_data) {
  // keyboard linked to first string parameter, until TAB is pressed
  mainw->rte_textparm = get_textparm();
  return TRUE;
}


boolean grabkeys_callback_hook(LiVESToggleButton *button, livespointer user_data) {
  if (!lives_toggle_button_get_active(button)) return TRUE;
  grabkeys_callback(NULL, NULL, 0, (LiVESXModifierType)0, user_data);
  return TRUE;
}


boolean rtemode_callback(LiVESAccelGroup *group, LiVESWidgetObject *obj, uint32_t keyval, LiVESXModifierType mod,
                         livespointer user_data) {
  int dirn = LIVES_POINTER_TO_INT(user_data);
  // "m" mode key
  if (mainw->rte_keys == -1) return TRUE;
  rte_key_setmode(0, dirn == PREV_MODE_CYCLE ? -2 : -1);
  filter_mutex_lock(mainw->rte_keys);
  mainw->blend_factor = weed_get_blend_factor(mainw->rte_keys);
  filter_mutex_unlock(mainw->rte_keys);
  return TRUE;
}


boolean rtemode_callback_hook(LiVESToggleButton *button, livespointer user_data) {
  int key_mode = LIVES_POINTER_TO_INT(user_data);
  int modes = rte_getmodespk();
  int key = (int)(key_mode / modes);
  int mode = key_mode - key * modes;

  if (!lives_toggle_button_get_active(button)) return TRUE;

  rte_key_setmode(key + 1, mode);
  return TRUE;
}


boolean swap_fg_bg_callback(LiVESAccelGroup *group, LiVESWidgetObject *obj, uint32_t keyval, LiVESXModifierType mod,
                            livespointer user_data) {
  int blend_file = mainw->blend_file;

  if (mainw->playing_file < 1 || mainw->num_tr_applied == 0 || mainw->blend_file == -1 ||
      mainw->blend_file == mainw->current_file || mainw->files[mainw->blend_file] == NULL || mainw->preview ||
      (mainw->is_processing && cfile->is_loaded)) {
    return TRUE;
  }

  if (mainw->swapped_clip == -1) {
    // this is to avoid an annoying situation in VJ playback, where the cliplist postion
    // can keep getting reset each time we swap the fg and bg
    if (CURRENT_CLIP_IS_NORMAL)
      mainw->swapped_clip = mainw->current_file;
    else mainw->swapped_clip = mainw->pre_src_file;
  } else mainw->swapped_clip = -1;

  //rte_swap_fg_bg();

  mainw->new_clip = blend_file;
  //do_quick_switch(blend_file); // will set mainw->blend_file

  if (mainw->ce_thumbs && (mainw->active_sa_clips == SCREEN_AREA_BACKGROUND
                           || mainw->active_sa_clips == SCREEN_AREA_FOREGROUND))
    ce_thumbs_highlight_current_clip();

  return TRUE;

  // **TODO - for weed, invert all transition parameters for any active effects
}

//////////////////////////////////////////////////////////////


LIVES_GLOBAL_INLINE boolean rte_key_is_enabled(int key) {
  // key starts at 1
  return !((mainw->rte & (GU641 << --key)) == 0ll);
}


LIVES_GLOBAL_INLINE int rte_getmodespk(void) {
  return prefs->max_modes_per_key;
}


LIVES_GLOBAL_INLINE boolean rte_key_toggle(int key) {
  // key is 1 based, or < 0 for auto mode
  rte_on_off_callback(NULL, NULL, 0, (LiVESXModifierType)0, LIVES_INT_TO_POINTER(key));

  return rte_key_is_enabled(key);
}


boolean rte_key_on_off(int key, boolean on) {
  // key is 1 based
  // returns the state of the key afterwards
  uint64_t new_rte;
  if (key < 1 || key >= FX_KEYS_MAX_VIRTUAL) return FALSE;
  key--;
  new_rte = GU641 << (key);
  if (mainw->rte & new_rte) {
    if (on) return TRUE;
  } else if (!on) return FALSE;
  key++;
  rte_on_off_callback(NULL, NULL, 0, (LiVESXModifierType)0, LIVES_INT_TO_POINTER(key));
  return (mainw->rte & new_rte);
}


LIVES_GLOBAL_INLINE void rte_keys_reset(void) {
  // switch off all realtime effects
  rte_on_off_callback(NULL, NULL, 0, (LiVESXModifierType)0, LIVES_INT_TO_POINTER(EFFECT_NONE));
}


static int backup_key_modes[FX_KEYS_MAX_VIRTUAL];
static uint64_t backup_rte = 0;

void rte_keymodes_backup(int nkeys) {
  // backup the current key/mode state
  int i;

  backup_rte = mainw->rte;

  for (i = 0; i < nkeys; i++) {
    backup_key_modes[i] = rte_key_getmode(i + 1);
  }
}


void rte_keymodes_restore(int nkeys) {
  int i;

  rte_keys_reset();
  mainw->rte = backup_rte;

  for (i = 0; i < nkeys; i++) {
    // set the mode
    rte_key_setmode(i + 1, backup_key_modes[i]);
    // activate the key
    if ((mainw->rte & GU641 << (i)) != 0) rte_key_toggle(i + 1);
  }
  if (mainw->rte_keys != -1) {
    filter_mutex_lock(mainw->rte_keys);
    mainw->blend_factor = weed_get_blend_factor(mainw->rte_keys);
    filter_mutex_unlock(mainw->rte_keys);
  }
}
