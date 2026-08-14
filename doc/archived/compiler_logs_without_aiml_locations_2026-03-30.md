# Compiler Logs Without AIML Source Locations

Datum: 2026-03-30

## Ziel

Diese Liste identifiziert Log-/Diagnostikstellen im AIML-Projekt, die aktuell keine AIML-Datei/Zeile in der Meldung tragen. Fokus sind vor allem Compiler- und Semantikfehler, bei denen eine AIML-Quelle erwartet wird.

## Methodik

- Durchsucht wurden `compiler.cpp`, `src/core`, `src/sys`.
- Gesucht wurden `LOG_ERROR`, `LOG_WARNING`, `LOG_WARN`, `LOG_INFO`.
- Als "mit AIML-Location" zaehlen hier primaer Stellen, die `ctx.fmtLoc(...)` oder `fmtLoc()` direkt in die Nachricht voranstellen.
- Infrastruktur-/Runtime-Logs ohne AIML-Quelle sind separat markiert; die sind nicht zwingend falsch, aber fuer Compilerdiagnostik nicht ausreichend.

## Grobe Zahlen

- Gefundene Log-Stellen insgesamt: `152`
- Davon direkt mit `fmtLoc(...)`/`fmtLoc()`: `12`

Das Delta ist groesser als der echte Handlungsbedarf, weil etliche Logs absichtlich LLVM-/Runtime-/Cache-Logs sind. Die fuer `sprint_task_17` relevanten Luecken liegen vor allem in den AST-/Semantik-Dateien.

## Prioritaet A: Echte Compilerdiagnostics ohne AIML-Datei/Zeile

Diese Stellen sollten bevorzugt auf `ctx.fmtLoc(node)` oder mindestens `ctx.fmtLoc()` umgestellt werden.

### `src/core/ast_compiler_expr.cpp`

- `112`: `Null LLVM value evaluated for initialization of member: ...`
  Fix: `ctx.fmtLoc(<expr/member node>)` voranstellen.
- `115`: `Failed to compile AST expression for initialization of member: ...`
  Fix: `ctx.fmtLoc(<expr/member node>)`.
- `122`: `Unknown class for new: ...`
  Fix: `ctx.fmtLoc(newExpr->sourceNode)`.
- `168`: `Invalid unknown literal (expected bool or number): ...`
  Fix: `ctx.fmtLoc(expr->sourceNode)`.
- `296`: `Array Slicing not supported on raw arrays yet.`
  Fix: `ctx.fmtLoc(call/expr source node)`.
- `322`: `Invalid list index: ...`
  Fix: `ctx.fmtLoc(expr->sourceNode)`.
- `553`: `Cannot apply eq/neq to struct type without __eq__: ...`
  Fix: `ctx.fmtLoc(bin->sourceNode)`.
- `583`: `Unknown binary operator: ...`
  Fix: `ctx.fmtLoc(bin->sourceNode)`.
- `597`: `Unary '&': operand must be a variable`
  Fix: `ctx.fmtLoc(unary->sourceNode)`.
- `599`: `Unary '&': undefined variable: ...`
  Fix: `ctx.fmtLoc(unary->sourceNode)`.
- `675`: `Unknown unary operator: ...`
  Fix: `ctx.fmtLoc(unary->sourceNode)`.
- `682`: `spawn: unknown function: ...`
  Fix: `ctx.fmtLoc(spawn->sourceNode)`.
- `758`: `spawn: thread_handle type not found`
  Fix: `ctx.fmtLoc(spawn->sourceNode)`.

### `src/core/ast_compiler_stmt.cpp`

- `43`: `Variable shadowing is not allowed: ...`
  Fix: `ctx.fmtLoc(decl->sourceNode)` oder `ctx.fmtLoc()`.
- `268`: `Failed to evaluate value expression for assignment to: ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `274`: `Cannot assign to const variable ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `385`: `Slice assignment is not supported yet: ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `400`: `Invalid list index for set: ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `476`: `Failed to evaluate return expression`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `480`: `Empty return statement in non-void function!`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.
- `577`: `AST Error: Try statement is missing a body. ...`
  Fix: `ctx.fmtLoc(stmt->sourceNode)`.

### `src/core/ast_compiler.cpp`

- `471`: `resolveImportCallback not bound! Cannot process import: ...`
  Fix: `ctx.fmtLoc(imp->sourceNode)`.
- `496`: `Unknown parent class: ...`
  Fix: `ctx.fmtLoc(node/class source)`.
