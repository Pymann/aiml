# Kapitel 40 — Containerisierung

> **Leitgedanke:** In AIML ist ein Container keine externe Infrastruktur (kein Docker-Daemon,
> kein Kubernetes). Ein Container ist die konsequente Weiterentwicklung von `sys.exec`:
> Statt Code im selben Prozess auszuführen, führt man ihn in einer **isolierten Umgebung**
> aus — mit eigenem Dateisystem, eigenem Netzwerk-Stack und eigenen Ressourcen-Limits.
> Das Code-Objekt (`Code`) aus Kapitel 29 *ist* das Container-Image.

---

## 1. Was ist ein Container — und warum kein Docker?

### Das Problem mit externen Container-Runtimes

Docker, Podman, containerd — all diese Tools lösen dasselbe Problem auf unterschiedliche
Weise: Sie isolieren Prozesse voneinander, damit ein fehlerhafter oder bösartiger Prozess
nicht den Rest des Systems beeinträchtigen kann. Aber sie fügen dabei eine massive
Abstraktionsschicht ein:

```
Anwendung → Docker SDK → Docker Daemon → containerd → runc → Linux Kernel
```

Jede Schicht hat ihren eigenen Protokoll-Overhead, ihre eigene Konfigurationssprache
(Dockerfile, docker-compose.yml, Kubernetes YAML), ihre eigene Fehlerquelle.

### AIML's Ansatz: Container als Sprachkonzept

In AIML ist die Isolierung keine externe Schicht, sondern ein Parameter von `sys.exec`.
Das `new_context`-Feld im `Code`-Struct (bereits in Kapitel 29 definiert) war von
Anfang an für genau diesen Zweck reserviert:

```
sys.exec(pkg)                    → shared context (aktueller Prozess, selber Speicher)
sys.exec(pkg, {isolate: true})   → isolated context (Linux-Namespace, neuer Prozess)
container.run(pkg, options)      → volles Container-Lifecycle-Management
```

Das bedeutet: Jedes `Code`-Objekt ist **bereits ein portables Container-Image**.
Es enthält alle Abhängigkeiten (via `sys.bundle`), einen definierten Einstiegspunkt
(`start_call`) und optional initiale Daten (`data_chunks`). Es muss nirgendwo
registriert, getaggt oder gepusht werden — es ist eine normale AIML-Variable.

---

## 2. Der `container`-Namespace

`container` ist ein **reservierter Namespace** in AIML, analog zu `db`, `sys`, `io`.
Er kapselt alle Container-Operationen und kommuniziert direkt mit den Linux-Kernel-APIs,
ohne einen Daemon-Prozess dazwischen.

```aiml
# Falsch — Compilerfehler:
- def:
    name: container.run   # "container" ist reserviert

# Richtig:
- decl: [c, Container]
- set:  [c, { call: [container.run, my_pkg, { memory: "256mb" }] }]
```

---

## 3. Wie Container-Isolierung unter Linux funktioniert

Um den AIML-Ansatz zu verstehen, ist es hilfreich zu wissen, was der Linux-Kernel
tatsächlich für Containerisierung bereitstellt. Es gibt **drei unabhängige Mechanismen**,
die zusammen einen "Container" ergeben:

### 3.1 Namespaces — Isolierung der Sicht auf das System

Ein **Namespace** ist eine Kernel-Abstraktion, die einem Prozess eine eigene,
isolierte Sicht auf einen Systembereich gibt. Es gibt sechs Namespace-Typen:

| Namespace | Was wird isoliert | Beispiel |
|-----------|------------------|---------|
| `PID`     | Prozess-IDs | Der Container-Prozess hat PID 1 aus seiner eigenen Sicht |
| `NET`     | Netzwerk-Interfaces, Routing, Firewall | Eigene `eth0`, eigene IP-Adresse |
| `MNT`     | Dateisystem-Hierarchie | Container sieht nur sein eigenes Root-Filesystem |
| `UTS`     | Hostname, Domain | Container kann eigenen Hostnamen haben |
| `IPC`     | Shared Memory, Message Queues | Prozesse im Container können nicht auf IPC des Hosts zugreifen |
| `USER`    | Benutzer- und Gruppen-IDs | Root im Container ≠ Root auf dem Host |

AIML erstellt diese Namespaces via `clone(2)` mit den entsprechenden Flags — kein
Docker-Daemon, kein OCI-Runtime, nur direkte Syscalls.

### 3.2 cgroups — Ressourcen-Limits

**Control Groups (cgroups)** begrenzen, wie viele Systemressourcen ein Prozess
(oder eine Gruppe von Prozessen) verbrauchen darf. Ohne cgroups könnte ein Container
beliebig viel RAM belegen und den Host zum Absturz bringen.

