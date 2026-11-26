#include "keyboard.h"

#include <cctype>

namespace {
const char* const kRows[4] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789.,?!"};
}  // namespace

KeyboardModel::KeyboardModel() {
  size_t n = 0;
  for (uint8_t r = 0; r < 4; ++r)
    for (uint8_t c = 0; c < 10; ++c) keys_[n++] = Key{KeyKind::Char, kRows[r][c], c, r, 1};
  keys_[n++] = Key{KeyKind::Space, ' ', 0, 4, 3};
  keys_[n++] = Key{KeyKind::Backspace, 0, 3, 4, 2};
  keys_[n++] = Key{KeyKind::Shift, 0, 5, 4, 2};
  keys_[n++] = Key{KeyKind::Send, 0, 7, 4, 3};
  clear();
}

char KeyboardModel::label(size_t i) const {
  char c = keys_[i].ch;
  return (!upper_ && c >= 'A' && c <= 'Z') ? (char)tolower((unsigned char)c) : c;
}

void KeyboardModel::clear() {
  len_ = 0;
  text_[0] = 0;
}

bool KeyboardModel::press(size_t i) {
  if (i >= kKeys) return false;
  const Key& k = keys_[i];
  switch (k.kind) {
    case KeyKind::Char:
    case KeyKind::Space:
      if (len_ < kMax) {
        text_[len_++] = k.kind == KeyKind::Space ? ' ' : label(i);
        text_[len_] = 0;
      }
      return false;
    case KeyKind::Backspace:
      if (len_) text_[--len_] = 0;
      return false;
    case KeyKind::Shift:
      upper_ = !upper_;
      return false;
    case KeyKind::Send:
      return len_ > 0;
  }
  return false;
}
