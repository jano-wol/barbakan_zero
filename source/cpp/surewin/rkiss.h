#include "types.h"

class RKISS {

  uint64_t a, b, c, d;

  uint64_t rotate(uint64_t x, uint64_t k) const {
    return (x << k) | (x >> (64 - k));
  }

  uint64_t rand64() {

    const uint64_t e = a - rotate(b,  7);
    a = b ^ rotate(c, 13);
    b = c + rotate(d, 37);
    c = d + e;
    return d = e + a;
  }

public:
  RKISS(int seed = 73) {

    a = 0xF1EA5EED, b = c = d = 0xD4E12C77;

    for (int i = 0; i < seed; ++i) // Scramble a few rounds
        rand64();
  }

  template<typename T> T rand() { return T(rand64()); }
};