AIML schreibt cgroup-Limits in `/sys/fs/cgroup/` — das ist eine normale Dateisystem-API.

### 3.3 Overlay-Filesystem — Schichtenmodell

Ein **Overlay-Filesystem** kombiniert mehrere Verzeichnisse (Layer) zu einem einzigen
sichtbaren Dateisystem. Der untere Layer ist **read-only** (das "Image"), der obere
Layer ist **writable** (Container-spezifische Änderungen). Wenn der Container gelöscht
wird, wird nur der obere Layer entfernt — das Image bleibt unberührt.

In AIML wird das Overlay automatisch aufgebaut, wenn ein Container gestartet wird.
Das AIML-Image-Format ist kein tar-Ball wie bei Docker — es ist ein Verzeichnis
mit AIML-Binaries und einem `manifest.aiml`, das den Einstiegspunkt definiert.

---

## 4. Container-Lebenszyklus

### 4.1 Container starten

```aiml
# Schritt 1: Ein Code-Objekt (das "Image") erzeugen:
- class:
    name: WebRequest
    members: [[path, string], [method, string]]

- def:
    name: handle_request
    args: [[req, WebRequest]]
    return: void
    body:
      - print: ["Handling: ${req.method} ${req.path}"]

- decl: [image, Code]
- set:  [image, { call: [sys.bundle, "handle_request"] }]

# Schritt 2: Container starten:
- decl: [c, Container]
- set: [c, {
    call: [
      container.run, image,
      {
        # Ressourcen-Limits:
        memory:    "256mb",
        cpu:       "0.5",          # 50% eines CPU-Kerns
        pids:      100,            # max. 100 Prozesse/Threads

        # Netzwerk:
        net:       "bridge",       # Eigenes virtuelles Netzwerk-Interface

        # Dateisystem:
        mounts:    [
          "/host/data:/container/data:ro",   # read-only mount
          "/tmp/logs:/container/logs:rw"     # read-write mount
        ],
        tmpfs:     ["/tmp"],        # In-Memory-Filesystem für /tmp

        # Metadaten:
        name:      "request-handler-1",
        env:       { LOG_LEVEL: "INFO", PORT: "8080" }
      }
    ]
  }]
```

### 4.2 Mit dem Container kommunizieren

Container kommunizieren über **Channels** (Kapitel 27) oder über **Netzwerk** (Kapitel 32).
Channels sind dabei der bevorzugte Weg innerhalb desselben Hosts — sie sind schneller
als Netzwerk-Sockets und type-safe.

```aiml
# Container erhält Input über einen Channel:
- decl: [input_chan,  chan<WebRequest>]
- decl: [output_chan, chan<string>]

- decl: [c, Container]
- set: [c, {
    call: [
      container.run, image,
      {
        channels: {
          input:  input_chan,
          output: output_chan
        }
      }
    ]
  }]

# Host schickt Anfragen in den Container:
- decl: [req, WebRequest]
- set: [req.path, "/api/users"]
- set: [req.method, "GET"]
- call: [input_chan.send, req]

# Host liest Antwort:
- decl: [response, string]
- set: [response, { call: [output_chan.recv] }]
- print: ["Response: ${response}"]
```

### 4.3 Container überwachen

```aiml
# Warten bis Container beendet:
- decl: [exit_code, i32]
- set: [exit_code, { call: [c.wait] }]
- print: ["Container exited with code: ${exit_code}"]

# Nicht-blockierendes Polling:
- decl: [running, bool]
- set:  [running, { call: [c.is_running] }]

# Ressourcen-Nutzung abfragen:
- decl: [stats, ContainerStats]
- set:  [stats, { call: [c.stats] }]
- print: ["Memory: ${stats.memory_mb}MB, CPU: ${stats.cpu_percent}%"]

# Logs lesen:
- decl: [logs, string]
- set:  [logs, { call: [c.logs, {lines: 50}] }]

# Container stoppen:
- call: [c.stop, {timeout: 10}]   # Wartet 10s, dann SIGKILL
- call: [c.kill]                   # Sofortiger SIGKILL
```

---

## 5. Netzwerk-Modi

### 5.1 Bridge (Standard)

Jeder Container erhält ein virtuelles Netzwerk-Interface (`veth`-Paar), das an
eine virtuelle Bridge auf dem Host angeschlossen ist. Container können miteinander
und mit dem Host kommunizieren, aber nicht direkt mit dem externen Netzwerk —
es sei denn, man konfiguriert Port-Forwarding.

