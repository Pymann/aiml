Was fehlt für sys.bundle / sys.exec
1. Aktuelle Implementierung (Ist-Zustand)
Code-Typ — vollständig implementiert in code_impl.cpp:


Struct: { i8* id, i8* source, i64 src_len, i8* entry }
Methoden: contains(), size(), toString(), merge() — alle funktionieren.

sys.bundle — nicht implementiert (kein Handler in builtin_registry.cpp, kein Intercept in ast_compiler_call.cpp)

sys.load_bundle — nicht implementiert

sys.exec — nicht implementiert

Infrastruktur existiert bereits in context.h:

FunctionInfo::sourceYaml — YAML-Dump des Funktions-AST
FunctionInfo::referencedTypes — transitive Typ-Abhängigkeiten
ClassInfo::sourceYaml / ClassInfo::referencedTypes — dasselbe für Klassen
ctx.bundleTrackingTarget — wird gesetzt während Kompilierung, tracked Typ-Abhängigkeiten
2. Spec-Probleme (29_serialize.md)
Inkonsistenz: Code-Struct-Definition stimmt nicht mit Implementierung überein:

Spec sagt	Impl hat
id: string (SHA-256)	id: i8* — immer leer ""
start_call: map (AST-Node)	entry: i8* — einfacher Funktionsname als String
chunks: list<string>	source: i8* + src_len: i64 — kein Chunks-Array
Unklare Semantik:

Was passiert bei sys.bundle im AOT-Modus (nicht JIT)? Macht Ausführen zur Laufzeit keinen Sinn.
sys.load_bundle — "lädt Definitionen in aktuelle Umgebung" — das bedeutet dynamische Rekompilierung des YAML zur Laufzeit: extrem komplex, vermutlich nur in JIT-Modus sinnvoll.
Enum-Definitionen haben kein sourceYaml → können sie überhaupt gebundelt werden?
Was passiert bei sys.bundle auf eine Funktion die externe C-Bibliotheken nutzt?
3. Was fehlt konkret (Implementierung)
sys.bundle (Compile-Time-Operation):

Handler in builtin_registry.cpp als FuncHandler
Selector-Argument: string-Literal zur Kompilierzeit
Dependency-Closure-Algorithmus: Ziel + referencedTypes rekursiv → alle sourceYaml zusammensammeln
Code-Objekt mit zusammengesetztem YAML als source + entry = selector erstellen
sys.exec (JIT-only):

Handler in builtin_registry.cpp
Code.source YAML parsen + in neuem AIML-Compiler-Kontext ausführen (via params.compileCallback)
Rückgabewert? Fehlerbehandlung?
sys.load_bundle — unklar ob wirklich nötig. Alternativ: nur sys.exec implementieren.

4. Realistischer Plan
Bevor wir implementieren, sollte die Spec folgende Fragen beantworten:

Soll sys.exec / sys.load_bundle nur im JIT-Modus funktionieren oder auch AOT?
Soll sys.bundle Enums einschließen?
Was ist der Rückgabewert von sys.exec?
Brauchen wir sys.load_bundle überhaupt, oder reicht sys.exec allein?
Soll der Code-Typ die Spec-Definition (mit start_call: map, chunks) bekommen — oder die einfachere Implementierung (source: string, entry: string) in der Spec dokumentieren?
Soll ich die Spec zuerst präzisieren und dann implementieren?

Genau, warum du das Code Object anders implementiert hast als spezifiziert habe ich nicht verstanden, die idee war, dass man aiml-pieces auf chunks pushen kann. Natürlich muss man auch einen start_call angeben können, damit man den entrypoint mit parametern kennen kann. Natürlich ist eine Liste von Strings nicht unbedingt nötig, aber es representiert im prinzip ein aiml file, dass auch eine liste von instruktionen ist. Im chunks sind diese dann aber serialisiert, bzw der ursprüngliche yaml string. Wir haben nun, dass problem, dass wenn ein Code-Objekt z.b. Klasse y benutzt, dann müssen eventuell gesetzte werte natürlich mit im Code-Objekt ablegen. weshalb wir eigentlich 2 chunks brauchen, einmal code_chunk und einmal data_chunk. Oder man tut alles in code_chunk mit direkten serialise befehlen? natürlich soll das auch im AOT gehen, dann müssen wir die libaiml.so mitlinken, die wir bereits gebaut haben. Wieso haben enum-defintionen kein source-yaml? Das kann und muss dann wohl gesetzt werden. DIe finale Idee ist, dass z.b. ein Agent direkt über Netzwerk ein Code-Object gestreamt bekommt und ausführen kann. Wie z.b. ein jenkins-agent, bloß groovy und cps trafos sind abartig. Bei nutzung externe C-Libs müssen sie bei der Ausführung eben auch wieder vorhanden sein, wir schicken ja direkt aiml code im Code-Object hin und her, wieso ist das ein problem? Dann analysiere mal weiter mit meiner Eingabe!
Sehr klärend. Lass mich das jetzt systematisch analysieren:

Architektur-Analyse: Code-Objekt & sys.bundle/exec
1. Code-Struct: Was die Spec meinte vs. Impl
Die Implementierung hat das Konzept falsch vereinfacht. Korrekt:


