#pragma once
// Streaming search for the ElementIQ product marker inside a firmware image.
// Works across arbitrary chunk boundaries and over the WHOLE image (the marker string
// can sit anywhere in .rodata, not just in the first N KB).
// Matching is a simple restart-on-mismatch scan, correct because the marker's first
// character ('E') does not occur again inside it. Host-tested: tools/test_marker.cpp
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct MarkerScanner {
  const char* marker = "";
  size_t mlen = 0;
  size_t matched = 0;
  bool found = false;

  void begin(const char* m) { marker = m; mlen = strlen(m); matched = 0; found = false; }

  void feed(const uint8_t* d, size_t n) {
    if (found || mlen == 0) return;
    for (size_t i = 0; i < n; i++) {
      uint8_t c = d[i];
      if (c == (uint8_t)marker[matched]) {
        if (++matched == mlen) { found = true; return; }
      } else {
        matched = (c == (uint8_t)marker[0]) ? 1 : 0;
      }
    }
  }
};
