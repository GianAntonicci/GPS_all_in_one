#include <unity.h>
#include <cstdio>
#include <cctype>
#include <cstring>
#include "protocol.h"

void setUp() {}
void tearDown() {}

static size_t enc(const Packet& p, char* buf) { return protoEncode(p, buf, FRAME_MAX); }

// Monta um frame com checksum correto a partir do corpo (entre $ e *).
static void frameOf(const char* body, char* out) {
  snprintf(out, FRAME_MAX, "$%s*%02X", body, protoChecksum(body, strlen(body)));
}

void test_checksum_known() {
  const char* s = "PING,A1,7";
  uint8_t x = 0;
  for (const char* c = s; *c; ++c) x ^= (uint8_t)*c;
  TEST_ASSERT_EQUAL_HEX8(x, protoChecksum(s, strlen(s)));
}

void test_encode_ping_exact() {
  Packet p{};
  p.type = PktType::PING;
  strcpy(p.id, "A1");
  p.seq = 7;
  char buf[FRAME_MAX];
  size_t n = enc(p, buf);
  char expect[32];
  snprintf(expect, sizeof expect, "$PING,A1,7*%02X\n", protoChecksum("PING,A1,7", 9));
  TEST_ASSERT_EQUAL_STRING(expect, buf);
  TEST_ASSERT_EQUAL(strlen(expect), n);
}

void test_pos_roundtrip_negative() {
  Packet p{};
  p.type = PktType::POS;
  strcpy(p.id, "B2");
  p.seq = 255;
  p.lat = -23.55052;
  p.lon = -46.63331;
  p.alt = 760;
  p.bat = 87;
  char buf[FRAME_MAX];
  size_t n = enc(p, buf);
  TEST_ASSERT_TRUE(n > 0);
  Packet q{};
  TEST_ASSERT_TRUE(protoDecode(buf, n - 1, q));
  TEST_ASSERT_EQUAL(PktType::POS, q.type);
  TEST_ASSERT_EQUAL_STRING("B2", q.id);
  TEST_ASSERT_EQUAL_UINT8(255, q.seq);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, -23.55052, q.lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, -46.63331, q.lon);
  TEST_ASSERT_EQUAL_INT32(760, q.alt);
  TEST_ASSERT_EQUAL_UINT8(87, q.bat);
  TEST_ASSERT_FALSE(q.hasBaroAlt);
}

void test_pos_with_baro_alt_roundtrip() {
  Packet p{};
  p.type = PktType::POS;
  strcpy(p.id, "A1");
  p.seq = 12;
  p.lat = -23.55052;
  p.lon = -46.63331;
  p.alt = 755;
  p.bat = 91;
  p.hasBaroAlt = true;
  p.baroAlt = 762;
  char buf[FRAME_MAX];
  size_t n = enc(p, buf);
  TEST_ASSERT_TRUE(n > 0);
  Packet q{};
  TEST_ASSERT_TRUE(protoDecode(buf, n - 1, q));
  TEST_ASSERT_EQUAL(PktType::POS, q.type);
  TEST_ASSERT_EQUAL_INT32(755, q.alt);
  TEST_ASSERT_EQUAL_UINT8(91, q.bat);
  TEST_ASSERT_TRUE(q.hasBaroAlt);
  TEST_ASSERT_EQUAL_INT32(762, q.baroAlt);
}

void test_msg_with_commas_roundtrip() {
  Packet p{};
  p.type = PktType::MSG;
  strcpy(p.id, "A1");
  p.seq = 3;
  strcpy(p.text, "oi, tudo bem, e ai");
  char buf[FRAME_MAX];
  size_t n = enc(p, buf);
  Packet q{};
  TEST_ASSERT_TRUE(protoDecode(buf, n - 1, q));
  TEST_ASSERT_EQUAL(PktType::MSG, q.type);
  TEST_ASSERT_EQUAL_STRING("oi, tudo bem, e ai", q.text);
}

static void roundtrip(const Packet& p, Packet& q) {
  char buf[FRAME_MAX];
  size_t n = enc(p, buf);
  TEST_ASSERT_TRUE_MESSAGE(n > 0, "encode falhou");
  TEST_ASSERT_EQUAL_CHAR('\n', buf[n - 1]);
  TEST_ASSERT_TRUE_MESSAGE(protoDecode(buf, n - 1, q), buf);
  TEST_ASSERT_EQUAL(p.type, q.type);
  TEST_ASSERT_EQUAL_STRING(p.id, q.id);
  TEST_ASSERT_EQUAL_UINT8(p.seq, q.seq);
}

void test_all_types_roundtrip() {
  Packet p{}, q{};
  strcpy(p.id, "C");
  p.seq = 0;

  p.type = PktType::PING; roundtrip(p, q);
  p.type = PktType::SEEN; p.ackSeq = 33; roundtrip(p, q);
  TEST_ASSERT_EQUAL_UINT8(33, q.ackSeq);
  p.type = PktType::SOSOFF; roundtrip(p, q);

  p.type = PktType::ACK; p.ackSeq = 12; roundtrip(p, q);
  TEST_ASSERT_EQUAL_UINT8(12, q.ackSeq);

  p.type = PktType::SOS; p.lat = 1.5; p.lon = -2.25; p.bat = 100; roundtrip(p, q);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, 1.5, q.lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, -2.25, q.lon);
  TEST_ASSERT_EQUAL_UINT8(100, q.bat);

  p.type = PktType::MSG; strcpy(p.text, "Estou bem"); roundtrip(p, q);
  TEST_ASSERT_EQUAL_STRING("Estou bem", q.text);
}

