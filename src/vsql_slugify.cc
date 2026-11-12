// Copyright (c) 2026 VillageSQL Contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <https://www.gnu.org/licenses/>.

#include <villagesql/vsql.h>

#include <cctype>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

using namespace vsql;

// An exception that escapes a VDF aborts mysqld for every connection. Turn it
// into a warning instead, so only this row returns NULL.
template <typename Result, typename Body>
static void guard(Result &out, Body &&body) {
  try {
    body();
  } catch (const std::exception &e) {
    out.warning(e.what());
  } catch (...) {
    out.warning("unexpected error");
  }
}

// Transliterates a Unicode codepoint to its ASCII approximation.
// Returns nullptr for codepoints with no mapping.
static const char *transliterate(uint32_t cp) {
  switch (cp) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
    case 0xC6: return "AE";
    case 0xC7: return "C";
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
    case 0xD0: return "D";
    case 0xD1: return "N";
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8: return "O";
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
    case 0xDD: return "Y";
    case 0xDE: return "Th";
    case 0xDF: return "ss";
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
    case 0xE6: return "ae";
    case 0xE7: return "c";
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
    case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
    case 0xF0: return "d";
    case 0xF1: return "n";
    case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return "o";
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
    case 0xFD: case 0xFF: return "y";
    case 0xFE: return "th";
    case 0x110: return "D"; case 0x111: return "d";
    case 0x141: return "L"; case 0x142: return "l";
    case 0x150: return "O"; case 0x151: return "o";
    case 0x170: return "U"; case 0x171: return "u";
    case 0x15A: return "S"; case 0x15B: return "s";
    case 0x179: return "Z"; case 0x17A: return "z";
    case 0x17B: return "Z"; case 0x17C: return "z";
    case 0x102: return "A"; case 0x103: return "a";
    case 0x218: return "S"; case 0x219: return "s";
    case 0x21A: return "T"; case 0x21B: return "t";
    case 0x10C: return "C"; case 0x10D: return "c";
    case 0x10E: return "D"; case 0x10F: return "d";
    case 0x11A: return "E"; case 0x11B: return "e";
    case 0x147: return "N"; case 0x148: return "n";
    case 0x158: return "R"; case 0x159: return "r";
    case 0x160: return "S"; case 0x161: return "s";
    case 0x164: return "T"; case 0x165: return "t";
    case 0x16E: return "U"; case 0x16F: return "u";
    case 0x17D: return "Z"; case 0x17E: return "z";
    default: return nullptr;
  }
}

// Decodes one UTF-8 character, returns the codepoint and advances pos.
static uint32_t decode_utf8(const char *s, size_t len, size_t &pos) {
  auto b = static_cast<unsigned char>(s[pos]);
  if (b < 0x80) {
    pos += 1;
    return b;
  }
  if ((b & 0xE0) == 0xC0 && pos + 1 < len) {
    uint32_t cp = ((b & 0x1F) << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
    pos += 2;
    return cp;
  }
  if ((b & 0xF0) == 0xE0 && pos + 2 < len) {
    uint32_t cp = ((b & 0x0F) << 12) |
                  ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 6) |
                  (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
    pos += 3;
    return cp;
  }
  if ((b & 0xF8) == 0xF0 && pos + 3 < len) {
    uint32_t cp = ((b & 0x07) << 18) |
                  ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 12) |
                  ((static_cast<unsigned char>(s[pos + 2]) & 0x3F) << 6) |
                  (static_cast<unsigned char>(s[pos + 3]) & 0x3F);
    pos += 4;
    return cp;
  }
  pos += 1;
  return 0xFFFD;
}

// slugify(value TEXT) -> TEXT
// Produces a URL-safe slug: transliterate, lowercase, collapse non-alnum to '-', trim.
static void slugify(StringArg input, StringResult out) {
  if (input.is_null()) {
    out.set_null();
    return;
  }

  auto sv = input.value();
  std::string result;
  result.reserve(sv.size());
  bool prev_sep = true;

  size_t pos = 0;
  while (pos < sv.size()) {
    uint32_t cp = decode_utf8(sv.data(), sv.size(), pos);

    if (cp < 0x80 && std::isalnum(static_cast<unsigned char>(cp))) {
      result += static_cast<char>(std::tolower(static_cast<unsigned char>(cp)));
      prev_sep = false;
      continue;
    }

    const char *ascii = transliterate(cp);
    if (ascii) {
      for (const char *p = ascii; *p; ++p) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
      }
      prev_sep = false;
      continue;
    }

    if (!prev_sep) {
      result += '-';
      prev_sep = true;
    }
  }

  size_t cap = out.buffer().size();
  if (result.size() > cap) result.resize(cap);
  if (!result.empty() && result.back() == '-') {
    result.pop_back();
  }

  out.set(std::string_view(result));
}

void vdf_slugify(StringArg input, StringResult out) {
  guard(out, [&] { slugify(input, out); });
}

VEF_GENERATE_ENTRY_POINTS(
    make_extension()
        .func(make_func<&vdf_slugify>("slugify")
                  .returns(STRING)
                  .param(STRING)
                  .buffer_size(65536)
                  .deterministic()
                  .build()))
