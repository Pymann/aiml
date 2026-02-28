#pragma once

// Set the active CompilerContext and ExecutionEngine for aiml_exec_code.
// Called from jitRun() before/after engine->runFunction().
extern "C" void aiml_set_active_context(void* ctx, void* ee);

// Execute a Code object (produced by sys.bundle).
// Returns: 0=success, -1=error, -2=no active context/engine
extern "C" int aiml_exec_code(void* code_obj);

// Fptable lookup and update — also usable from future libaiml.
// aiml_fptable_find: returns index of named function in fptable (-1 = not found)
// aiml_fptable_update: writes fn_ptr into fptable[func_idx][version-1] and
//                      atomically updates fptable_current[func_idx]
extern "C" int  aiml_fptable_find(const char* name);
extern "C" void aiml_fptable_update(int func_idx, int version, void* fn_ptr);
