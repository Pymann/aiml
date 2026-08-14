# AOT-Build: Top-Level-Handler für uncaught Exceptions

Date: 2026-07-30
Status: backlog

## Kontext

2026-07-30 wurde die **JIT**-Ausführung (`aiml … -r`) sauber gemacht: eine
uncaught AIML-Exception wird jetzt am Top-Level in `AIMLCompiler::jitRun` von
einem `try/catch(...)` gefangen → druckt `Unhandled AIML exception: <message>`
nach stderr + `return 2` (statt `std::terminate`/Core-Dump). Die `message` wird
in `visitThrow` vor `__cxa_throw` via `aiml_note_thrown_message` in einer
thread-local abgelegt (siehe Memory `aiml_exception_propagation`).

**Offen:** Der **AOT**-Build (native Executable, z.B. der 9rooms-`game`-Server via
`exodus build -c …`) hat KEINEN äquivalenten Wrapper. Die kompilierte AIML-`main`
ist der Entry-Point; eine uncaught Exception unwindet aus `main` heraus →
harter Abbruch (`std::terminate`) ohne Diagnose. Für einen langlebigen Server ist
das gefährlich (ein einziger Bad-Data-Pfad killt den Prozess).

## Plan

- Beim AOT-Codegen die AIML-`main` NICHT direkt als C-`main` exportieren, sondern
  eine C++-`main`-Wrapper generieren/linken, die die AIML-`main` in
  `try { … } catch (...) { fprintf(stderr, "Unhandled AIML exception: %s\n",
  g_aiml_last_thrown_msg…); return 2; }` aufruft — analog zu `jitRun`.
  `aiml_note_thrown_message` + `g_aiml_last_thrown_msg` existieren bereits
  (`compiler.cpp`); für AOT müssen sie in die Runtime (`libaiml`) statt in den
  Compiler, damit das gelinkte Binary sie findet.
- Alternativ: `std::set_terminate`-Handler registrieren, der die zuletzt notierte
  Message druckt.

## Warum relevant

Blockiert die saubere Server-seitige Fehler-Behandlung bei der geplanten
Effect/Cost-Kaskaden-Validierung (9rooms, eigenes Backlog): dort MUSS der
create_card-RPC-Handler die Exception selbst fangen, weil der AOT-Server sonst
crasht. Ein AOT-Top-Level-Wrapper wäre das robustere Netz.
