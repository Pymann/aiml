# Chapter 32.6 — SSH Client

> **Design principle:** unlike WebSockets (§32.4), SSH is **not** reimplemented on top of
> `net.*`. The protocol carries authentication secrets and host identity; a hand-written
> implementation would put key exchange, cipher negotiation and host-key handling into this
> codebase, where every bug is a security bug. AIML therefore binds **libssh** (bound via
> `exodus pkg`, not installed system-wide) and exposes only what a caller needs.

The driving use case is remote provisioning: a controller logs into a node, uploads a binary
and runs privileged commands — see cosmos `doc/spec/94`. That shapes two API decisions that
look unusual and are deliberate: **host verification is a separate, explicit step** (§3), and
**stdin is writable** (§5), because a `sudo` password must never travel in argv.

---

## 1. Types

| Type          | LLVM  | Description |
|---------------|-------|-------------|
| `ssh_session` | `i8*` | Connection to one host. Carries the transport and, after §4, the authenticated user. |
| `ssh_channel` | `i8*` | One executed command on a session: stdin, stdout, stderr, exit code. |
| `ssh_test_server` | `i8*` | **Test-only** server (§7). Not for production use. |

---

## 2. Connecting

```aiml
- decl: [sess, ssh_session, { call: [ssh.connect, "10.0.0.7", 22, "deploy", 5000] }]
- if: { condition: { eq: [sess, null] }, then: [{ return: 1 }] }
```

| Call | Return | Description |
|------|--------|-------------|
| `ssh.connect(host, port, user, timeout_ms)` | `ssh_session` | TCP connect + key exchange. `null` on failure. **Does not authenticate** and leaves the session *unverified*. |
| `sess.close()` | — | Close transport, free resources. Safe on an already-closed session. |
| `sess.last_error()` | `string` | libssh's message for the last failed call — for diagnostics, never for control flow. |

`connect` performing the key exchange but **not** the authentication is what makes §3
possible: the host key exists at that point, the password does not yet.

---

## 3. Host verification — a separate step, on purpose

An unverified session **refuses** every `auth_*`, `exec` and `sftp_*` call. There is no flag
to skip this, and no default that accepts silently.

| Call | Return | Description |
|------|--------|-------------|
| `sess.fingerprint()` | `string` | Host key as `SHA256:<base64>` — the same form OpenSSH prints. Available right after `connect`. |
| `sess.accept_host(expected)` | `i32` | `1` = verified, session becomes usable. `0` = mismatch, session stays unusable. With `expected == ""` the current key is accepted (**trust on first use**); otherwise it must equal `expected`. |

```aiml
# Known host: the expected fingerprint decides.
- decl: [ok_host, i32, { call: [sess.accept_host, cfg_fingerprint] }]
- if: { condition: { neq: [ok_host, 1] }, then: [{ call: [log.error, "host key mismatch"] }, { return: 1 }] }

# First contact: show it, let a human confirm, then accept.
- decl: [shown, string, { call: [sess.fingerprint] }]
- call: [log.info, "host key: ${shown}"]
- decl: [ok_tofu, i32, { call: [sess.accept_host, ""] }]
```

**Why not a `known_hosts` file.** A file would make the decision invisible: a caller that
never thought about host identity would still connect. Here, `accept_host("")` is something
you have to write, and it reads like what it is. A UI can show the fingerprint before anyone
types a password — which is the point, since the password is what a man in the middle is
after.

---

## 4. Authentication

| Call | Return | Description |
|------|--------|-------------|
| `sess.auth_password(password)` | `i32` | `1` on success. Fails on an unverified session (§3). |
| `sess.auth_pubkey_auto()` | `i32` | `1` on success. Tries the user's default keys and a running `ssh-agent`. |

```aiml
- decl: [authed, i32, { call: [sess.auth_pubkey_auto] }]
- if:
    condition: { neq: [authed, 1] }
    then: [{ set: [authed, { call: [sess.auth_password, pw] }] }]
```

The password is a plain `string` argument and is **not** retained by the session beyond the
call. Callers must not log it (cosmos `doc/spec/94` §94.3 S2).

---

## 5. Running a command

A command is a channel, not a one-shot call, because the three things provisioning needs —
separate streams, an exit code, and **writable stdin** — cannot all come out of a single
return value.

| Call | Return | Description |
|------|--------|-------------|
| `sess.exec(command)` | `ssh_channel` | Start `command` on the remote shell. `null` on failure. |
| `ch.write(text)` | `i32` | Bytes written to stdin. |
| `ch.write_buffer(buf)` | `i32` | Same for binary content — this is how a file is shipped without SFTP (§6). |
| `ch.send_eof()` | `i32` | Close stdin; many commands only finish after this. |
| `ch.read_out(max_bytes)` | `string` | Up to `max_bytes` from stdout. When nothing is buffered yet it waits up to **2 s** for data, so a single read after a short command returns its output; `""` means nothing arrived in that window or the stream ended. The returned string is an owned copy. |
| `ch.read_err(max_bytes)` | `string` | Same for stderr. Separate stream, never merged into stdout. |
| `ch.is_eof()` | `i32` | `1` when the remote side closed both output streams. |
| `ch.exit_code()` | `i32` | Exit status after EOF; `-1` while still running or if the command died from a signal. |
| `ch.close()` | — | Free the channel. Safe to call twice. |

```aiml
# A sudo password goes to STDIN, never into argv — it would be in the node's process list.
- decl: [ch, ssh_channel, { call: [sess.exec, "sudo -S -p '' apparmor_parser -r /etc/apparmor.d/cosmos-agent"] }]
- decl: [wrote, i32, { call: [ch.write, "${sudo_pw}\n"] }]
- decl: [eof_rc, i32, { call: [ch.send_eof] }]
- decl: [out_txt, string, { call: [ch.read_out, 65536] }]
- decl: [err_txt, string, { call: [ch.read_err, 65536] }]
- decl: [rc, i32, { call: [ch.exit_code] }]
- call: [ch.close]
```

