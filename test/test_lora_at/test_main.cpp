#include <unity.h>
#include <cstring>
#include "lora_at.h"

void setUp() {}
void tearDown() {}

void test_desired_settings_order_and_values() {
  AtSetting s[16];
  size_t n = loraDesiredSettings("47", s, 16);
  const char* keys[] = {"SWITCH", "MODE", "LEVEL", "CHANNEL", "POWE", "DRSSI", "PACKET", "BAUD"};
  const char* vals[] = {"0", "0", "0", "47", "22", "1", "3", "3"};
  TEST_ASSERT_EQUAL(8, n);
  for (size_t i = 0; i < 8; ++i) {
    TEST_ASSERT_EQUAL_STRING(keys[i], s[i].key);
    TEST_ASSERT_EQUAL_STRING(vals[i], s[i].value);
  }
}

void test_desired_settings_cap() {
  AtSetting s[3];
  TEST_ASSERT_EQUAL(0, loraDesiredSettings("47", s, 3));
}

void test_parse_query_with_spaces() {
  char v[8];
  TEST_ASSERT_TRUE(atParseQuery("+LEVEL =2", "LEVEL", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("2", v);
  TEST_ASSERT_TRUE(atParseQuery("+ CHANNEL=41", "CHANNEL", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("41", v);
  TEST_ASSERT_TRUE(atParseQuery("+drssi=1\r", "DRSSI", v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("1", v);
}

void test_parse_query_wrong_key_false() {
  char v[8];
  TEST_ASSERT_FALSE(atParseQuery("+LEVEL=2", "MODE", v, sizeof v));
  TEST_ASSERT_FALSE(atParseQuery("+LEVELX=2", "LEVEL", v, sizeof v));
  TEST_ASSERT_FALSE(atParseQuery("OK", "LEVEL", v, sizeof v));
  TEST_ASSERT_FALSE(atParseQuery("+LEVEL=", "LEVEL", v, sizeof v));
}

void test_ok_and_error() {
  TEST_ASSERT_TRUE(atIsOk("OK"));
  TEST_ASSERT_TRUE(atIsOk("OK\r"));
  TEST_ASSERT_FALSE(atIsOk("+LEVEL=0"));
  TEST_ASSERT_TRUE(atIsError("ERROR=3"));
  TEST_ASSERT_TRUE(atIsError("EEROR=3"));
  TEST_ASSERT_FALSE(atIsError("OK"));
}

void test_value_equals_hex_channel() {
  TEST_ASSERT_TRUE(atValueEquals("CHANNEL", "47", "47"));
  TEST_ASSERT_TRUE(atValueEquals("CHANNEL", "047", "47"));
  TEST_ASSERT_TRUE(atValueEquals("CHANNEL", "4a", "4A"));
  TEST_ASSERT_FALSE(atValueEquals("CHANNEL", "41", "47"));
  TEST_ASSERT_TRUE(atValueEquals("POWE", "22", "22"));
  TEST_ASSERT_TRUE(atValueEquals("LEVEL", "00", "0"));
  TEST_ASSERT_FALSE(atValueEquals("LEVEL", "2", "0"));
  TEST_ASSERT_FALSE(atValueEquals("LEVEL", "", "0"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_desired_settings_order_and_values);
  RUN_TEST(test_desired_settings_cap);
  RUN_TEST(test_parse_query_with_spaces);
  RUN_TEST(test_parse_query_wrong_key_false);
  RUN_TEST(test_ok_and_error);
  RUN_TEST(test_value_equals_hex_channel);
  return UNITY_END();
}
