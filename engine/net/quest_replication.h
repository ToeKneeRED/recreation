#ifndef RECREATION_NET_QUEST_REPLICATION_H_
#define RECREATION_NET_QUEST_REPLICATION_H_

#include <functional>
#include <optional>
#include <vector>

#include <base/containers/unordered_map.h>

#include "core/types.h"
#include "quest/quest_system.h"

namespace rec::net {

// Quest state replication. Unlike entity snapshots, quest progress is sparse,
// permanent, and must never be lost, so updates ride the reliable channel and
// carry only the bits a client cannot derive on its own. Stage and objective
// TEXT is never sent: clients resolve it locally from their own QuestDef, so a
// QuestUpdate is just handles, stages, and a handful of flags.

// Serializes the replicated subset of `quests` (handle, stage, running/active/
// complete, and per-objective index + displayed/completed bits) into a
// self-describing, bounds-checked little-endian blob.
std::vector<u8> EncodeQuestUpdate(const std::vector<quest::QuestStatus>& quests);

// Inverse of EncodeQuestUpdate. Fills only the replicated fields and leaves
// text (name/log_entry/objective text) empty; the caller resolves those from
// its own definitions. Returns nullopt on a truncated or corrupt blob, never
// reading out of bounds.
std::optional<std::vector<quest::QuestStatus>> DecodeQuestUpdate(ByteSpan data);

// Server side: turns successive full quest snapshots into reliable deltas. Each
// Build call compares the live snapshot against the per-handle revision last
// sent and emits only the quests that changed, so an idle session produces an
// empty blob. ForceFull makes the next Build resend every quest (used when a
// client joins and needs the whole journal at once).
class QuestReplicator {
 public:
  // Returns the encoding of the quests that changed since the last Build, or an
  // empty vector when nothing changed. `snapshot` is the authoritative state
  // this tick (e.g. QuestSystem::AllStatuses()).
  std::vector<u8> Build(const std::vector<quest::QuestStatus>& snapshot);

  // The next Build resends the full snapshot regardless of revisions.
  void ForceFull() { force_full_ = true; }

 private:
  // Last revision we replicated for each handle, so unchanged quests drop out.
  base::UnorderedMap<quest::QuestHandle, u32> sent_revision_;
  bool force_full_ = false;
};

// Client side: decodes a QuestUpdate blob and hands each quest to `sink`, which
// the engine wires to QuestSystem::ApplyStatus. Returns false on a corrupt blob
// (the sink is then not called for any quest in it). A blob encoding zero
// quests is valid and simply invokes the sink zero times.
bool ApplyQuestUpdate(
    ByteSpan data,
    const std::function<void(const quest::QuestStatus&)>& sink);

}  // namespace rec::net

#endif  // RECREATION_NET_QUEST_REPLICATION_H_
