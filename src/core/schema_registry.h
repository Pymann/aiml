#pragma once
#include "context.h"
#include <cstdint>

// Maximum number of fields tracked per class/model.
static constexpr int AIML_MAX_FIELDS = 256;

// Per-field metadata stored in the Schema-Registry.
// NOTE: Field offsets are NOT stored here — they are baked as GEP indices
// at compile time and remain stable under the Append-Only constraint.
struct AimlFieldInfo {
    const char* name;  // Field name (for Append-Only validation in exec_impl)
};

// Schema entry for one class or model.
// Stored as a global in the binary so exec_impl can read it at runtime.
struct AimlClassSchema {
    const char*   name;
    int32_t       version;
    int32_t       field_count;
    int32_t       packed_size;  // Actual sum of field sizes (LLVM DataLayout)
    int32_t       padded_size;  // Next power-of-2 of packed_size → calloc size
    AimlFieldInfo fields[AIML_MAX_FIELDS];
};

namespace AIML {

// Emits @__aiml_schemas and @__aiml_schema_count after all class/model definitions.
// Must be called at the end of compile(), after registerTypes() has run.
void emitSchemaRegistry(CompilerContext& ctx);

} // namespace AIML
