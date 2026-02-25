#pragma once
#include "context.h"

// Registers all builtin types (buffer, process) and namespaces (sys, io, time)
// into the CompilerContext registry. Call once before compilation starts.
void registerBuiltins(CompilerContext& ctx);
