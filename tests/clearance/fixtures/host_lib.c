/* host_lib.c — a HOST as a shared library (spec/65 F65-24, second route).
 *
 * spec/65 used to know only one route by which an ADDRESS reaches the script:
 * `sys.export_symbol` in the same process (probe 09).  A real host that has foreign
 * source text compiled cannot do that — it IS the compiler, and that one only knows
 * the script at runtime.  There the address comes from a preloaded
 * library, and name + level from the export table.
 *
 * This file is the smallest host that makes this testable.  cosmos doc/spec/108 F108-9
 * builds on it. */
int clearance_host_answer(void) { return 42; }

/* Offered at level 2: a session with clearance 7 must not name it,
 * although the address exists in the process.  That is the point — admission
 * depends on the table, not on the reachability of the symbol. */
int clearance_host_secret(void) { return 7; }
