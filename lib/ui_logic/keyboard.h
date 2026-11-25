#pragma once
// Teclado em grade 10x5 navegado pelo encoder (ordem de leitura).
#include <cstddef>
#include <cstdint>

enum class KeyKind : uint8_t { Char, Space, Backspace, Shift, Send };

struct Key {
  KeyKind kind;
  char ch;  // maiúscula para letras; o modelo aplica minúscula se !upper()
  uint8_t col, row, span;
};

class KeyboardModel {
 public:
  static constexpr size_t kMax = 48;
  KeyboardModel();
  size_t keyCount() const { return kKeys; }
  const Key& key(size_t i) const { return keys_[i]; }
  char label(size_t i) const;  // caractere já com maiúscula/minúscula
  bool upper() const { return upper_; }
  bool press(size_t i);        // true = ENVIAR com texto não vazio
  const char* text() const { return text_; }
  size_t length() const { return len_; }
  void clear();

 private:
  static constexpr size_t kKeys = 44;
  Key keys_[kKeys];
  char text_[kMax + 1];
  size_t len_ = 0;
  bool upper_ = true;
};
