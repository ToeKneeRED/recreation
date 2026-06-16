// dialoguetest: checks INFO response parsing and the INFO VMAD fragment parser
// (the fragment is what advances a quest when a line plays). No game data.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bethesda/record.h"
#include "bethesda/script_attachment.h"
#include "core/types.h"
#include "dialogue/dialogue.h"

using namespace rec;

namespace {

int g_failures = 0;

void Check(const char* what, bool ok) {
  std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) ++g_failures;
}

// Backing store for synthetic subrecord spans.
struct Buffers {
  std::vector<std::vector<u8>> store;
  ByteSpan Bytes(const void* p, size_t n) {
    auto& b = store.emplace_back(n);
    if (n) std::memcpy(b.data(), p, n);
    return ByteSpan(b.data(), b.size());
  }
  ByteSpan Str(const char* s) { return Bytes(s, std::strlen(s) + 1); }
};

void AppendU16(std::vector<u8>& v, u16 x) {
  v.push_back(static_cast<u8>(x));
  v.push_back(static_cast<u8>(x >> 8));
}
void AppendWStr(std::vector<u8>& v, const char* s) {
  u16 len = static_cast<u16>(std::strlen(s));
  AppendU16(v, len);
  v.insert(v.end(), s, s + len);
}

// An INFO VMAD with no scripts and a single begin fragment.
std::vector<u8> MakeInfoVmad(const char* script, const char* function) {
  std::vector<u8> v;
  AppendU16(v, 5);  // version
  AppendU16(v, 2);  // object format
  AppendU16(v, 0);  // script count
  v.push_back(2);   // fragment version
  v.push_back(0x01);  // flags: begin fragment present
  AppendWStr(v, "");  // shared file name
  v.push_back(0);     // begin: unknown byte
  AppendWStr(v, script);
  AppendWStr(v, function);
  return v;
}

void Add(bethesda::Record& r, u32 type, ByteSpan data) {
  bethesda::Subrecord sub;
  sub.type = type;
  sub.data = data;
  r.subrecords.push_back(std::move(sub));
}

void TestInfoFragments() {
  std::puts("info vmad fragments:");
  std::vector<u8> vmad = MakeInfoVmad("TIF__010A1234", "Fragment_0");
  bethesda::ScriptAttachment att;
  bethesda::InfoFragments frags;
  bool ok = bethesda::ParseInfoFragments(ByteSpan(vmad.data(), vmad.size()), &att, &frags);
  Check("parse ok", ok);
  Check("no user scripts", att.scripts.empty());
  Check("begin script", frags.begin.script_name == "TIF__010A1234");
  Check("begin function", frags.begin.function == "Fragment_0");
  Check("no end fragment", frags.end.script_name.empty());

  // A truncated tail must not crash and must leave fragments empty.
  std::vector<u8> truncated(vmad.begin(), vmad.begin() + 8);
  bethesda::InfoFragments frags2;
  bethesda::ParseInfoFragments(ByteSpan(truncated.data(), truncated.size()), &att, &frags2);
  Check("truncated tail -> no begin fragment", frags2.begin.script_name.empty());
}

void TestInfoRecord() {
  std::puts("info record:");
  Buffers buf;
  std::vector<u8> vmad = MakeInfoVmad("TIF__010A1234", "Fragment_0");
  bethesda::Record info;
  Add(info, FourCc('R', 'N', 'A', 'M'), buf.Str("Where are we going?"));
  Add(info, FourCc('N', 'A', 'M', '1'), buf.Str("Follow me to the keep."));
  Add(info, FourCc('V', 'M', 'A', 'D'), buf.Bytes(vmad.data(), vmad.size()));

  dialogue::Response r = dialogue::ParseInfoRecord(info, 0x000a5678ull, "fallback prompt", nullptr);
  Check("info handle", r.info == 0x000a5678ull);
  Check("player line from RNAM", r.player_line == "Where are we going?");
  Check("npc line from NAM1", r.npc_line == "Follow me to the keep.");
  Check("fragment script parsed", r.fragment_script == "TIF__010A1234");
  Check("fragment function parsed", r.fragment_function == "Fragment_0");

  // No RNAM falls back to the topic text.
  bethesda::Record info2;
  Add(info2, FourCc('N', 'A', 'M', '1'), buf.Str("Hello."));
  dialogue::Response r2 = dialogue::ParseInfoRecord(info2, 1, "Greeting", nullptr);
  Check("player line falls back to topic", r2.player_line == "Greeting");
  Check("no fragment without vmad", r2.fragment_script.empty());
}

}  // namespace

int main() {
  TestInfoFragments();
  TestInfoRecord();
  if (g_failures == 0) {
    std::puts("dialogue: all checks passed");
    return 0;
  }
  std::printf("dialogue: %d checks FAILED\n", g_failures);
  return 1;
}