void test_decode_rejects_bad_checksum() {
  char f[FRAME_MAX];
  frameOf("PING,A1,7", f);
  size_t n = strlen(f);
  f[n - 1] = (f[n - 1] == '0') ? '1' : '0';
  Packet q{};
  TEST_ASSERT_FALSE(protoDecode(f, n, q));
}

void test_decode_rejects_unknown_type() {
  char f[FRAME_MAX];
  frameOf("FOO,A1,1", f);
  Packet q{};
  TEST_ASSERT_FALSE(protoDecode(f, strlen(f), q));
}

void test_decode_rejects_missing_fields() {
  char f[FRAME_MAX];
  frameOf("POS,A1,3,-23.5", f);
  Packet q{};
  TEST_ASSERT_FALSE(protoDecode(f, strlen(f), q));
}

void test_decode_rejects_seq_over_255() {
  char f[FRAME_MAX];
  frameOf("PING,A1,300", f);
  Packet q{};
  TEST_ASSERT_FALSE(protoDecode(f, strlen(f), q));
}

void test_decode_rejects_bad_id() {
  char f[FRAME_MAX];
  Packet q{};
  frameOf("PING,ABC,1", f);
  TEST_ASSERT_FALSE(protoDecode(f, strlen(f), q));
  frameOf("PING,,1", f);
  TEST_ASSERT_FALSE(protoDecode(f, strlen(f), q));
}

void test_sanitize_accents_and_specials() {
  char out[MSG_MAX_LEN + 1];
  protoSanitize("voc\xC3\xAA a$b*c", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("voc?? a?b?c", out);
}

void test_sanitize_newline_and_control() {
  char out[MSG_MAX_LEN + 1];
  protoSanitize("a\nb\rc\td", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("a?b?c?d", out);
}

void test_sanitize_truncates_to_48() {
  char in[61];
  memset(in, 'x', 60);
  in[60] = 0;
  char out[MSG_MAX_LEN + 1];
  protoSanitize(in, out, sizeof out);
  TEST_ASSERT_EQUAL(48, strlen(out));
}

void test_encode_msg_never_emits_high_bytes() {
  Packet p{};
  p.type = PktType::MSG;
  strcpy(p.id, "A1");
  p.seq = 1;
  protoSanitize("a\xC3\xA7\xC3\xA3o*$\n", p.text, sizeof p.text);
  char buf[FRAME_MAX];
  size_t n = enc(p, buf);
  TEST_ASSERT_TRUE(n > 0);
  for (size_t i = 0; i < n; ++i) TEST_ASSERT_TRUE((uint8_t)buf[i] < 0x80);
  // exatamente um '$' e um '*' e um '\n' no frame
  int d = 0, s = 0, nl = 0;
  for (size_t i = 0; i < n; ++i) { d += buf[i] == '$'; s += buf[i] == '*'; nl += buf[i] == '\n'; }
  TEST_ASSERT_EQUAL(1, d); TEST_ASSERT_EQUAL(1, s); TEST_ASSERT_EQUAL(1, nl);
}

void test_encode_rejects_unsanitized_text() {
  Packet p{};
  p.type = PktType::MSG;
  strcpy(p.id, "A1");
  strcpy(p.text, "a*b");
  char buf[FRAME_MAX];
  TEST_ASSERT_EQUAL(0, enc(p, buf));
}

void test_needs_ack() {
  TEST_ASSERT_TRUE(protoNeedsAck(PktType::MSG));
  TEST_ASSERT_TRUE(protoNeedsAck(PktType::PING));
  TEST_ASSERT_TRUE(protoNeedsAck(PktType::SOS));
  TEST_ASSERT_TRUE(protoNeedsAck(PktType::SEEN));
  TEST_ASSERT_TRUE(protoNeedsAck(PktType::SOSOFF));
  TEST_ASSERT_FALSE(protoNeedsAck(PktType::POS));
  TEST_ASSERT_FALSE(protoNeedsAck(PktType::ACK));
}

void test_id_from_mac() {
  const uint8_t m1[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x10};
  const uint8_t m2[6] = {0x24, 0x6F, 0x28, 0x01, 0x03, 0x10};  // mesmo último byte
  char a[3], b[3], c[3];
  protoIdFromMac(m1, a);
  protoIdFromMac(m2, b);
  protoIdFromMac(m1, c);
  TEST_ASSERT_EQUAL(2, strlen(a));
  TEST_ASSERT_TRUE(isalnum((unsigned char)a[0]) && isalnum((unsigned char)a[1]));
  TEST_ASSERT_EQUAL_STRING(a, c);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(a, b));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_checksum_known);
  RUN_TEST(test_encode_ping_exact);
  RUN_TEST(test_pos_roundtrip_negative);
  RUN_TEST(test_pos_with_baro_alt_roundtrip);
  RUN_TEST(test_msg_with_commas_roundtrip);
  RUN_TEST(test_all_types_roundtrip);
  RUN_TEST(test_decode_rejects_bad_checksum);
  RUN_TEST(test_decode_rejects_unknown_type);
  RUN_TEST(test_decode_rejects_missing_fields);
  RUN_TEST(test_decode_rejects_seq_over_255);
  RUN_TEST(test_decode_rejects_bad_id);
  RUN_TEST(test_sanitize_accents_and_specials);
  RUN_TEST(test_sanitize_newline_and_control);
  RUN_TEST(test_sanitize_truncates_to_48);
  RUN_TEST(test_encode_msg_never_emits_high_bytes);
  RUN_TEST(test_encode_rejects_unsanitized_text);
  RUN_TEST(test_needs_ack);
  RUN_TEST(test_id_from_mac);
  return UNITY_END();
}
