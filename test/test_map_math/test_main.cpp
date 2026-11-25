#include <unity.h>
#include "map_math.h"

void setUp() {}
void tearDown() {}

static bool hasTile(const TileRef* t, uint8_t n, uint32_t x, uint32_t y) {
  for (uint8_t i = 0; i < n; ++i)
    if (t[i].x == x && t[i].y == y) return true;
  return false;
}
static const TileRef* getTile(const TileRef* t, uint8_t n, uint32_t x, uint32_t y) {
  for (uint8_t i = 0; i < n; ++i)
    if (t[i].x == x && t[i].y == y) return &t[i];
  return nullptr;
}

void test_pixel_origin() {
  PixelPos p = latLonToPixel(0, 0, 0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 128.0, p.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 128.0, p.y);
  p = latLonToPixel(0, 0, 1);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 256.0, p.x);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 256.0, p.y);
  TEST_ASSERT_DOUBLE_WITHIN(1e-6, 256.0, latLonToPixel(0, 180, 0).x);
}

void test_pixel_sao_paulo_z15() {
  PixelPos p = latLonToPixel(-23.55052, -46.63331, 15);
  TEST_ASSERT_DOUBLE_WITHIN(0.5, 3107669.12, p.x);
  TEST_ASSERT_DOUBLE_WITHIN(0.5, 4759209.66, p.y);
  TEST_ASSERT_EQUAL_UINT32(12139, (uint32_t)(p.x / 256));
  TEST_ASSERT_EQUAL_UINT32(18590, (uint32_t)(p.y / 256));
}

void test_pixel_lat_clamped() {
  PixelPos a = latLonToPixel(89, 0, 0), b = latLonToPixel(85.05112878, 0, 0);
  TEST_ASSERT_DOUBLE_WITHIN(1e-3, b.y, a.y);
  TEST_ASSERT_DOUBLE_WITHIN(1e-3, 0.0, a.y);
}

void test_mpp_sao_paulo() { TEST_ASSERT_DOUBLE_WITHIN(1e-3, 4.3794, metersPerPixel(-23.55052, 15)); }

void test_visible_aligned_center() {
  TileRef t[8];
  uint8_t n = visibleTiles({512, 512}, 2, 320, 240, t, 8);
  TEST_ASSERT_EQUAL_UINT8(4, n);
  TEST_ASSERT_TRUE(hasTile(t, n, 1, 1) && hasTile(t, n, 2, 1) && hasTile(t, n, 1, 2) && hasTile(t, n, 2, 2));
  const TileRef* a = getTile(t, n, 1, 1);
  TEST_ASSERT_EQUAL_INT16(-96, a->sx);
  TEST_ASSERT_EQUAL_INT16(-136, a->sy);
  TEST_ASSERT_EQUAL_UINT8(2, a->z);
}

void test_visible_three_columns() {
  TileRef t[8];
  uint8_t n = visibleTiles({384, 384}, 2, 320, 240, t, 8);
  TEST_ASSERT_EQUAL_UINT8(3, n);
  TEST_ASSERT_TRUE(hasTile(t, n, 0, 1) && hasTile(t, n, 1, 1) && hasTile(t, n, 2, 1));
  TEST_ASSERT_EQUAL_INT16(-224, getTile(t, n, 0, 1)->sx);
}

void test_visible_wraps_x() {
  TileRef t[8];
  uint8_t n = visibleTiles({10, 384}, 2, 320, 240, t, 8);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  const TileRef* w = getTile(t, n, 3, 1);
  TEST_ASSERT_NOT_NULL(w);
  TEST_ASSERT_EQUAL_INT16(-106, w->sx);
  TEST_ASSERT_TRUE(hasTile(t, n, 0, 1));
}

