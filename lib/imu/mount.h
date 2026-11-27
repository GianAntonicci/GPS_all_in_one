#pragma once
// Lógica pura de orientação de montagem da IMU (sem Arduino).
// Referencial lógico do aparelho (mão direita):
//   +X = topo da tela, +Y = esquerda, +Z = para cima (saindo da tela).
// Suporta as 24 rotações próprias de 90° (det = +1) entre o chip e a caixa.
#include <cstdint>

enum class ImuMount : uint8_t {
  // Placa horizontal com face superior para cima (+Z_s = +Z)
  PLACA_PADRAO = 0,
  GIRADA_90 = 1,
  GIRADA_180 = 2,
  GIRADA_270 = 3,
  // Placa horizontal virada para baixo (-Z_s = +Z)
  VIRADA_PARA_BAIXO = 4,
  VIRADA_90 = 5,
  VIRADA_180 = 6,
  VIRADA_270 = 7,
  // Placa montada na vertical (eixos +X, -X, +Y, -Y do chip apontando para cima)
  VERT_XP_0 = 8,
  VERT_XP_90 = 9,
  VERT_XP_180 = 10,
  VERT_XP_270 = 11,
  VERT_XN_0 = 12,
  VERT_XN_90 = 13,
  VERT_XN_180 = 14,
  VERT_XN_270 = 15,
  VERT_YP_0 = 16,
  VERT_YP_90 = 17,
  VERT_YP_180 = 18,
  VERT_YP_270 = 19,
  VERT_YN_0 = 20,
  VERT_YN_90 = 21,
  VERT_YN_180 = 22,
  VERT_YN_270 = 23,
  COUNT = 24
};

constexpr uint8_t kImuMountCount = static_cast<uint8_t>(ImuMount::COUNT);

struct ImuMountMatrix {
  int8_t m[3][3];
};

bool imuMountValid(uint8_t raw);
ImuMount imuMountFromUint8(uint8_t raw, ImuMount fallback = ImuMount::PLACA_PADRAO);
ImuMountMatrix imuMountMatrix(ImuMount mount);
int imuMountDeterminant(ImuMount mount);
const char* imuMountName(ImuMount mount);

// Remapeia um vetor (acel, giro ou mag já corrigido do AK09916) do referencial do chip
// para o referencial do aparelho (+X topo da tela, +Y esquerda, +Z para cima).
void applyMount(ImuMount mount, float sx, float sy, float sz, float& bx, float& by, float& bz);

// Modo assistido em 2 passos (recebe leituras CRUAS do acelerômetro em g, antes do remapeamento):
//   1. flat: aparelho deitado com a tela para cima (define +Z)
//   2. tilt: topo da tela inclinado para baixo (define -X -> +X)
// Retorna false se as leituras forem inválidas ou sem inclinação suficiente.
bool detectMountAssisted(float flatAx, float flatAy, float flatAz, float tiltAx, float tiltAy,
                         float tiltAz, ImuMount& outMount);