```aiml
- set: [c, {
    call: [container.run, image, {
      net: "bridge",
      ports: ["8080:80", "9090:443"]   # host_port:container_port
    }]
  }]
```

### 5.2 Host

Der Container teilt den Netzwerk-Namespace des Hosts. Kein Overhead durch
virtuelle Interfaces — maximale Netzwerk-Performance, aber keine Isolierung.

```aiml
- set: [c, { call: [container.run, image, { net: "host" }] }]
```

### 5.3 None

Kein Netzwerk. Nur für offline-Verarbeitung (Batch-Jobs, Crypto-Operationen).

```aiml
- set: [c, { call: [container.run, image, { net: "none" }] }]
```

### 5.4 Container-zu-Container (Service Discovery)

Mehrere Container können in einem **Pod** zusammengefasst werden. Container
im selben Pod teilen einen Netzwerk-Namespace und können über `localhost`
miteinander kommunizieren — ohne Port-Forwarding, ohne DNS.

```aiml
- decl: [pod, Pod]
- set:  [pod, { call: [container.pod, "my-service"] }]

- call: [pod.add, frontend_image, {name: "frontend"}]
- call: [pod.add, backend_image,  {name: "backend",  net_alias: "api"}]
- call: [pod.add, db_image,       {name: "database",  net_alias: "db"}]

- call: [pod.start]

# Frontend-Container kann "api:8080" und "db:5432" direkt ansprechen
```

---

## 6. Image-Management

### 6.1 AIML-Images vs. OCI-Images

Ein AIML-Container-Image ist ein **`Code`-Objekt**. Es hat kein Dockerfile,
kein Registry-Pull, keinen Layer-Cache auf der Festplatte. Es lebt als Variable
im laufenden Programm oder wird via `sys.serialize` persistiert.

Optional kann man AIML-Images in das **OCI-Format** (kompatibel mit Docker, Podman)
exportieren — für Interoperabilität mit bestehender Infrastruktur:

```aiml
# Export als OCI-Image:
- call: [container.export_oci, my_image, {
    tag:    "my-app:v1.2",
    output: "/tmp/my-app.tar"
  }]

# Import eines OCI-Images (z.B. von Docker Hub):
- decl: [nginx_image, Code]
- set:  [nginx_image, { call: [container.import_oci, "nginx:alpine"] }]
```

### 6.2 Image-Composition

Da Images normale `Code`-Objekte sind, können sie mit `Code.merge` zusammengesetzt
werden — das ist das AIML-Äquivalent zu Multi-Stage-Builds:

```aiml
# Base-Image (z.B. Standard-Library-Funktionen):
- decl: [base, Code]
- set:  [base, { call: [sys.bundle, "stdlib_core"] }]

# Anwendungs-Image:
- decl: [app, Code]
- set:  [app, { call: [sys.bundle, "my_service"] }]

# Zusammenführen — wie ein Dockerfile "FROM base":
- decl: [final_image, Code]
- set:  [final_image, { call: [base.merge, app] }]

- decl: [c, Container]
- set:  [c, { call: [container.run, final_image, {}] }]
```

---

## 7. Vollständiges Beispiel: Horizontale Skalierung

```aiml
# Web-Service der unter Last horizontal skaliert wird:

- class:
    name: HttpRequest
    members:
      - [method, string]
      - [path,   string]
      - [body,   string]

- class:
    name: HttpResponse
    members:
      - [status, i32]
      - [body,   string]

- def:
    name: worker
    return: void
    body:
      - decl: [req_chan,  chan<HttpRequest>]
      - decl: [resp_chan, chan<HttpResponse>]
      - set:  [req_chan,  { call: [sys.get_chan, "requests"] }]
      - set:  [resp_chan, { call: [sys.get_chan, "responses"] }]

      - while:
          condition: { const: true }
          body:
            - decl: [req, HttpRequest]
            - set:  [req, { call: [req_chan.recv] }]

            - decl: [resp, HttpResponse]
            - set: [resp.status, 200]
            - set: [resp.body, "OK: ${req.path}"]
            - call: [resp_chan.send, resp]

# Load-Balancer + Auto-Scaling:
- decl: [worker_image, Code]
- set:  [worker_image, { call: [sys.bundle, "worker"] }]

- decl: [req_chan,  chan<HttpRequest>]
- decl: [resp_chan, chan<HttpResponse>]

- decl: [workers, list<Container>]
- decl: [scale,   i32]
- set:  [scale, 4]   # Starte 4 Worker-Container

- decl: [i, i32]
- set:  [i, 0]
- while:
    condition: { call: [i < scale] }
    body:
      - decl: [c, Container]
      - set: [c, {
          call: [
            container.run, worker_image,
            {
              memory: "128mb",
              cpu:    "0.25",
              channels: { requests: req_chan, responses: resp_chan }
            }
          ]
        }]
      - call: [workers.push, c]
      - set: [i, { call: [i + 1] }]

# Anfragen senden — automatisch auf die Worker verteilt (Channel-Semantik):
- decl: [req, HttpRequest]
- set: [req.method, "GET"]
- set: [req.path,   "/api/data"]
- call: [req_chan.send, req]

- decl: [resp, HttpResponse]
- set:  [resp, { call: [resp_chan.recv] }]
- print: ["Status: ${resp.status}, Body: ${resp.body}"]
```

