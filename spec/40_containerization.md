# Chapter 40 — Containerization

> **Guiding principle:** In AIML a container is not external infrastructure (no Docker daemon,
> no Kubernetes). A container is the logical evolution of `sys.exec`:
> instead of executing code in the same process, it runs in an **isolated environment**
> — with its own filesystem, its own network stack, and its own resource limits.
> The code object (`Code`) from Chapter 29 *is* the container image.

---

## 1. What Is a Container — and Why Not Docker?

### The Problem with External Container Runtimes

Docker, Podman, containerd — all these tools solve the same problem in different
ways: they isolate processes from each other so that a faulty or malicious process
cannot affect the rest of the system. But in doing so they introduce a massive
abstraction layer:

```
Application → Docker SDK → Docker Daemon → containerd → runc → Linux Kernel
```

Every layer has its own protocol overhead, its own configuration language
(Dockerfile, docker-compose.yml, Kubernetes YAML), its own source of errors.

### AIML's Approach: Container as a Language Concept

In AIML, isolation is not an external layer but a parameter of `sys.exec`.
The `new_context` field in the `Code` struct (already defined in Chapter 29) was
reserved from the start for exactly this purpose:

```
sys.exec(pkg)                    → shared context (current process, same memory)
sys.exec(pkg, {isolate: true})   → isolated context (Linux namespace, new process)
container.run(pkg, options)      → full container lifecycle management
```

This means: every `Code` object is **already a portable container image**.
It contains all dependencies (via `sys.bundle`), a defined entry point
(`start_call`), and optionally initial data (`data_chunks`). It does not need to be
registered, tagged, or pushed anywhere — it is a normal AIML variable.

---

## 2. The `container` Namespace

`container` is a **reserved namespace** in AIML, analogous to `db`, `sys`, `io`.
It encapsulates all container operations and communicates directly with the Linux kernel APIs,
without a daemon process in between.

```aiml
# Wrong — compiler error:
- def:
    name: container.run   # "container" is reserved

# Correct:
- decl: [c, Container]
- set:  [c, { call: [container.run, my_pkg, { memory: "256mb" }] }]
```

---

## 3. How Container Isolation Works on Linux

To understand the AIML approach, it helps to know what the Linux kernel
actually provides for containerization. There are **three independent mechanisms**
that together make up a "container":

### 3.1 Namespaces — Isolating the View of the System

A **namespace** is a kernel abstraction that gives a process its own,
isolated view of a system area. There are six namespace types:

| Namespace | What is isolated | Example |
|-----------|------------------|---------|
| `PID`     | Process IDs | The container process has PID 1 from its own perspective |
| `NET`     | Network interfaces, routing, firewall | Own `eth0`, own IP address |
| `MNT`     | Filesystem hierarchy | Container only sees its own root filesystem |
| `UTS`     | Hostname, domain | Container can have its own hostname |
| `IPC`     | Shared memory, message queues | Processes in the container cannot access host IPC |
| `USER`    | User and group IDs | Root in container ≠ root on the host |

AIML creates these namespaces via `clone(2)` with the corresponding flags — no
Docker daemon, no OCI runtime, only direct syscalls.

### 3.2 cgroups — Resource Limits

**Control Groups (cgroups)** limit how many system resources a process
(or group of processes) may consume. Without cgroups a container could
occupy arbitrary amounts of RAM and crash the host.

AIML writes cgroup limits to `/sys/fs/cgroup/` — this is a normal filesystem API.

### 3.3 Overlay Filesystem — Layer Model

An **overlay filesystem** combines multiple directories (layers) into a single
visible filesystem. The lower layer is **read-only** (the "image"), the upper
layer is **writable** (container-specific changes). When the container is deleted,
only the upper layer is removed — the image remains untouched.

In AIML the overlay is built automatically when a container starts.
The AIML image format is not a tar ball like Docker — it is a directory
with AIML binaries and a `manifest.aiml` that defines the entry point.

---

## 4. Container Lifecycle

### 4.1 Starting a Container

