#include "wifi_config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void invalid(const char *text) {
  pearl_wifi_profile out[4], before[4];
  memset(out, 0x5a, sizeof(out));
  memcpy(before, out, sizeof(out));
  unsigned count = 99;
  assert(!pearl_wifi_config_parse(text, strlen(text), out, &count));
  assert(count == 99 && !memcmp(out, before, sizeof(out)));
}
static unsigned writes;
static bool fail_write;
static const char *file_path;
static bool persist(const pearl_wifi_profile *profiles, unsigned count,
                    void *ctx) {
  (void)ctx;
  assert(access(file_path, F_OK) == 0);
  assert(count == 2 && !strcmp(profiles[0].ssid, "Home #1"));
  writes++;
  return !fail_write;
}
static const char *good =
    "\xef\xbb\xbf" "config_version = 1\r\n[[networks]]\r\nssid = 'Home #1' # "
    "comment\r\npassword = 'abc#=xyz'\r\n[[networks]]\r\nssid = "
    "\"Listener\\u2665\"\r\npassword = \"quote\\\"slash\\\\\"\r\nhidden = "
    "true\r\n";
int main(void) {
  pearl_wifi_profile out[4] = {0};
  unsigned count = 0;
  assert(pearl_wifi_config_parse(good, strlen(good), out, &count));
  assert(count == 2 && !strcmp(out[0].ssid, "Home #1") &&
         !strcmp(out[0].password, "abc#=xyz") &&
         !strcmp(out[1].ssid, "Listener♥") &&
         !strcmp(out[1].password, "quote\"slash\\"));
  const char *pi =
      "config_version=1\n[wlan]\nssid='Pi "
      "network'\npassword='12345678'\npassword_encrypted=false\nhidden=true\n";
  assert(pearl_wifi_config_parse(pi, strlen(pi), out, &count) && count == 1 &&
         !strcmp(out[0].ssid, "Pi network"));
  const char *open = "[[networks]]\nssid='Open'\npassword=''\n";
  assert(pearl_wifi_config_parse(open, strlen(open), out, &count) &&
         count == 1 && !out[0].password[0]);
  invalid("config_version=2\n[[networks]]\nssid='a'\npassword='12345678'");
  invalid("[[networks]]\nssid='a'\npassword='12345678'\n[[networks]]\nssid='b'"
          "\npassword='short'");
  invalid("[[networks]]\nssid='a'\npassword='12345678'\n[[networks]]\nssid='a'"
          "\npassword='87654321'");
  invalid("[[networks]]\nssid='a'\npassword='12345678'\nextra='unsupported'");
  invalid("[[networks]]\nssid='a'");
  invalid("[wlan]\nssid='a'\npassword=12345678");
  invalid("[wlan]\nssid='a'\npassword='12345678'\npassword_encrypted=true");
  invalid("[wlan]\nssid='a'\npassword='12345678'\n[[networks]]\nssid='b'"
          "\npassword='12345678'");
  invalid("[[networks]]\nssid='a'\npassword='unterminated");
  invalid("[[networks]]\nssid='a'\nssid='b'\npassword='12345678'");
  invalid("[[networks]]\nssid=\"a\\u0000b\"\npassword='12345678'");
  invalid("[[networks]]\nssid='\xff'\npassword='12345678'");
  invalid("networks=[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]"
          "]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]");
  char many[2048] = {0};
  for (int i = 0; i < 5; i++) {
    char item[128];
    snprintf(item, sizeof(item),
             "[[networks]]\nssid='n%d'\npassword='12345678'\n", i);
    strcat(many, item);
  }
  invalid(many);
  char wide[512];
  snprintf(wide, sizeof(wide), "[[networks]]\nssid='%s'\npassword='12345678'",
           "♥♥♥♥♥♥♥♥♥♥♥");
  invalid(wide);
  snprintf(wide, sizeof(wide), "[[networks]]\nssid='%s'\npassword='12345678'",
           "♥♥♥♥♥♥♥♥♥♥ab");
  assert(pearl_wifi_config_parse(wide, strlen(wide), out, &count));
  char *huge = malloc(8193);
  memset(huge, '#', 8193);
  assert(!pearl_wifi_config_parse(huge, 8193, out, &count));
  free(huge);
  char path[] = "/tmp/pearl-wifi-config-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  assert(write(fd, good, strlen(good)) == (ssize_t)strlen(good));
  close(fd);
  file_path = path;
  pearl_wifi_profile before[4];
  memcpy(before, out, sizeof(out));
  unsigned before_count = count;
  fail_write = true;
  assert(pearl_wifi_config_import(path, persist, NULL, out, &count) ==
         WIFI_IMPORT_STORAGE);
  assert(access(path, F_OK) == 0 && count == before_count &&
         !memcmp(before, out, sizeof(out)));
  fail_write = false;
  assert(pearl_wifi_config_import(path, persist, NULL, out, &count) ==
         WIFI_IMPORT_DONE);
  assert(access(path, F_OK) != 0 && count == 2 && writes == 2);
  assert(pearl_wifi_config_import(path, persist, NULL, out, &count) ==
         WIFI_IMPORT_MISSING);
  puts(
      "Card TOML: multiple networks, Pi-style wlan, quoting/Unicode/CRLF/BOM, "
      "open/hidden networks, byte limits, malformed/duplicate/oversized/nested "
      "input, unchanged outputs on failure, and commit-before-delete pass.");
}