---

## 8. Sicherheit

### 8.1 User-Namespace-Mapping

Standardmäßig läuft AIML-Code im Container als **nicht-privilegierter Benutzer**,
selbst wenn das Code-Objekt als root kompiliert wurde. Der User-Namespace mappt
UIDs im Container auf nicht-privilegierte UIDs auf dem Host:

```aiml
- set: [c, {
    call: [container.run, image, {
      user:        "nobody",           # Als welcher User im Container
      user_map:    "1000:100000:65536" # Container-UID → Host-UID-Bereich
    }]
  }]
```

### 8.2 Capability-Dropping

Linux-Prozesse haben "Capabilities" — granulare Superuser-Rechte. AIML
entfernt standardmäßig alle nicht benötigten Capabilities:

```aiml
- set: [c, {
    call: [container.run, image, {
      capabilities: {
        drop: ["ALL"],               # Alle Capabilities entfernen
        add:  ["NET_BIND_SERVICE"]   # Nur Port < 1024 binden erlaubt
      }
    }]
  }]
```

### 8.3 Seccomp-Filter

Ein **Seccomp-Filter** (Secure Computing Mode) schränkt ein, welche Syscalls
ein Prozess überhaupt aufrufen darf. AIML kennt die Syscalls, die ein
`Code`-Objekt benötigt, und kann automatisch ein minimales Profil generieren:

```aiml
- set: [c, {
    call: [container.run, image, {
      seccomp: "auto"   # Compiler analysiert benötigte Syscalls automatisch
      # oder:
      seccomp: "strict" # Nur read, write, exit, sigreturn
    }]
  }]
```

---

## 9. Integration mit anderen AIML-Features

### 9.1 Hot-Reload über Container-Grenzen

Ein laufender Container kann via `sys.exec` ein neues `Code`-Objekt mit einer
höheren Funktionsversion erhalten. Das Hot-Reload-System (Kapitel 30) funktioniert
auch dann, wenn das Update-Bundle über das Netzwerk (Kapitel 32) übertragen wird:

```aiml
# Host sendet Update an laufenden Container:
- decl: [update_pkg, Code]
- set:  [update_pkg, { call: [sys.bundle, "handle_request"] }]  # v2

# Container empfängt und führt Hot-Reload durch:
- call: [update_chan.send, update_pkg]
# Im Container: sys.exec(update_pkg) → hot-reload handle_request v1 → v2
```

### 9.2 AionDB über Container-Grenzen

Ein `Store<T>` kann als **shared volume** zwischen Containern gemountet werden.
Da AionDB Zero-Copy-Reads über mmap verwendet, funktioniert das auch über
Prozessgrenzen hinweg — solange beide Prozesse das Segment lesen (nicht schreiben,
ohne explizite Synchronisation):

```aiml
- decl: [shared_db, Store<GameState>]
- set:  [shared_db, { call: [db.open, "/shared/game_state"] }]

- set: [c1, { call: [container.run, reader_image, {
    mounts: ["/shared:/shared:ro"]  # Reader: read-only
  }] }]

- set: [c2, { call: [container.run, writer_image, {
    mounts: ["/shared:/shared:rw"]  # Writer: read-write
  }] }]
```

---

## 10. Offene Designfragen (Future Work)

- **Rootless Containers**: Containerisierung ohne Root-Rechte (User-Namespace-only).
- **Checkpoint/Restore**: Laufenden Container pausieren, Zustand auf Disk schreiben,
  auf anderem Host fortsetzen (CRIU-Integration).
- **WASM-Target**: `sys.bundle` → WebAssembly statt Linux-Container. Gleiche
  Isolierungs-Semantik, browser- und edge-kompatibel.
- **Quota auf AionDB**: DB-Zugriffe pro Container messen und limitieren.
- **Netzwerk-Policies**: Feinkörnige Regeln, welche Container miteinander kommunizieren dürfen.
