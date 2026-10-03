#include "artwork.h"
#include "metadata.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  assert(argc == 2);
  pearl_tags t;
  pearl_metadata(argv[1], &t);
  assert(t.title[0] && t.artist[0] && t.album[0]);
  size_t bytes;
  uint8_t *art = pearl_art_payload(argv[1], &bytes);
  assert(art && bytes > 0);
  free(art);
  uint8_t *pixels = pearl_art_pixels(argv[1]);
  assert(pixels);
  free(pixels);
  puts("Prepared MP3 tags and embedded artwork parse through PearlPod code");
}