void test_visible_skips_rows_outside() {
  TileRef t[8];
  uint8_t n = visibleTiles({512, 10}, 2, 320, 240, t, 8);
  TEST_ASSERT_EQUAL_UINT8(2, n);
  for (uint8_t i = 0; i < n; ++i) TEST_ASSERT_EQUAL_UINT32(0, t[i].y);
}

void test_visible_respects_max() {
  TileRef t[8];
  TEST_ASSERT_EQUAL_UINT8(2, visibleTiles({512, 512}, 2, 320, 240, t, 2));
}

void test_to_screen_center() {
  PixelPos c{1000, 2000};
  ScreenPt p = toScreen(c, c, 320, 240);
  TEST_ASSERT_EQUAL_INT32(160, p.x);
  TEST_ASSERT_EQUAL_INT32(120, p.y);
  p = toScreen({1010, 1995}, c, 320, 240);
  TEST_ASSERT_EQUAL_INT32(170, p.x);
  TEST_ASSERT_EQUAL_INT32(115, p.y);
}

void test_on_screen_margin() {
  TEST_ASSERT_FALSE(onScreen({5, 5}, 320, 240, 8));
  TEST_ASSERT_TRUE(onScreen({160, 120}, 320, 240, 8));
}

void test_edge_arrow_up() {
  EdgeArrow a = edgeArrow({160, -1000}, 320, 240, 12);
  TEST_ASSERT_EQUAL_INT16(160, a.x);
  TEST_ASSERT_EQUAL_INT16(12, a.y);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, a.angleDeg);
}

void test_edge_arrow_right() {
  EdgeArrow a = edgeArrow({5000, 120}, 320, 240, 12);
  TEST_ASSERT_EQUAL_INT16(307, a.x);
  TEST_ASSERT_EQUAL_INT16(120, a.y);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 90.0, a.angleDeg);
}

void test_edge_arrow_down_left() {
  EdgeArrow a = edgeArrow({-1000, 1120}, 320, 240, 12);
  TEST_ASSERT_TRUE(a.angleDeg > 180.0 && a.angleDeg < 270.0);
  TEST_ASSERT_TRUE(a.x == 12 || a.y == 227);
  TEST_ASSERT_TRUE(a.x >= 12 && a.x <= 307 && a.y >= 12 && a.y <= 227);
}

void test_zoom_initial_and_step() {
  ZoomLevels z;
  TEST_ASSERT_TRUE(z.empty());
  z.add(10); z.add(12); z.add(16); z.add(18);
  TEST_ASSERT_EQUAL_UINT8(4, z.count());
  TEST_ASSERT_TRUE(z.has(12));
  TEST_ASSERT_FALSE(z.has(11));
  TEST_ASSERT_EQUAL_UINT8(16, z.initial());
  TEST_ASSERT_EQUAL_UINT8(18, z.step(16, +1));
  TEST_ASSERT_EQUAL_UINT8(18, z.step(18, +1));
  TEST_ASSERT_EQUAL_UINT8(12, z.step(16, -1));
  TEST_ASSERT_EQUAL_UINT8(10, z.step(10, -1));
  ZoomLevels hi;
  hi.add(17); hi.add(18);
  TEST_ASSERT_EQUAL_UINT8(17, hi.initial());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pixel_origin);
  RUN_TEST(test_pixel_sao_paulo_z15);
  RUN_TEST(test_pixel_lat_clamped);
  RUN_TEST(test_mpp_sao_paulo);
  RUN_TEST(test_visible_aligned_center);
  RUN_TEST(test_visible_three_columns);
  RUN_TEST(test_visible_wraps_x);
  RUN_TEST(test_visible_skips_rows_outside);
  RUN_TEST(test_visible_respects_max);
  RUN_TEST(test_to_screen_center);
  RUN_TEST(test_on_screen_margin);
  RUN_TEST(test_edge_arrow_up);
  RUN_TEST(test_edge_arrow_right);
  RUN_TEST(test_edge_arrow_down_left);
  RUN_TEST(test_zoom_initial_and_step);
  return UNITY_END();
}
