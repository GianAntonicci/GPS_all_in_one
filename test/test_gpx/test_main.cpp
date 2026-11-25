#include <unity.h>
#include <cstring>
#include "gpx_format.h"

void setUp() {}
void tearDown() {}

void test_header_exact() {
  char b[256];
  size_t n = gpxHeader(b, sizeof b);
  const char* e =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<gpx version=\"1.1\" creator=\"GPS_all_in_one\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
      "<trk><trkseg>\n";
  TEST_ASSERT_EQUAL_STRING(e, b);
  TEST_ASSERT_EQUAL_size_t(strlen(e), n);
}

void test_point_exact() {
  char b[200];
  DateTime t{2026, 11, 25, 17, 1, 5};
  size_t n = gpxPoint(b, sizeof b, -23.55052, -46.63331, 760.0f, t, false);
  const char* e =
      "<trkpt lat=\"-23.550520\" lon=\"-46.633310\"><ele>760.0</ele>"
      "<time>2026-11-25T17:01:05Z</time></trkpt>\n";
  TEST_ASSERT_EQUAL_STRING(e, b);
  TEST_ASSERT_EQUAL_size_t(strlen(e), n);
}

void test_point_with_break() {
  char b[200];
  DateTime t{2026, 11, 25, 17, 1, 5};
  gpxPoint(b, sizeof b, -23.55052, -46.63331, 760.0f, t, true);
  TEST_ASSERT_EQUAL_INT(0, strncmp(b, "</trkseg><trkseg>\n<trkpt ", 25));
}

void test_point_too_small_returns_zero() {
  char b[20];
  DateTime t{2026, 11, 25, 17, 1, 5};
  TEST_ASSERT_EQUAL_size_t(0, gpxPoint(b, sizeof b, 0, 0, 0, t, false));
}

void test_footer_exact() { TEST_ASSERT_EQUAL_STRING("</trkseg></trk></gpx>\n", kGpxFooter); }

void test_filename() {
  char b[40];
  gpxFileName(b, sizeof b, DateTime{2026, 11, 25, 14, 1, 5}, 0);
  TEST_ASSERT_EQUAL_STRING("/tracks/20261125_140105.gpx", b);
  gpxFileName(b, sizeof b, DateTime{2026, 11, 25, 14, 1, 5}, 2);
  TEST_ASSERT_EQUAL_STRING("/tracks/20261125_140105_2.gpx", b);
  gpxFileName(b, sizeof b, DateTime{2027, 2, 1, 3, 4, 5}, 1);
  TEST_ASSERT_EQUAL_STRING("/tracks/20270201_030405.gpx", b);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_header_exact);
  RUN_TEST(test_point_exact);
  RUN_TEST(test_point_with_break);
  RUN_TEST(test_point_too_small_returns_zero);
  RUN_TEST(test_footer_exact);
  RUN_TEST(test_filename);
  return UNITY_END();
}
