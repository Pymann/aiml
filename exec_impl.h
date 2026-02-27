#pragma once

// Set the active CompilerContext and ExecutionEngine for aiml_exec_code.
// Called from jitRun() before/after engine->runFunction().
extern "C" void aiml_set_active_context(void* ctx, void* ee);

// Execute a Code object (produced by sys.bundle).
// Returns: 0=success, -1=error, -2=no active context/engine
extern "C" int aiml_exec_code(void* code_obj);
