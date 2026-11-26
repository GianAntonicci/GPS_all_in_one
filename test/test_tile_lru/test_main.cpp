#include <unity.h>
#include "tile_lru.h"

void setUp() {}
void tearDown() {}

static const TileKey A{15, 1, 1}, B{15, 2, 1}, C{15, 3, 1};

void test_find_empty_returns_minus_one() {
  TileLru l(4);
  TEST_ASSERT_EQUAL_INT16(-1, l.find(A));
}

void test_acquire_then_find() {
  TileLru l(4);
  int16_t s = l.acquire(A);
  TEST_ASSERT_TRUE(s >= 0);
  TEST_ASSERT_EQUAL_INT16(s, l.find(A));
}

void test_fills_free_slots_first() {
  TileLru l(3);
  int16_t a = l.acquire(A), b = l.acquire(B), c = l.acquire(C);
  TEST_ASSERT_TRUE(a != b && b != c && a != c);
  TEST_ASSERT_TRUE(l.find(A) >= 0 && l.find(B) >= 0 && l.find(C) >= 0);
}

void test_evicts_least_recent() {
  TileLru l(2);
  l.acquire(A);
  int16_t b = l.acquire(B);
  l.find(A);
  int16_t c = l.acquire(C);
  TEST_ASSERT_EQUAL_INT16(b, c);
  TEST_ASSERT_EQUAL_INT16(-1, l.find(B));
  TEST_ASSERT_TRUE(l.find(A) >= 0);
}

void test_release_frees_slot() {
  TileLru l(2);
  l.acquire(B);
  int16_t s = l.acquire(A);
  l.release(s);
  TEST_ASSERT_EQUAL_INT16(-1, l.find(A));
  TEST_ASSERT_EQUAL_INT16(s, l.acquire(C));
}

void test_missing_entries() {
  TileLru l(2);
  l.markMissing(A);
  TEST_ASSERT_TRUE(l.isMissing(A));
  TEST_ASSERT_FALSE(l.isMissing(B));
}

void test_missing_ring_drops_oldest() {
  TileLru l(2);
  for (uint32_t i = 0; i < 65; ++i) l.markMissing(TileKey{10, i, 0});
  TEST_ASSERT_FALSE(l.isMissing(TileKey{10, 0, 0}));
  TEST_ASSERT_TRUE(l.isMissing(TileKey{10, 64, 0}));
}

void test_clear() {
  TileLru l(2);
  l.acquire(A);
  l.markMissing(B);
  l.clear();
  TEST_ASSERT_EQUAL_INT16(-1, l.find(A));
  TEST_ASSERT_FALSE(l.isMissing(B));
}

void test_keys_differ_by_zoom() {
  TileLru l(4);
  l.acquire(TileKey{15, 1, 1});
  TEST_ASSERT_EQUAL_INT16(-1, l.find(TileKey{16, 1, 1}));
  l.markMissing(TileKey{15, 5, 5});
  TEST_ASSERT_FALSE(l.isMissing(TileKey{16, 5, 5}));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_find_empty_returns_minus_one);
  RUN_TEST(test_acquire_then_find);
  RUN_TEST(test_fills_free_slots_first);
  RUN_TEST(test_evicts_least_recent);
  RUN_TEST(test_release_frees_slot);
  RUN_TEST(test_missing_entries);
  RUN_TEST(test_missing_ring_drops_oldest);
  RUN_TEST(test_clear);
  RUN_TEST(test_keys_differ_by_zoom);
  return UNITY_END();
}