```aiml
# Step 1: Create a code object (the "image"):
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

# Step 2: Start container:
- decl: [c, Container]
- set: [c, {
    call: [
      container.run, image,
      {
        # Resource limits:
        memory:    "256mb",
        cpu:       "0.5",          # 50% of one CPU core
        pids:      100,            # max. 100 processes/threads

        # Network:
        net:       "bridge",       # Own virtual network interface

        # Filesystem:
        mounts:    [
          "/host/data:/container/data:ro",   # read-only mount
          "/tmp/logs:/container/logs:rw"     # read-write mount
        ],
        tmpfs:     ["/tmp"],        # In-memory filesystem for /tmp

        # Metadata:
        name:      "request-handler-1",
        env:       { LOG_LEVEL: "INFO", PORT: "8080" }
      }
    ]
  }]
```

### 4.2 Communicating with the Container

Containers communicate via **channels** (Chapter 27) or over the **network** (Chapter 32).
Channels are the preferred way within the same host — they are faster
than network sockets and type-safe.

```aiml
# Container receives input via a channel:
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

# Host sends requests into the container:
- decl: [req, WebRequest]
- set: [req.path, "/api/users"]
- set: [req.method, "GET"]
- call: [input_chan.send, req]

# Host reads response:
- decl: [response, string]
- set: [response, { call: [output_chan.recv] }]
- print: ["Response: ${response}"]
```

### 4.3 Monitoring the Container

```aiml
# Wait until container finishes:
- decl: [exit_code, i32]
- set: [exit_code, { call: [c.wait] }]
- print: ["Container exited with code: ${exit_code}"]

# Non-blocking polling:
- decl: [running, bool]
- set:  [running, { call: [c.is_running] }]

# Query resource usage:
- decl: [stats, ContainerStats]
- set:  [stats, { call: [c.stats] }]
- print: ["Memory: ${stats.memory_mb}MB, CPU: ${stats.cpu_percent}%"]

# Read logs:
- decl: [logs, string]
- set:  [logs, { call: [c.logs, {lines: 50}] }]

# Stop container:
- call: [c.stop, {timeout: 10}]   # Waits 10s, then SIGKILL
- call: [c.kill]                   # Immediate SIGKILL
```

---

## 5. Network Modes

### 5.1 Bridge (Default)

Each container receives a virtual network interface (`veth` pair) attached to
a virtual bridge on the host. Containers can communicate with each other
and with the host, but not directly with the external network —
unless port forwarding is configured.

```aiml
- set: [c, {
    call: [container.run, image, {
      net: "bridge",
      ports: ["8080:80", "9090:443"]   # host_port:container_port
    }]
  }]
```

### 5.2 Host

The container shares the host's network namespace. No overhead from
virtual interfaces — maximum network performance, but no isolation.

```aiml
- set: [c, { call: [container.run, image, { net: "host" }] }]
```

### 5.3 None

No network. Only for offline processing (batch jobs, crypto operations).

```aiml
- set: [c, { call: [container.run, image, { net: "none" }] }]
```

### 5.4 Container-to-Container (Service Discovery)

Multiple containers can be grouped into a **pod**. Containers
in the same pod share a network namespace and can communicate via `localhost`
— without port forwarding, without DNS.

```aiml
- decl: [pod, Pod]
- set:  [pod, { call: [container.pod, "my-service"] }]

- call: [pod.add, frontend_image, {name: "frontend"}]
- call: [pod.add, backend_image,  {name: "backend",  net_alias: "api"}]
- call: [pod.add, db_image,       {name: "database",  net_alias: "db"}]

- call: [pod.start]

# Frontend container can address "api:8080" and "db:5432" directly
```

---

## 6. Image Management

### 6.1 AIML Images vs. OCI Images

An AIML container image is a **`Code` object**. It has no Dockerfile,
no registry pull, no layer cache on disk. It lives as a variable
in the running program or is persisted via `sys.serialize`.

Optionally AIML images can be exported to the **OCI format** (compatible with Docker, Podman)
— for interoperability with existing infrastructure:

```aiml
# Export as OCI image:
- call: [container.export_oci, my_image, {
    tag:    "my-app:v1.2",
    output: "/tmp/my-app.tar"
  }]

# Import an OCI image (e.g. from Docker Hub):
- decl: [nginx_image, Code]
- set:  [nginx_image, { call: [container.import_oci, "nginx:alpine"] }]
```

### 6.2 Image Composition

