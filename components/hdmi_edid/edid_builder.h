#pragma once
// Pure C++ EDID generator (no ESPHome dependencies, so it can be unit-tested on a PC).
//
// Produces a 256 byte EDID: a base block plus one CEA-861 extension block that looks
// like a plain 1080p TV with 2-channel LPCM audio. The HDMI vendor specific data block
// carries the CEC physical address the *soundbar* will adopt (e.g. 0x1000 = 1.0.0.0,
// i.e. "you are plugged into HDMI port 1 of the TV at the root").

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace esphome {
namespace hdmi_edid {

constexpr size_t EDID_BLOCK_SIZE = 128;
constexpr size_t EDID_SIZE = 2 * EDID_BLOCK_SIZE;
using EdidBytes = std::array<uint8_t, EDID_SIZE>;

struct EdidParams {
  uint16_t physical_address{0x1000};  // address the soundbar takes, A.B.C.D = nibbles
  std::string monitor_name{"ESP ARC"};  // max 13 characters
  std::string manufacturer{"ESP"};      // three letters A-Z (PNP ID)
};

// Sets the last byte of a 128 byte block so that all bytes sum to 0 (mod 256).
inline void edid_finalize_block(uint8_t *block) {
  uint8_t sum = 0;
  for (size_t i = 0; i < EDID_BLOCK_SIZE - 1; i++)
    sum = static_cast<uint8_t>(sum + block[i]);
  block[EDID_BLOCK_SIZE - 1] = static_cast<uint8_t>(0x100 - sum);
}

inline EdidBytes build_edid(const EdidParams &p) {
  EdidBytes edid{};
  uint8_t *b = edid.data();

  // ---- Base block -------------------------------------------------------
  static const uint8_t HEADER[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
  std::memcpy(b, HEADER, sizeof(HEADER));

  // Manufacturer PNP ID: three 5-bit letters, big endian.
  char m[3] = {'E', 'S', 'P'};
  if (p.manufacturer.size() == 3) {
    bool ok = true;
    for (char c : p.manufacturer)
      if (c < 'A' || c > 'Z')
        ok = false;
    if (ok)
      for (int i = 0; i < 3; i++)
        m[i] = p.manufacturer[i];
  }
  uint16_t id = static_cast<uint16_t>(((m[0] - '@') << 10) | ((m[1] - '@') << 5) | (m[2] - '@'));
  b[8] = static_cast<uint8_t>(id >> 8);
  b[9] = static_cast<uint8_t>(id & 0xFF);
  b[10] = 0x01;  // product code (LE)
  b[11] = 0x00;
  b[12] = 0x01;  // serial (LE)
  b[13] = b[14] = b[15] = 0x00;
  b[16] = 1;   // week of manufacture
  b[17] = 36;  // year - 1990 => 2026
  b[18] = 1;   // EDID 1.3
  b[19] = 3;
  b[20] = 0x80;  // digital input
  b[21] = 0x00;  // image size cm (undefined)
  b[22] = 0x00;
  b[23] = 0x78;  // gamma 2.2
  b[24] = 0x1E;  // RGB+YCbCr, sRGB, preferred timing = DTD 1

  // Standard sRGB chromaticity coordinates.
  static const uint8_t CHROMA[10] = {0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54};
  std::memcpy(b + 25, CHROMA, sizeof(CHROMA));

  // Established timings: none. Standard timings: unused (0x01 0x01).
  b[35] = b[36] = b[37] = 0x00;
  for (int i = 0; i < 8; i++) {
    b[38 + 2 * i] = 0x01;
    b[39 + 2 * i] = 0x01;
  }

  // Descriptor 1 (offset 54): 1920x1080 @ 60 Hz detailed timing, 148.5 MHz.
  static const uint8_t DTD_1080P60[18] = {0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58,
                                          0x2C, 0x45, 0x00, 0xC4, 0x8E, 0x21, 0x00, 0x00, 0x1E};
  std::memcpy(b + 54, DTD_1080P60, sizeof(DTD_1080P60));

  // Descriptor 2 (offset 72): monitor name.
  {
    uint8_t *d = b + 72;
    d[0] = d[1] = d[2] = 0x00;
    d[3] = 0xFC;
    d[4] = 0x00;
    size_t n = p.monitor_name.size() > 13 ? 13 : p.monitor_name.size();
    for (size_t i = 0; i < 13; i++) {
      if (i < n)
        d[5 + i] = static_cast<uint8_t>(p.monitor_name[i]);
      else if (i == n)
        d[5 + i] = 0x0A;
      else
        d[5 + i] = 0x20;
    }
  }

  // Descriptor 3 (offset 90): monitor range limits 24-75 Hz, 30-83 kHz, 170 MHz max.
  {
    static const uint8_t RANGE[18] = {0x00, 0x00, 0x00, 0xFD, 0x00, 0x18, 0x4B, 0x1E, 0x53,
                                      0x11, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};
    std::memcpy(b + 90, RANGE, sizeof(RANGE));
  }

  // Descriptor 4 (offset 108): dummy (tag 0x10), rest zero.
  b[111] = 0x10;

  b[126] = 0x01;  // one extension block
  edid_finalize_block(b);

  // ---- CEA-861 extension block -----------------------------------------
  uint8_t *x = b + EDID_BLOCK_SIZE;
  x[0] = 0x02;  // CEA extension tag
  x[1] = 0x03;  // revision 3
  x[3] = 0x70;  // basic audio, YCbCr 4:4:4 and 4:2:2, no native DTDs

  size_t pos = 4;
  // Video data block: VIC 16 (1080p60, native), 4 (720p60), 31 (1080p50), 19 (720p50).
  x[pos++] = (2 << 5) | 4;
  x[pos++] = 0x90;
  x[pos++] = 0x04;
  x[pos++] = 0x1F;
  x[pos++] = 0x13;
  // Audio data block: one SAD, LPCM 2 ch, 32/44.1/48 kHz, 16/20/24 bit.
  x[pos++] = (1 << 5) | 3;
  x[pos++] = 0x09;
  x[pos++] = 0x07;
  x[pos++] = 0x07;
  // Speaker allocation: front left/right.
  x[pos++] = (4 << 5) | 3;
  x[pos++] = 0x01;
  x[pos++] = 0x00;
  x[pos++] = 0x00;
  // HDMI vendor specific data block: OUI 00-0C-03 (LSB first) + physical address.
  x[pos++] = (3 << 5) | 5;
  x[pos++] = 0x03;
  x[pos++] = 0x0C;
  x[pos++] = 0x00;
  x[pos++] = static_cast<uint8_t>(p.physical_address >> 8);
  x[pos++] = static_cast<uint8_t>(p.physical_address & 0xFF);

  x[2] = static_cast<uint8_t>(pos);  // offset of first DTD (none follow)
  edid_finalize_block(x);
  return edid;
}

}  // namespace hdmi_edid
}  // namespace esphome
