#ifndef TU_MULT_H_
#define TU_MULT_H_

const uint8_t MULT_MAX = 14;    // max multiplier
const uint8_t MULT_BY_ONE = 7; // default multiplication

const char* const multiplierStrings[] = {
  "/8", "/7", "/6", "/5", "/4", "/3", "/2", "-", "x2", "x3", "x4", "x5", "x6", "x7", "x8"
};

const uint64_t multipliers_[] = {

  0x100000000,// /8
  0xE0000000, // /7
  0xC0000000, // /6
  0xA0000000, // /5
  0x80000000, // /4
  0x60000000, // /3
  0x40000000, // /2
  0x20000000, // x1
  0x10000000, // x2
  0xAAAAAAB,  // x3
  0x8000000,  // x4
  0x6666667,  // x5
  0x5555556,  // x6
  0x4924926,  // x7
  0x4000000   // x8
}; // = multiplier / 8.0f * 2^32

#endif