- `510`: `Unknown model: ...`
  Fix: `ctx.fmtLoc(node/model source)`.
- `524`: `Duplicate member ... in class ...`
  Fix: `ctx.fmtLoc(memberNode)`.
- `530`: `Unknown type ... for member ...`
  Fix: `ctx.fmtLoc(memberNode)`.
- `681`: `Unknown type ... for field ... in model ...`
  Fix: `ctx.fmtLoc(fieldNode)`.
- `999`: `Variable shadowing is not allowed: function argument ...`
  Fix: `ctx.fmtLoc(fn->sourceNode)` oder arg-node.
- `1085`: `Undefined enum variant: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1147`: `Dash-index on non-list field: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1168`: `Invalid list index: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1228`: `Undefined property: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1262`: `Cannot access private member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1265`: `Cannot access protected member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1279`: `Dash-index on non-list field: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1305`: `Invalid list index: ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1337`: `Cannot access private member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1340`: `Cannot access protected member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.
- `1344`: `Cannot assign to const member ...`
  Fix: `ctx.fmtLoc(expr/source node)`.

### `src/core/ast_compiler_call.cpp`

- `258`: `Failed to evaluate argument ... for call to ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `324`: `Null LLVM value passed as argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `546`: `Unknown chained sub-method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `587`: `buffer<T>.get: unknown elem type ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `793`: `Unknown list method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `842`: `Unknown map method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `869`: `Unknown channel method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `960`: `get_by_key: no primary_key defined for ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1004`: `Unknown Store method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1085`: `Unknown Kv method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1151`: `Unknown VecStore method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1240`: `Unknown TimeSeries method: ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1345`: `sys.serialize: 'any' type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1353`: `sys.serialize: ptr<...> inner type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1368`: `sys.serialize: type ... is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1395`: `sys.deserialize: requires at least one argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1406`: `sys.deserialize: second argument must be a string literal type name`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1414`: `sys.deserialize: type cannot be inferred ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1424`: `sys.deserialize: 'any' type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1432`: `sys.deserialize: ptr<...> inner type is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1440`: `sys.deserialize: type ... is not serialisable`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1512`: `sys.bundle: requires 1 argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1539`: `sys.bundle: no YAML source found for ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1579`: `sys.bundle: unknown variable ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1586`: `sys.bundle: no YAML source found for type ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1631`: `sys.bundle: argument must be a string literal ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1640`: `sys.exec: requires exactly 1 argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1648`: `sys.exec: argument must be a Code variable ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1657`: `sys.exec: failed to evaluate Code argument`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1680`: `db.run_system: requires at least one argument ...`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1689`: `db.run_system: first arg must be a function name`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1695`: `db.run_system: function ... not found in module`
  Fix: `ctx.fmtLoc(call->sourceNode)`.
- `1706`: `db.run_system: need at least one store argument`
  Fix: `ctx.fmtLoc(call->sourceNode)`.

### `src/core/utils.cpp`

Diese Datei erzeugt viele Semantikfehler, hat aber aktuell keinen expliziten Node-Kontext im Log.

- `13`, `41`, `44`, `49`, `64`, `67`, `76`, `82`, `98`, `134`, `146`, `226`, `257`
  Fix: Funktionssignaturen erweitern, damit ein `CompilerContext&` plus optional `const YAML::Node*` oder `sourceLine` mitgegeben werden kann. Dann `ctx.fmtLoc(...)` voranstellen.

Anmerkung:
- Hier reicht nicht nur ein String-Prefix. Der Call-Stack muss den Source-Kontext bis in die Utility-Helfer weiterreichen.

### `src/core/context.h`

- `345`: `CRITICAL ERROR: getLLVMType called with invalid typeName ...`
- `432`: `Failed to resolve key/val types for map`
- `452`: `Separator ':' not found in map type content: ...`

Fix:
- Falls aus Benutzer-AIML ausloesbar: `fmtLoc()`/`fmtLoc(node)` voranstellen.
- Falls interne Invariante: explizit als interner Compilerfehler markieren, z. B. `internal compiler error`.

### `src/core/expression_builder.cpp`

- `50`: `Operator '...' is not allowed. Use '...' instead.`

Fix:
- Builder traegt bereits Linieninformationen; hier sollte der aktuelle Builder-Kontext mit Datei/Zeile in die Fehlermeldung einfliesen.

### `src/core/string_utils.h`

- `238`: `YAML Parse Error in String Expansion: ...`
- `246`: `String Expansion requires evaluator: ...`
- `280`: `Unmatched brace in expression: ...`

Fix:
- Falls diese Pfade aus AIML-Quelltext stammen, SourceLoc bis in den String-Expansion-Kontext weiterreichen.
- Sonst zumindest den umgebenden Selector/Funktionsnamen mitgeben.

### `src/core/builtin_registry.cpp`

- `2010`: `db.query: predicate function '...' not found`

Fix:
- `CompilerContext` oder Call-Site-Node bis in die Builtin-Aufloesung weiterreichen und `ctx.fmtLoc(call->sourceNode)` verwenden.

## Prioritaet B: Frontend-Compiler/Driver-Logs ohne AIML-Location

Diese Stellen sind teils berechtigt ohne AIML-Zeile, sollten aber, wo moeglich, den betroffenen AIML-Dateinamen oder Importpfad tragen.

### `compiler.cpp`

Stellen ohne AIML-Datei/Zeile:

- `227`: `Failed to parse import signatures: ...`
- `239`: `Could not open file: ...`
- `405`: `AST Builder failed to produce a valid program.`
- `408`: `YAML Parsing Error during AST Build: ...`
- `422`: `Could not open file: ...`
- `478`: `compileFromString: AST builder produced no program`
- `481`: `compileFromString: YAML parse error: ...`
- `1743`: `Import source not found: ...`

Fix:
- Dateipfad der betroffenen AIML-Datei immer explizit in die Diagnose.
- Wo ein YAML-Node vorhanden ist, spaeter auf `DiagnosticLoc` umstellen.

Nicht zwingend mit AIML-Zeile noetig, aber klar als Infrastruktur markieren:

- `72`, `511`, `666`, `684`, `692`, `704`, `711`, `719`, `725`, `733`
- `1599`, `1610`, `1617`, `1624`, `1639`, `1646`, `1655`, `1662`
- `1749`, `1793`, `1801`, `1803`

Empfehlung:
- Diese Logs nicht mit `fmtLoc()` mischen, sondern in der Meldung sauber unterscheiden:
  - `AIML compile error: ...`
  - `LLVM/JIT error: ...`
  - `Import cache info: ...`

## Prioritaet C: Runtime-/System-Logs ohne AIML-Location

Diese Logs sind aktuell nicht das Hauptproblem von `sprint_task_17`, weil sie keine direkten Compilerdiagnostics sind.

### `src/sys/process_runtime.cpp`

- `25`, `41`, `68`, `79`

### `src/sys/process_commands.h`

- `39`

### `src/sys/sys_commands.h`

- `24`

### `src/core/buffer_impl.h`

- `29`, `33`, `98`, `163`, `266`

Bewertung:
- Kein Bug im engeren Sinn, solange diese Meldungen als Runtime-/Systemfehler verstanden werden.
- Wenn langfristig AIML-Stacktraces/Diagnostics ausgebaut werden, sollten diese Pfade zumindest Funktionskontext oder Aufrufer benennen.

## Konkrete Bugfix-Strategie

### Stufe 1: Schnelle Compiler-Diagnostics

Direkt umstellen auf `ctx.fmtLoc(...)` in:

- `src/core/ast_compiler_expr.cpp`
- `src/core/ast_compiler_stmt.cpp`
- `src/core/ast_compiler.cpp`
- `src/core/ast_compiler_call.cpp`

Das bringt sofort den groessten Gewinn.

### Stufe 2: Utility-/Builtin-Kontext nachziehen

Refactor fuer:

- `src/core/utils.cpp`
- `src/core/builtin_registry.cpp`
- `src/core/string_utils.h`

Hier reicht ein lokaler Stringfix nicht. Der Source-Kontext muss architektonisch mitgereicht werden.

### Stufe 3: Driver vs. Diagnostics trennen

In `compiler.cpp` sollten Logs in zwei Klassen gesplittet werden:

- echte Benutzerdiagnostics mit AIML-Datei/Zeile
- Infrastruktur-/LLVM-/Cache-Logs ohne AIML-Zeile, aber klar gekennzeichnet

## Wichtigste erste Kandidaten fuer direkte Umsetzung

Wenn nur die schlimmsten Stellen zuerst gefixt werden sollen:

1. `src/core/ast_compiler_expr.cpp`
2. `src/core/ast_compiler_stmt.cpp`
3. `src/core/ast_compiler_call.cpp`
4. `src/core/ast_compiler.cpp`
5. `src/core/utils.cpp`

Diese fuenf Bereiche erklaeren den groessten Teil der heute "ortlosen" AIML-Compilerfehler.