Since images are normal `Code` objects, they can be composed with `Code.merge`
— this is the AIML equivalent of multi-stage builds:

```aiml
# Base image (e.g. standard library functions):
- decl: [base, Code]
- set:  [base, { call: [sys.bundle, "stdlib_core"] }]

# Application image:
- decl: [app, Code]
- set:  [app, { call: [sys.bundle, "my_service"] }]

# Merge — like a Dockerfile "FROM base":
- decl: [final_image, Code]
- set:  [final_image, { call: [base.merge, app] }]

- decl: [c, Container]
- set:  [c, { call: [container.run, final_image, {}] }]
```

---

## 7. Full Example: Horizontal Scaling

```aiml
# Web service that scales horizontally under load:

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

# Load balancer + auto-scaling:
- decl: [worker_image, Code]
- set:  [worker_image, { call: [sys.bundle, "worker"] }]

- decl: [req_chan,  chan<HttpRequest>]
- decl: [resp_chan, chan<HttpResponse>]

- decl: [workers, list<Container>]
- decl: [scale,   i32]
- set:  [scale, 4]   # Start 4 worker containers

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

# Send requests — automatically distributed across workers (channel semantics):
- decl: [req, HttpRequest]
- set: [req.method, "GET"]
- set: [req.path,   "/api/data"]
- call: [req_chan.send, req]

- decl: [resp, HttpResponse]
- set:  [resp, { call: [resp_chan.recv] }]
- print: ["Status: ${resp.status}, Body: ${resp.body}"]
```

---

## 8. Security

### 8.1 User Namespace Mapping

By default AIML code runs in the container as an **unprivileged user**,
even if the code object was compiled as root. The user namespace maps
UIDs inside the container to unprivileged UIDs on the host:

```aiml
- set: [c, {
    call: [container.run, image, {
      user:        "nobody",           # Which user inside the container
      user_map:    "1000:100000:65536" # container-UID → host-UID range
    }]
  }]
```

### 8.2 Capability Dropping

Linux processes have "capabilities" — granular superuser privileges. AIML
drops all unnecessary capabilities by default:

```aiml
- set: [c, {
    call: [container.run, image, {
      capabilities: {
        drop: ["ALL"],               # Remove all capabilities
        add:  ["NET_BIND_SERVICE"]   # Only allow binding port < 1024
      }
    }]
  }]
```

### 8.3 Seccomp Filter

A **seccomp filter** (Secure Computing Mode) restricts which syscalls
a process may invoke at all. AIML knows the syscalls that a
`Code` object needs and can automatically generate a minimal profile:

```aiml
- set: [c, {
    call: [container.run, image, {
      seccomp: "auto"   # Compiler automatically analyzes required syscalls
      # or:
      seccomp: "strict" # Only read, write, exit, sigreturn
    }]
  }]
```

---

## 9. Integration with Other AIML Features

### 9.1 Hot-Reload Across Container Boundaries

A running container can receive a new `Code` object with a higher function version
via `sys.exec`. The hot-reload system (Chapter 30) works even when the update
bundle is transmitted over the network (Chapter 32):

```aiml
# Host sends update to running container:
- decl: [update_pkg, Code]
- set:  [update_pkg, { call: [sys.bundle, "handle_request"] }]  # v2

# Container receives and performs hot-reload:
- call: [update_chan.send, update_pkg]
# Inside container: sys.exec(update_pkg) → hot-reload handle_request v1 → v2
```

### 9.2 AionDB Across Container Boundaries

A `Store<T>` can be mounted as a **shared volume** between containers.
Since AionDB uses zero-copy reads via mmap, this works across
process boundaries — as long as both processes read the segment (not write
without explicit synchronization):

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

## 10. Open Design Questions (Future Work)

- **Rootless Containers**: Containerization without root privileges (user-namespace-only).
- **Checkpoint/Restore**: Pause a running container, write state to disk,
  resume on another host (CRIU integration).
- **WASM Target**: `sys.bundle` → WebAssembly instead of Linux container. Same
  isolation semantics, browser- and edge-compatible.
- **Quota on AionDB**: Measure and limit DB accesses per container.
- **Network Policies**: Fine-grained rules for which containers may communicate with each other.
