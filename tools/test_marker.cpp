// Host test for include/ota_marker.h   build+run:  g++ -std=c++17 -Iinclude tools/test_marker.cpp -o /tmp/tm && /tmp/tm
#include "ota_marker.h"
#include <cstdio>
#include <vector>
#include <cstdlib>
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

static bool scan(const std::vector<uint8_t>& img, size_t chunk) {
  MarkerScanner s; s.begin("ElementIQ-FW");
  for (size_t i = 0; i < img.size(); i += chunk) s.feed(img.data() + i, std::min(chunk, img.size() - i));
  return s.found;
}
int main() {
  const char* m = "ElementIQ-FW";
  std::vector<uint8_t> img(1700000);
  for (auto& b : img) b = (uint8_t)(rand() & 0xFF);
  // marker far beyond the old 256000-byte window
  memcpy(img.data() + 1234567, m, 12);
  CHECK(scan(img, 1024), "marker at 1.2 MB found (old code only scanned first 250 KB)");
  // marker straddling every possible chunk boundary offset
  bool all = true;
  for (size_t off = 0; off < 12; off++) {
    std::vector<uint8_t> t(8192, 0x41);
    memcpy(t.data() + 1024 - off, m, 12);           // crosses the 1024 boundary at each offset
    if (!scan(t, 1024)) all = false;
  }
  CHECK(all, "marker found when split across a chunk boundary at all 12 offsets");
  CHECK(scan(img, 1), "byte-at-a-time chunks");
  CHECK(scan(img, 1460), "TCP-sized chunks");
  // near-miss text and absence
  std::vector<uint8_t> n(5000, 0x41);
  memcpy(n.data() + 100, "ElementIQ-F", 11); memcpy(n.data() + 300, "EEEElementIQ-FX", 15);
  CHECK(!scan(n, 1024), "near-misses are not a match");
  memcpy(n.data() + 2000, "EEElementIQ-FW", 14);
  CHECK(scan(n, 7), "match after repeated 'E' prefix");
  std::vector<uint8_t> none(300000);
  for (auto& b : none) b = (uint8_t)(rand() & 0xFF);
  CHECK(!scan(none, 1024), "random data without marker is rejected");
  printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
  return fails ? 1 : 0;
}
