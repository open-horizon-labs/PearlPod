#include "lyrics.h"
#include <stdio.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define lyric_malloc(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define lyric_calloc(n,s) heap_caps_calloc((n),(s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#else
#define lyric_malloc malloc
#define lyric_calloc calloc
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static int compare(const void *a, const void *b) {
  const pearl_cue *x = a, *y = b;
  return x->milliseconds < y->milliseconds   ? -1
         : x->milliseconds > y->milliseconds ? 1
                                             : 0;
}
void pearl_lyrics_free(pearl_lyrics *l) {
  free(l->text);
  free(l->cues);
  memset(l, 0, sizeof(*l));
}
bool pearl_lyrics_load(const char *path, pearl_lyrics *l) {
  pearl_lyrics_free(l);
  struct stat st;
  if (!path || stat(path, &st) || st.st_size <= 0 || st.st_size > 256 * 1024)
    return false;
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  l->text = lyric_malloc(st.st_size + 1);
  if (!l->text) {
    fclose(f);
    return false;
  }
  bool ok = fread(l->text, 1, st.st_size, f) == (size_t)st.st_size;
  fclose(f);
  l->text[st.st_size] = 0;
  if (!ok || memchr(l->text, 0, st.st_size)) {
    pearl_lyrics_free(l);
    return false;
  }
  const char *ext = strrchr(path, '.');
  if (!ext || strcmp(ext, ".lrc"))
    return true;
  l->cues = lyric_calloc(4096, sizeof(*l->cues));
  if (!l->cues) {
    pearl_lyrics_free(l);
    return false;
  }
  char *save = NULL, *line = strtok_r(l->text, "\n", &save);
  int offset = 0;
  while (line) {
    size_t n = strlen(line);
    if (n && line[n - 1] == '\r')
      line[--n] = 0;
    if (n > 2048) {
      pearl_lyrics_free(l);
      return false;
    }
    if (!strncmp(line, "[offset:", 8))
      offset = atoi(line + 8);
    char *p = line;
    unsigned added = l->count;
    while (*p == '[') {
      unsigned minute, second;
      int consumed = 0;
      if (sscanf(p, "[%u:%u%n", &minute, &second, &consumed) != 2 ||
          consumed <= 0 || minute > 71582 || second >= 60)
        break;
      p += consumed;
      unsigned fraction = 0, digits = 0;
      if (*p == '.' || *p == ':') {
        p++;
        while (*p >= '0' && *p <= '9' && digits < 3) {
          fraction = fraction * 10 + (*p++ - '0');
          digits++;
        }
        while (digits++ < 3)
          fraction *= 10;
      }
      if (*p != ']' || l->count >= 4096) {
        pearl_lyrics_free(l);
        return false;
      }
      p++;
      int64_t ms = (int64_t)minute * 60000 + second * 1000 + fraction + offset;
      l->cues[l->count++] =
          (pearl_cue){.milliseconds = ms < 0 ? 0 : (uint32_t)ms, .text = NULL};
    }
    if (l->count > added) {
      if (strlen(p) > 1024) { pearl_lyrics_free(l); return false; }
      for (unsigned i = added; i < l->count; i++) l->cues[i].text = p;
    }
    if (added == l->count && line[0] != '[' && line[0]) {
      pearl_lyrics_free(l);
      return false;
    }
    line = strtok_r(NULL, "\n", &save);
  }
  if (!l->count) {
    pearl_lyrics_free(l);
    return false;
  }
  qsort(l->cues, l->count, sizeof(*l->cues), compare);
  l->timed = true;
  return true;
}
int pearl_lyrics_at(const pearl_lyrics *l, uint32_t ms) {
  int lo = 0, hi = l->count;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    if (l->cues[mid].milliseconds <= ms)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo - 1;
}
