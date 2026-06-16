#include "net/quest_replication.h"

#include <nanobuf.h>

namespace rec::net {
namespace {

// Quest-record flag bits, packed into one byte (status flags) and one byte per
// objective. Kept separate so neither byte runs out of room as fields grow.
enum QuestFlags : u8 {
  kFlagRunning = 1u << 0,
  kFlagActive = 1u << 1,
  kFlagComplete = 1u << 2,
};
enum ObjectiveFlags : u8 {
  kObjDisplayed = 1u << 0,
  kObjCompleted = 1u << 1,
};

// One quest is encoded as a self-contained little-endian byte record so the
// outer message can carry it as an opaque bytes entry:
//   u64 handle | i32 stage | u8 status_flags | u32 objective_count
//   then objective_count * (i32 index | u8 obj_flags)
// All integers are little-endian, matching the nanobuf runtime's codec.
void AppendU32(std::vector<u8>& out, u32 v) {
  u8 buf[4];
  nanobuf::StoreLe<u32>(buf, v);
  out.insert(out.end(), buf, buf + 4);
}
void AppendU64(std::vector<u8>& out, u64 v) {
  u8 buf[8];
  nanobuf::StoreLe<u64>(buf, v);
  out.insert(out.end(), buf, buf + 8);
}

std::vector<u8> EncodeQuestRecord(const quest::QuestStatus& q) {
  std::vector<u8> rec;
  AppendU64(rec, q.handle);
  AppendU32(rec, static_cast<u32>(q.stage));
  u8 flags = 0;
  if (q.running) flags |= kFlagRunning;
  if (q.active) flags |= kFlagActive;
  if (q.complete) flags |= kFlagComplete;
  rec.push_back(flags);
  AppendU32(rec, static_cast<u32>(q.objectives.size()));
  for (const quest::ObjectiveStatus& obj : q.objectives) {
    AppendU32(rec, static_cast<u32>(obj.index));
    u8 obj_flags = 0;
    if (obj.displayed) obj_flags |= kObjDisplayed;
    if (obj.completed) obj_flags |= kObjCompleted;
    rec.push_back(obj_flags);
  }
  return rec;
}

// Reads one quest record built by EncodeQuestRecord, advancing `pos`. Returns
// false on any read that would run past `size`, leaving `out` unspecified.
bool DecodeQuestRecord(const u8* data, size_t size, quest::QuestStatus* out) {
  size_t pos = 0;
  auto take = [&](size_t n) -> const u8* {
    if (n > size - pos) return nullptr;
    const u8* p = data + pos;
    pos += n;
    return p;
  };

  const u8* p = take(8);
  if (!p) return false;
  out->handle = nanobuf::LoadLe<u64>(p);

  p = take(4);
  if (!p) return false;
  out->stage = static_cast<i32>(nanobuf::LoadLe<u32>(p));

  p = take(1);
  if (!p) return false;
  const u8 flags = *p;
  out->running = (flags & kFlagRunning) != 0;
  out->active = (flags & kFlagActive) != 0;
  out->complete = (flags & kFlagComplete) != 0;

  p = take(4);
  if (!p) return false;
  const u32 objective_count = nanobuf::LoadLe<u32>(p);
  // Each objective is exactly 5 bytes; reject a count the buffer cannot hold
  // before reserving, so a corrupt length never triggers a huge allocation.
  if (objective_count > (size - pos) / 5) return false;
  out->objectives.clear();
  out->objectives.reserve(objective_count);
  for (u32 i = 0; i < objective_count; ++i) {
    quest::ObjectiveStatus obj;
    p = take(4);
    if (!p) return false;
    obj.index = static_cast<i32>(nanobuf::LoadLe<u32>(p));
    p = take(1);
    if (!p) return false;
    obj.displayed = (*p & kObjDisplayed) != 0;
    obj.completed = (*p & kObjCompleted) != 0;
    out->objectives.push_back(std::move(obj));
  }
  return pos == size;  // no trailing junk inside a record
}

}  // namespace

std::vector<u8> EncodeQuestUpdate(const std::vector<quest::QuestStatus>& quests) {
  // The outer frame is a nanobuf message carrying the quest records as a
  // list<bytes>, so the runtime's bounds-checked Parse guards the whole blob.
  nanobuf::Writer writer;
  writer.Begin(/*fixed_len=*/6);  // 2-byte header + one 4-byte offset slot
  writer.PutOffsetList<quest::QuestStatus>(
      /*slot=*/2, quests,
      [](nanobuf::Writer& w, const quest::QuestStatus& q) {
        std::vector<u8> rec = EncodeQuestRecord(q);
        return w.HeapBytes(rec);
      });
  return writer.TakeBuffer();
}

std::optional<std::vector<quest::QuestStatus>> DecodeQuestUpdate(ByteSpan data) {
  std::optional<nanobuf::View> view =
      nanobuf::View::Parse(data.data(), data.size());
  if (!view) return std::nullopt;

  std::optional<nanobuf::BytesList> records = view->BytesListAt(/*slot=*/2);
  if (!records) return std::nullopt;  // present but corrupt list

  std::vector<quest::QuestStatus> out;
  out.reserve(records->size());
  for (size_t i = 0; i < records->size(); ++i) {
    std::optional<nanobuf::BytesView> bytes = records->Get(i);
    if (!bytes) return std::nullopt;
    quest::QuestStatus status;
    if (!DecodeQuestRecord(bytes->data, bytes->size, &status)) return std::nullopt;
    out.push_back(std::move(status));
  }
  return out;
}

std::vector<u8> QuestReplicator::Build(
    const std::vector<quest::QuestStatus>& snapshot) {
  const bool full = force_full_;
  force_full_ = false;

  std::vector<quest::QuestStatus> changed;
  for (const quest::QuestStatus& q : snapshot) {
    u32* sent = sent_revision_.find(q.handle);
    if (!full && sent && *sent == q.revision) continue;
    if (sent) {
      *sent = q.revision;
    } else {
      sent_revision_.insert(q.handle, q.revision);
    }
    changed.push_back(q);
  }

  // Nothing to say. Callers treat the empty vector as "skip this tick".
  if (changed.empty()) return {};
  return EncodeQuestUpdate(changed);
}

bool ApplyQuestUpdate(
    ByteSpan data,
    const std::function<void(const quest::QuestStatus&)>& sink) {
  std::optional<std::vector<quest::QuestStatus>> quests = DecodeQuestUpdate(data);
  if (!quests) return false;
  for (const quest::QuestStatus& q : *quests) sink(q);
  return true;
}

}  // namespace rec::net