`read_out` returning `""` for both "nothing yet" and "ended" is deliberate: a caller that
loops until `is_eof()` is correct in both cases, and one that reads once gets whatever was
there. Distinguishing the two would invite code that treats a slow command as a finished one.

**Each read returns an owned copy.** Handing back a pointer into the channel's own buffer
looks cheaper and is wrong: `read_out` and `read_err` would share it, and the second call
would overwrite what the first result still points at — which during implementation made
stdout *appear* to contain stderr. The copy is RC-managed (`aiml_str_dup`), so the receiving
local frees it.

**`exit_code()` waits for the status, not for EOF.** The exit status arrives as its own
channel request and can follow the stream EOF; polling until EOF and then reading the status
returns `-1` more often than not. It therefore pumps until the status is there (bounded),
and the output it pumps along the way stays retrievable through `read_out`/`read_err`.

---

## 6. File transfer

**Not in this chapter.** A file is shipped through a channel:

```aiml
- decl: [total, i64, { call: [io.file_size, local_path] }]
- decl: [ch, ssh_channel, { call: [sess.exec, "cat > /tmp/agent.new"] }]
- decl: [src, file_handle, { call: [io.open_binary, local_path] }]
- decl: [done, i64, 0]
- while:
    condition: { lt: [done, total] }
    body:
      - decl: [block, buffer<u8>, { call: [io.read_bytes, src, 1048576] }]
      - decl: [sent, i32, { call: [ch.write_buffer, block] }]
      - set: [done, { +: [done, { cast: [sent, i64] }] }]
- call: [io.close, src]
- decl: [eof2, i32, { call: [ch.send_eof] }]
```

Blockweise über `io.file_size` als Schranke, nicht in einem Stück: eine Datei in
Agentengrösse im Arbeitsspeicher zu halten wäre dieselbe Sorte Fehler, die cosmos
`doc/spec/90` §AP1.5a bei der Ausgabe vermeidet. (`io.read_bytes` liefert am Dateiende
weniger als angefordert, hat aber keine eigene EOF-Abfrage — darum die gezählte Schranke.)

libssh does bring SFTP (`sftp.h`), and it is the better fit for many files or for reading
remote directories. It is left out of this slice on purpose: `write_buffer` already covers
"put this file there", and an SFTP binding would add API that nothing calls yet. When a case
needs it (remote listing, resume, large sets), it belongs here as §6 proper.

---

## 7. Test server (`ssh.test_*`) — test scope only

A client cannot be tested without a server, and the development machine has no `sshd`
(nothing on port 22, `openssh-server` absent). libssh ships a server side, so the test
suite starts its **own** sshd inside the test process.

> ⚠️ **This server executes the commands it receives.** That is remote code execution by
> design, which is exactly why it is fenced in: it binds **loopback only** (`127.0.0.1`),
> accepts **one** fixed credential pair passed by the test, generates an **ephemeral** host
> key per run, and is named `test_*` so it cannot be mistaken for infrastructure. It must
> never be used to serve anything.

| Call | Return | Description |
|------|--------|-------------|
| `ssh.test_listen(port, user, password)` | `ssh_test_server` | Bind `127.0.0.1:port`. `null` on failure (port in use). |
| `srv.serve_async()` | `thread_handle` | Serve sessions in a background thread until `stop()`. Each session is authenticated against the one credential pair, then its exec requests are served until the client disconnects. |
| `srv.fingerprint()` | `string` | The ephemeral host key's fingerprint — lets a test check `accept_host` against a *known* value instead of only TOFU. |
| `srv.stop()` | — | **Signals** the serve loop to end; it does **not** join. The thread is reaped by the `thread_handle` from `serve_async`, as with `http.start_async`. |

The thread belongs to the server, as with `http.start_async` (§32.2): a test that had to
spawn it itself would be a test about thread plumbing. **`stop()` must not join** — the caller
holds the handle and joins it; doing both joins the same thread twice, which is undefined and
in practice hangs at the very end, after every assertion has already passed. Serving must happen **concurrently**
with the client — single-threaded, `connect` would wait for a server that never gets to run.

```aiml
- decl: [srv, ssh_test_server, { call: [ssh.test_listen, 22899, "deploy", "secret"] }]
- decl: [srv_thread, thread_handle, { call: [srv.serve_async] }]
# … client work …
- call: [srv.stop]
- call: [srv_thread.join]
```

The server runs each requested command through the local shell and relays stdout, stderr and
the exit code. That makes the client test end-to-end: the assertion is about a command that
really ran, not about a canned reply.

---

## 8. Platform

Native only. On wasm32 and Android every call is a no-op returning `null`/`0`: there is no
TCP socket API in the browser, and an SSH client in a page would be pointless. A program that
needs SSH checks `ssh.connect` for `null`, as it does for any connection.

---

## 9. Dependency

`libssh` is bound through `exodus pkg` (cosmos `doc/spec/94` §94.4), not installed
system-wide:

| Package | Version | Provides |
|---|---|---|
| `libssh-dev` | `0.10.6-2ubuntu0.5` | `libssh/libssh.h`, `server.h`, `sftp.h` |
| `libssh-4` | `0.10.6-2ubuntu0.5` | `libssh.so.4` |

Link against the **shared** `libssh.so.4`. The `-dev` package's `libssh.a` additionally needs
GSSAPI (`gss_*` undefined symbols) on top of OpenSSL and zlib, which the shared library
resolves on its own.