__aiml_code_t:
  id:          string   → SHA-256 von code_chunk (Fingerprint)
  start_call:  string   → serialisierter YAML-Call-Node: '{"call":["worker_task",...]}'
  code_chunk:  string   → AIML-Definitionen (enum, class, def — statisch)
  data_chunk:  string   → AIML-Set-Statements (laufzeitstatus, via sys.deserialize)
Warum zwei Chunks? Genau richtig erkannt:


# code_chunk enthält:
- class: { name: Payload, members: [[id, i32]] }
- def: { name: worker_task, args: [[p, Payload]], body: [...] }

# data_chunk enthält (serialisierter Zustand):
- decl: [p, Payload]
- set: [p, { call: [sys.deserialize, '{"__type":"Payload","id":42}', "Payload"] }]

# start_call:
'{"call": ["worker_task", "p"]}'
Wenn ein Agent das erhält: code_chunk compilieren → data_chunk ausführen → start_call aufrufen. ✓

2. Was in der Impl fehlt/falsch ist
code_impl.cpp — aktueller Struct:


// IST (falsch):
{ i8* id, i8* source, i64 src_len, i8* entry }

// SOLL:
{ i8* id, i8* start_call, i8* code_chunk, i8* data_chunk }
Methoden die noch fehlen:

push(chunk: string) → hängt an code_chunk an (für manuelles Aufbauen)
push_data(chunk: string) → hängt an data_chunk an
set_entry(call_yaml: string) → setzt start_call
Enum-sourceYaml fehlt komplett:


// context.h hat für Enums nur:
std::map<std::string, std::map<std::string, int>> enums;
// Kein Platz für die YAML-Quelle!

// Brauchen wir:
std::map<std::string, std::string> enumSources;  // name → YAML-String
In ast_compiler.cpp muss beim Verarbeiten eines enum:-Nodes ctx.enumSources[name] = yamlDump gesetzt werden.

3. sys.bundle — Was das tun muss

# Syntax-Optionen:
- set: [pkg, { call: [sys.bundle, "worker_task"] }]
# oder mit explizitem Aufruf:
- set: [pkg, { call: [sys.bundle, "worker_task", "p", some_payload] }]
# oder auf Instanz (für Daten-Bundle):
- set: [pkg, { call: [sys.bundle, p] }]
Compile-Time-Operation (Intercept wie sys.serialize):


1. Selector auflösen → ctx.functions["worker_task"] oder ctx.classes["Payload"]
2. sourceYaml holen
3. Transitive referencedTypes sammeln:
   - für jeden referenced type: sourceYaml + rekursiv deren deps
   - enums: ctx.enumSources[name]
4. code_chunk = join(alle sourcYamls)
5. data_chunk = "" (initial leer; bei Instanz-Bundle: sys.serialize der Felder)
6. id = SHA-256 von code_chunk (oder simpler Hash für jetzt)
7. start_call = serialisierter YAML-Call
8. Code-Objekt erstellen
Instanz-Bundle (sys.bundle(p) wobei p: Payload):


code_chunk = Payload.sourceYaml + transitive deps
data_chunk = generierter AIML-Code:
  "- decl: [p, Payload]\n- set: [p, {call: [sys.deserialize, '<json>', 'Payload']}]\n"
start_call = "" (kein automatischer Einstiegspunkt)
4. sys.exec — AOT via libaiml.so
Der Schlüssel ist richtig: wir schicken AIML-Quellcode, kein IR. Der Empfänger braucht nur:

libaiml.so (AIML-Compiler+Runtime als Shared Library)
Dieselben C-Bibliotheken (yaml-cpp, LLVM-18) wie der Sender
libaiml.so API (was wir bauen müssen):


// Öffentliche C-API für AOT sys.exec:
int aiml_exec(const char* code_chunk, const char* data_chunk, const char* start_call);
int aiml_load(const char* code_chunk);   // für sys.load_bundle
sys.exec Ablauf:


JIT-Modus:  ctx.params.compileCallback(code_chunk + "\n" + data_chunk + "\n" + start_call_as_instr)
AOT-Modus:  dlopen("libaiml.so") → aiml_exec(code, data, call) → dlclose()
5. Spec muss diese Punkte präzisieren
Die aktuelle spec/29_serialize.md muss folgendes klären:

Code-Struct (die reflektierte Definition stimmt nicht mit der Impl überein):

- class:
    name: Code
    members:
      - [id,         string]   # SHA-256 von code_chunk
      - [start_call, string]   # YAML-serialisierter Call-Node
      - [code_chunk, List<string>]   # AIML-Defs (enum/class/def)
      - [data_chunk, List<string>]   # AIML-State-Inits (decl/set via deserialize)
    methods:
      - set_entry(call_yaml: string) → void   # setzt start_call
      - contains(name: string)       → bool
      - toString()                   → string # = code_chunk + data_chunk
      - merge(other: Code)           → Code
sys.bundle:

Enums werden mit ihrer Definition eingeschlossen
Externe C-Libs: nicht im Bundle — Empfänger braucht dieselbe Laufzeitumgebung
AOT-Modus: funktioniert via libaiml.so
sys.exec:

JIT: direkt im selben Prozess
AOT: via libaiml_exec() aus libaiml.so
Rückgabewert: i32 (Exit-Code)