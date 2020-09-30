// xeffect.c
// livido plugin
// (c) G. Finch (salsaman) 2005
//
// released under the GNU GPL 3 or later
// see file COPYING or www.gnu.org for details

///////////////////////////////////////////////////////////////////

static int package_version = 1; // version of this package

//////////////////////////////////////////////////////////////////

#define NEED_PALETTE_CONVERSIONS
#define NEED_PALETTE_UTILS

#ifndef NEED_LOCAL_WEED_PLUGIN
#include <weed/weed-plugin.h>
#include <weed/weed-utils.h> // optional
#include <weed/weed-plugin-utils.h> // optional
#else
#include "../../libweed/weed-plugin.h"
#include "../../libweed/weed-utils.h" // optional
#include "../../libweed/weed-plugin-utils.h" // optional
#endif

#include "weed-plugin-utils.c" // optional

/////////////////////////////////////////////////////////////

static inline void make_white(unsigned char *pixel) {
  weed_memset(pixel, 255, 3);
}

static inline void nine_fill(unsigned char *new_data, int row, unsigned char *o) {
  // fill nine pixels with the centre colour
  new_data[-row - 3] = new_data[-row] = new_data[-row + 3] = new_data[-3] = new_data[0] =
                                          new_data[3] = new_data[row - 3] = new_data[row] = new_data[row + 3] = o[0];
  new_data[-row - 2] = new_data[-row + 1] = new_data[-row + 4] = new_data[-2] = new_data[1] =
                         new_data[4] = new_data[row - 2] = new_data[row + 1] = new_data[row + 4] = o[1];
  new_data[-row - 1] = new_data[-row + 2] = new_data[-row + 5] = new_data[-1] = new_data[2] =
                         new_data[5] = new_data[row - 1] = new_data[row + 2] = new_data[row + 5] = o[2];
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

static weed_error_t xeffect_process(weed_plant_t *inst, weed_timecode_t timestamp) {
  weed_plant_t *in_channel = weed_get_in_channel(inst, 0),
                *out_channel = weed_get_out_channel(inst, 0);

  int width = weed_channel_get_width(in_channel) * 3 - 4;
  int height = weed_channel_get_height(in_channel);
  int palette = weed_channel_get_palette(in_channel);

  int irowstride = weed_channel_get_stride(in_channel);
  int orowstride = weed_channel_get_stride(out_channel);

  unsigned char *src = weed_channel_get_pixel_data(in_channel) + irowstride;
  unsigned char *dst = weed_channel_get_pixel_data(out_channel) + orowstride;

  unsigned int myluma, threshold = 10000;
  int nbr;

  for (int h = 1; h < height - 2; h++) {
    for (int i = 3; i < width; i += 3) {
      myluma = calc_luma(&src[h * irowstride + i], palette, 0);
      nbr = 0;
      for (int j = h - 1; j <= h + 1; j++) {
        for (int k = -3; k < 4; k += 3) {
          if ((j != h || k != 0) && ABS(calc_luma(&src[j * irowstride + i + k], palette, 0) - myluma) > threshold) nbr++;
        }
      }
      if (nbr < 2 || nbr > 5) {
        nine_fill(&dst[h * orowstride + i], orowstride, &src[h * irowstride + i]);
      } else {
        if (myluma < 12500) {
          blank_pixel(&dst[h * orowstride + i], palette, 0, NULL);
        } else {
          if (myluma > 20000) {
            make_white(&dst[h * orowstride + i]);
          }
        }
      }
    }
  }

  return WEED_SUCCESS;
}


WEED_SETUP_START(200, 200) {
  int palette_list[] = {WEED_PALETTE_RGB24, WEED_PALETTE_BGR24, WEED_PALETTE_END};

  weed_plant_t *in_chantmpls[] = {weed_channel_template_init("in channel 0", 0), NULL};
  weed_plant_t *out_chantmpls[] = {weed_channel_template_init("out channel 0", 0), NULL};

  weed_plant_t *filter_class = weed_filter_class_init("graphic novel", "salsaman", 1, 0, palette_list,
                               NULL, xeffect_process, NULL, in_chantmpls, out_chantmpls, NULL, NULL);

  weed_plugin_info_add_filter_class(plugin_info, filter_class);
  weed_plugin_set_package_version(plugin_info, package_version);
}
WEED_SETUP_END;

