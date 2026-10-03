#include "lyrics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
  char path[] = "/tmp/pearl-lyrics-XXXXXX.lrc";
  int fd = mkstemps(path, 4);
  FILE *f = fdopen(fd, "w");
  fputs("[00:02.500]Two\n[00:01.100][00:03.000]One\n", f);
  fclose(f);
  pearl_lyrics l = {0};
  assert(pearl_lyrics_load(path, &l));
  assert(l.timed && l.count == 3);
  assert(pearl_lyrics_at(&l, 1099) == -1);
  assert(pearl_lyrics_at(&l, 1100) == 0);
  assert(pearl_lyrics_at(&l, 2500) == 1);
  assert(!strcmp(l.cues[2].text, "One"));
  pearl_lyrics_free(&l);
  f = fopen(path, "w");
  fputs("[00:99.000]Invalid\n", f);
  fclose(f);
  assert(!pearl_lyrics_load(path, &l));
  assert(!l.text && !l.cues);
  unlink(path);
  puts("Lyrics timing, repeated stamps, sorting, invalid input and cleanup "
       "pass");
}
