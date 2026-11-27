#include <unity.h>
#include <cmath>
#include <cstring>
#include "compass.h"
#include "mount.h"

void setUp() {}
void tearDown() {}

// Aplica a transposta M^T (inversa de uma rotação ortogonal) para simular o que o chip mede
// quando um vetor físico conhecido (bx, by, bz) está no referencial do aparelho.
static void bodyToSensor(ImuMount mount, float bx, float by, float bz, float& sx, float& sy,
                         float& sz) {
  const ImuMountMatrix mat = imuMountMatrix(mount);
  const auto& m = mat.m;
  sx = m[0][0] * bx + m[1][0] * by + m[2][0] * bz;
  sy = m[0][1] * bx + m[1][1] * by + m[2][1] * bz;
  sz = m[0][2] * bx + m[1][2] * by + m[2][2] * bz;
}

void test_all_24_matrices_are_valid_unique_rotations() {
  for (uint8_t i = 0; i < kImuMountCount; ++i) {
    const ImuMount m = static_cast<ImuMount>(i);
    TEST_ASSERT_EQUAL_INT(1, imuMountDeterminant(m));

    // Verifica ortogonalidade: M * M^T == I
    const ImuMountMatrix mat = imuMountMatrix(m);
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        int dot = mat.m[r][0] * mat.m[c][0] + mat.m[r][1] * mat.m[c][1] +
                  mat.m[r][2] * mat.m[c][2];
        TEST_ASSERT_EQUAL_INT(r == c ? 1 : 0, dot);
      }
    }

    // Verifica unicidade entre todas as 24 matrizes
    for (uint8_t j = i + 1; j < kImuMountCount; ++j) {
      const ImuMountMatrix other = imuMountMatrix(static_cast<ImuMount>(j));
      TEST_ASSERT_NOT_EQUAL(0, std::memcmp(mat.m, other.m, sizeof(mat.m)));
    }
  }
}

void test_same_physical_vector_gives_same_heading_in_all_24_mounts() {
  // Vetor físico no referencial do aparelho (inclinado + campo magnético):
  const float bodyAx = 0.30f, bodyAy = -0.20f, bodyAz = 0.93f;
  const float bodyMx = 18.0f, bodyMy = 24.0f, bodyMz = -15.0f;
  const float decl = -21.5f;

  float expectedHeading = 0.0f;
  TEST_ASSERT_TRUE(
      tiltCompensatedHeading(bodyAx, bodyAy, bodyAz, bodyMx, bodyMy, bodyMz, decl, expectedHeading));

  for (uint8_t i = 0; i < kImuMountCount; ++i) {
    const ImuMount mount = static_cast<ImuMount>(i);
    // Simula leitura crua do chip montado nessa orientação
    float sax, say, saz, smx, smy, smz;
    bodyToSensor(mount, bodyAx, bodyAy, bodyAz, sax, say, saz);
    bodyToSensor(mount, bodyMx, bodyMy, bodyMz, smx, smy, smz);

    // Remapeia de volta para o referencial do aparelho
    float rax, ray, raz, rmx, rmy, rmz;
    applyMount(mount, sax, say, saz, rax, ray, raz);
    applyMount(mount, smx, smy, smz, rmx, rmy, rmz);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, bodyAx, rax);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, bodyAy, ray);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, bodyAz, raz);

    float h = 0.0f;
    TEST_ASSERT_TRUE(tiltCompensatedHeading(rax, ray, raz, rmx, rmy, rmz, decl, h));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expectedHeading, h);
  }
}

void test_assisted_detection_identifies_all_24_mounts() {
  for (uint8_t i = 0; i < kImuMountCount; ++i) {
    const ImuMount expected = static_cast<ImuMount>(i);

    // Passo 1: tela para cima (com leve ruído de mão ~5°) -> (0.04, -0.05, 0.99)
    float fx, fy, fz;
    bodyToSensor(expected, 0.04f, -0.05f, 0.99f, fx, fy, fz);

    // Passo 2: topo da tela inclinado ~60° para baixo -> (-0.86, 0.03, 0.50)
    float tx, ty, tz;
    bodyToSensor(expected, -0.86f, 0.03f, 0.50f, tx, ty, tz);

    ImuMount detected = ImuMount::PLACA_PADRAO;
    TEST_ASSERT_TRUE(detectMountAssisted(fx, fy, fz, tx, ty, tz, detected));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(detected));
  }
}

void test_assisted_detection_rejects_bad_inputs() {
  ImuMount out = ImuMount::PLACA_PADRAO;
  // Sem inclinação no passo 2 (ambos planos)
  TEST_ASSERT_FALSE(detectMountAssisted(0, 0, 1, 0, 0, 1, out));
  // Vetor nulo
  TEST_ASSERT_FALSE(detectMountAssisted(0, 0, 0, -1, 0, 0, out));
  // Passo 1 inclinado demais (45° entre dois eixos)
  TEST_ASSERT_FALSE(detectMountAssisted(0.7f, 0, 0.7f, -1, 0, 0, out));
}

void test_mount_names_and_validation() {
  TEST_ASSERT_EQUAL_STRING("PLACA_PADRAO", imuMountName(ImuMount::PLACA_PADRAO));
  TEST_ASSERT_EQUAL_STRING("GIRADA_90", imuMountName(ImuMount::GIRADA_90));
  TEST_ASSERT_EQUAL_STRING("VIRADA_PARA_BAIXO", imuMountName(ImuMount::VIRADA_PARA_BAIXO));
  TEST_ASSERT_TRUE(imuMountValid(0));
  TEST_ASSERT_TRUE(imuMountValid(23));
  TEST_ASSERT_FALSE(imuMountValid(24));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ImuMount::PLACA_PADRAO),
                          static_cast<uint8_t>(imuMountFromUint8(99)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_all_24_matrices_are_valid_unique_rotations);
  RUN_TEST(test_same_physical_vector_gives_same_heading_in_all_24_mounts);
  RUN_TEST(test_assisted_detection_identifies_all_24_mounts);
  RUN_TEST(test_assisted_detection_rejects_bad_inputs);
  RUN_TEST(test_mount_names_and_validation);
  return UNITY_END();
}
