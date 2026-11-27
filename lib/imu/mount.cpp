#include "mount.h"

#include <cmath>

namespace {

struct MountEntry {
  const char* name;
  ImuMountMatrix mat;
};

// Cada matriz M satisfaz b = M * s, onde:
//   linha 0 = +X do aparelho (topo da tela)
//   linha 1 = +Y do aparelho (esquerda) = linha2 x linha0
//   linha 2 = +Z do aparelho (para cima)
// Todas as 24 entradas são rotações próprias (M * M^T = I, det(M) = +1).
constexpr MountEntry kMounts[kImuMountCount] = {
    // 0..3: +Z_s -> +Z (tela para cima, placa normal)
    {"PLACA_PADRAO", {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}},
    {"GIRADA_90", {{{0, 1, 0}, {-1, 0, 0}, {0, 0, 1}}}},
    {"GIRADA_180", {{{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}}}},
    {"GIRADA_270", {{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}}},
    // 4..7: -Z_s -> +Z (placa virada para baixo)
    {"VIRADA_PARA_BAIXO", {{{1, 0, 0}, {0, -1, 0}, {0, 0, -1}}}},
    {"VIRADA_90", {{{0, -1, 0}, {-1, 0, 0}, {0, 0, -1}}}},
    {"VIRADA_180", {{{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}}}},
    {"VIRADA_270", {{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}}},
    // 8..11: +X_s -> +Z
    {"VERT_XP_0", {{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}}},
    {"VERT_XP_90", {{{0, 0, 1}, {0, -1, 0}, {1, 0, 0}}}},
    {"VERT_XP_180", {{{0, -1, 0}, {0, 0, -1}, {1, 0, 0}}}},
    {"VERT_XP_270", {{{0, 0, -1}, {0, 1, 0}, {1, 0, 0}}}},
    // 12..15: -X_s -> +Z
    {"VERT_XN_0", {{{0, 1, 0}, {0, 0, -1}, {-1, 0, 0}}}},
    {"VERT_XN_90", {{{0, 0, -1}, {0, -1, 0}, {-1, 0, 0}}}},
    {"VERT_XN_180", {{{0, -1, 0}, {0, 0, 1}, {-1, 0, 0}}}},
    {"VERT_XN_270", {{{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}}}},
    // 16..19: +Y_s -> +Z
    {"VERT_YP_0", {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}}},
    {"VERT_YP_90", {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}}},
    {"VERT_YP_180", {{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}}},
    {"VERT_YP_270", {{{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}}},
    // 20..23: -Y_s -> +Z
    {"VERT_YN_0", {{{0, 0, 1}, {-1, 0, 0}, {0, -1, 0}}}},
    {"VERT_YN_90", {{{-1, 0, 0}, {0, 0, -1}, {0, -1, 0}}}},
    {"VERT_YN_180", {{{0, 0, -1}, {1, 0, 0}, {0, -1, 0}}}},
    {"VERT_YN_270", {{{1, 0, 0}, {0, 0, 1}, {0, -1, 0}}}},
};

void cross3(const int8_t a[3], const int8_t b[3], int8_t out[3]) {
  out[0] = (int8_t)(a[1] * b[2] - a[2] * b[1]);
  out[1] = (int8_t)(a[2] * b[0] - a[0] * b[2]);
  out[2] = (int8_t)(a[0] * b[1] - a[1] * b[0]);
}

bool matricesEqual(const ImuMountMatrix& a, const ImuMountMatrix& b) {
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 3; ++c)
      if (a.m[r][c] != b.m[r][c]) return false;
  return true;
}

}  // namespace

bool imuMountValid(uint8_t raw) { return raw < kImuMountCount; }

ImuMount imuMountFromUint8(uint8_t raw, ImuMount fallback) {
  return imuMountValid(raw) ? static_cast<ImuMount>(raw) : fallback;
}

ImuMountMatrix imuMountMatrix(ImuMount mount) {
  const uint8_t idx = static_cast<uint8_t>(mount);
  return kMounts[idx < kImuMountCount ? idx : 0].mat;
}

int imuMountDeterminant(ImuMount mount) {
  const ImuMountMatrix mat = imuMountMatrix(mount);
  const auto& m = mat.m;
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
         m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
         m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

const char* imuMountName(ImuMount mount) {
  const uint8_t idx = static_cast<uint8_t>(mount);
  return kMounts[idx < kImuMountCount ? idx : 0].name;
}

void applyMount(ImuMount mount, float sx, float sy, float sz, float& bx, float& by, float& bz) {
  const ImuMountMatrix mat = imuMountMatrix(mount);
  const auto& m = mat.m;
  bx = m[0][0] * sx + m[0][1] * sy + m[0][2] * sz;
  by = m[1][0] * sx + m[1][1] * sy + m[1][2] * sz;
  bz = m[2][0] * sx + m[2][1] * sy + m[2][2] * sz;
}

bool detectMountAssisted(float flatAx, float flatAy, float flatAz, float tiltAx, float tiltAy,
                         float tiltAz, ImuMount& outMount) {
  if (!std::isfinite(flatAx) || !std::isfinite(flatAy) || !std::isfinite(flatAz) ||
      !std::isfinite(tiltAx) || !std::isfinite(tiltAy) || !std::isfinite(tiltAz)) {
    return false;
  }

  const float flat[3] = {flatAx, flatAy, flatAz};
  const float tilt[3] = {tiltAx, tiltAy, tiltAz};

  const float flatNorm = std::sqrt(flat[0] * flat[0] + flat[1] * flat[1] + flat[2] * flat[2]);
  const float tiltNorm = std::sqrt(tilt[0] * tilt[0] + tilt[1] * tilt[1] + tilt[2] * tilt[2]);
  if (flatNorm < 0.5f || tiltNorm < 0.5f) return false;

  // Passo 1: encontra eixo dominante da gravidade com a tela para cima (+Z do aparelho).
  int zAxis = 0;
  float zBest = std::fabs(flat[0]);
  for (int i = 1; i < 3; ++i) {
    if (std::fabs(flat[i]) > zBest) {
      zBest = std::fabs(flat[i]);
      zAxis = i;
    }
  }
  if (zBest / flatNorm < 0.7f) return false;  // aparelho muito inclinado no passo 1
  const int8_t zSign = (flat[zAxis] >= 0.0f) ? 1 : -1;

  // Passo 2: com o topo da tela inclinado para baixo, gravidade aponta para -X do aparelho.
  // Procura eixo dominante ortogonal a zAxis.
  int xAxis = -1;
  float xBest = 0.0f;
  for (int i = 0; i < 3; ++i) {
    if (i == zAxis) continue;
    if (std::fabs(tilt[i]) > xBest) {
      xBest = std::fabs(tilt[i]);
      xAxis = i;
    }
  }
  if (xAxis < 0 || xBest < 0.35f) return false;  // inclinação insuficiente no passo 2
  // Topo para baixo => ax_aparelho negativo => +X tem sinal oposto ao medido em tilt[xAxis].
  const int8_t xSign = (tilt[xAxis] >= 0.0f) ? -1 : 1;

  ImuMountMatrix target{};
  target.m[2][zAxis] = zSign;
  target.m[0][xAxis] = xSign;
  cross3(target.m[2], target.m[0], target.m[1]);  // Y = Z x X

  for (uint8_t i = 0; i < kImuMountCount; ++i) {
    if (matricesEqual(kMounts[i].mat, target)) {
      outMount = static_cast<ImuMount>(i);
      return true;
    }
  }
  return false;
}
