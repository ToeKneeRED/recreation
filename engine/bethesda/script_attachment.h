#ifndef RECREATION_BETHESDA_SCRIPT_ATTACHMENT_H_
#define RECREATION_BETHESDA_SCRIPT_ATTACHMENT_H_

#include <string>
#include <vector>

#include "core/types.h"

namespace rec::bethesda {

// Parsed VMAD subrecord: the Papyrus scripts attached to a form plus the
// property values the editor baked in. This is the bridge from plugin data to
// the VM: the engine reads it to know which compiled scripts to instantiate on
// a form and how to seed their properties. Little-endian, like all plugin data.

// A scripted reference to another form, resolved later against the load order.
struct ScriptObjectValue {
  u32 form_id = 0;   // raw form id (plugin-local high byte), 0 = none
  u16 alias_id = 0;  // quest alias index, 0xffff = none
};

struct ScriptProperty {
  std::string name;
  u8 type = 0;     // 1 object, 2 string, 3 int, 4 float, 5 bool, 11-15 arrays
  u8 status = 0;

  i32 int_value = 0;
  f32 float_value = 0;
  bool bool_value = false;
  std::string string_value;
  ScriptObjectValue object_value;

  std::vector<i32> int_array;
  std::vector<f32> float_array;
  std::vector<u8> bool_array;
  std::vector<std::string> string_array;
  std::vector<ScriptObjectValue> object_array;
};

struct ScriptEntry {
  std::string name;  // compiled script object name (scripts/<name>.pex)
  u8 status = 0;
  std::vector<ScriptProperty> properties;
};

struct ScriptAttachment {
  i16 version = 0;
  i16 object_format = 0;
  std::vector<ScriptEntry> scripts;
};

// Parses a VMAD subrecord body. Only the script list is read (the per-record
// fragment data that can follow it is record-type specific and skipped).
// Returns false on a malformed or truncated body.
bool ParseScriptAttachment(ByteSpan vmad, ScriptAttachment* out);

}  // namespace rec::bethesda

#endif  // RECREATION_BETHESDA_SCRIPT_ATTACHMENT_H_
