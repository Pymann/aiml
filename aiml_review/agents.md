# Agenten & Remote-Ausführung (Entwurf)

## Überblick
AIML eignet sich durch seine Struktur (YAML = Code) hervorragend für verteilte Systeme (Agenten). 
Die Herausforderung ist Sicherheit und Stabilität, wenn Code bewegt oder zur Laufzeit ausgetauscht wird ("Hot-Swapping").

**Ziel:** Der Entwickler schreibt "normalen" Code. Der Compiler übernimmt die komplexe Absicherung.

## Problem 1: Fehlender Kontext (Unbekannte Variablen)
**Szenario:** Ein Agent schickt Code an einen anderen. Der Code greift auf `db.save()` zu. Wenn der Empfänger `db` nicht kennt, stürzt er ab.

### Lösung: Implizite Kontext-Injektion (Compiler-Magie)
Der Nutzer muss keinen `Context`-Parameter herumreichen. Der Compiler generiert diesen automatisch.

**User Code (Einfach):**
```aiml
- def:
    name: remote_task
    args: [[data, string]]
    body:
       # 'log' und 'config' sind einfach "da" (aus der Umgebung)
       - call: [log, "Task gestartet"]
       - set: [val, config.timeout]
```

**Compiler Generierung (Unter der Haube):**
Der Compiler wandelt dies um in eine Funktion, die einen versteckten `__ctx` Zeiger erhält.
Alle globalen Zugriffe (`log`, `config`) werden zu `__ctx->API.log` umgeschrieben.

```cpp
// Generiertes C++
void remote_task(Context* __ctx, String data) {
    __ctx->API.log("Task gestartet");
    auto val = __ctx->API.config.timeout;
}
```

### Lösung 1.2: Automatische Abhängigkeits-Analyse (Dependency Closure)
Das von dir genannte Problem: Was ist, wenn `remote_task` eine Klasse `CustomData` nutzt, die der Empfänger nicht kennt?

**Compiler-Strategie:**
Wenn Code serialisiert wird (`serialize(remote_task)`), analysiert der Compiler den AST rekursiv:
1.  Finde alle genutzten Typen (Klassen, Enums).
2.  Finde alle aufgerufenen Hilfsfunktionen.
3.  Prüfe, ob diese im "Standard-Kontext" des Empfängers (z.B. `system.*`) vorhanden sind.
4.  Falls **nicht**, packe den Quellcode (YAML) dieser Abhängigkeiten mit in das Paket.

**Das Code-Objekt (Struktur):**
Die exakte Struktur des Pakets wird durch die Klasse `Code` definiert (siehe `serialize.aiml`).
Es enthält:
*   `start_call`: Das AST-Statement, das ausgeführt werden soll (z.B. `{ call: [func, args] }`).
*   `content`: Der eigentliche Bytecode/Source (inklusive aller Abhängigkeiten wie `CustomData`).

Der Empfänger lädt dieses Bundle via `sys.load_bundle` oder `sys.exec`, wodurch alle Abhängigkeiten automatisch verfügbar sind.
Der Empfänger kompiliert erst den Content (inkl. Abhängigkeiten) und führt dann den `start_call` aus.

## Problem 2: Speichersicherheit & Versionierung
**Szenario:** Wir tauschen ein laufendes Objekt `PlayerV1` gegen `PlayerV2` aus.

### Die "Einfache" Lösung: Speicher-Puffer & In-Place Updates
Statt komplexer Indirektions-Tabellen für jeden Zugriff, planen wir **Puffer** im Speicher ein.

#### 1. Klassen-Versionierung & Memory Padding
Wenn wir eine Klasse definieren, reserviert der Compiler **zusätzlichen Speicher** (Padding) für zukünftige Versionen.
*   `Class V1`: Größe 64 Bytes + 64 Bytes Padding.
*   `Class V2`: Fügt `int new_field` hinzu. Größe 68 Bytes. Passt noch in den 128 Byte Slot!

**Vorteil:** Das Objekt kann **in-place** aktualisiert werden. Alte Zeiger bleiben gültig!

#### 2. Instanz-Updates durch Payload
Wenn neuer Code (Payload) eintrifft, enthält er oft auch neue Daten für existierende Instanzen.
*   Der `Deserializer` erkennt: "Aha, hier ist ein Update für Objekt ID `123`".
*   Er schreibt die neuen Werte direkt in den Speicher von Objekt `123`.
*   Da wir Padding haben, passen auch neue Felder rein, ohne dass wir das Objekt verschieben müssen.

#### 3. Funktions-Versionierung
Auch Funktionen erhalten eine Version (`v1`, `v2`).
*   Der **Wrapper** prüft: "Ist eine neuere Version (`v2`) verfügbar?"
*   Falls ja, wird der Funktionszeiger im Wrapper atomar auf `v2` umgebogen.
*   Der Aufrufer merkt davon nichts.

## Problem 3: Hot-Reload & Function Versioning
Wie in `hot_reload.md` detailliert beschrieben, nutzen wir eine **"Overwrite-In-Place"** Strategie statt indirekter Wrapper-Tabellen, um maximale Performance zu garantieren.

### 3.1 Pre-Allocation & Padding
Jede funktionale Einheit erhält bei der ersten Kompilierung mehr Speicher als nötig (Padding). Dies reserviert Platz für zukünftige Updates.

### 3.2 Update-Mechanismus (The Swap)
Wenn eine neue Version einer Funktion geladen wird:
1.  **Sicherung:** Der *alte* Code wird an eine neue Speicheradresse kopiert (für laufende Threads).
2.  **Overwrite:** Der *ursprüngliche* Einsprungpunkt wird atomar mit dem *neuen* Code überschrieben.
3.  **Resultat:** Alle existierenden Zeiger und Aufrufe landen sofort in der neuen Version, ohne Overhead durch Indirektion.
4.  **Cleanup:** Alter Speicher kann via `sys.free_version` freigegeben werden, sobald sicher ist, dass keine Threads mehr darin laufen.

## Zusammenfassung
- **User Experience**: Schreibt einfachen, imperativen Code.
- **Compiler**: 
  - Injiziert `Context` automatisch.
  - Verwandelt `ptr` in sichere `Handles`.
  - Generiert `Wrapper` für Hot-Reload.
- **Sicherheit**: Keine "Dangling Pointers", kein Zugriff auf falschen Speicher, vollautomatische Versionierung.
