// Helper for test/config_page_check.py --xcheck: reads lines "lat<TAB>lon<TAB>place<TAB>sleep<TAB>wake<TAB>wx" from stdin
// (a field may be the single character '~' for "not sent") and prints one gqc_validate result per line:
// the error code, or "ok lat lon sleep wake" on success. The Python stand-in must give the same answers.
//   g++ -std=c++17 -O1 -Wall -Wextra -Werror test/config_xcheck.cpp lib/gqsafe/src/gqconf.cpp lib/gqsafe/src/gq_crc32.cpp
#include <stdio.h>
#include <string.h>
#include "../lib/gqsafe/src/gqconf.h"

int main() {
  char line[512];
  GqConf cur;
  gqc_defaults(&cur);
  while (fgets(line, sizeof(line), stdin)) {
    size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    char* f[6] = { 0, 0, 0, 0, 0, 0 };
    char* p = line;
    for (int i = 0; i < 6 && p; i++) {
      f[i] = p;
      char* t = strchr(p, '\t');
      if (t) { *t = 0; p = t + 1; } else p = nullptr;
    }
    for (int i = 0; i < 6; i++) if (!f[i] || (f[i][0] == '~' && !f[i][1])) f[i] = nullptr;
    GqcInput in;
    in.lat = f[0]; in.lon = f[1]; in.place = f[2]; in.sleep_time = f[3]; in.wake_time = f[4];
    in.weather = f[5] ? (f[5][0] == '1') : 0;   // the form marker is always present: unchecked means off
    in.fahrenheit = -1;
    in.sleep_on = 0;
    GqConf out;
    int e = gqc_validate(&cur, &in, &out);
    if (e) printf("%d\n", e);
    else printf("ok %d %d %d %d\n", out.lat_c, out.lon_c, out.sleep_min, out.wake_min);
  }
  return 0;
}
