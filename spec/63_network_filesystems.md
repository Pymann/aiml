# 63 — Network drives: SMB, NFS, and what "RWX" really means

Status: draft.  Starting question: *can aiml handle network drives, with a Samba library
or something similar?*  The short answer is **yes, and without root** — but only if the three
approaches that are otherwise constantly confused are kept cleanly apart.

## 63.1 Three approaches, and they are not interchangeable

| Approach | What it is | Privileges | Visible to other processes |
|---|---|---|---|
| **A — client library** | `libsmbclient`, `libnfs`: the protocol inside the own process | **none in particular** | no |
| **B — kernel mount** | `mount.cifs`, `mount.nfs` | **root / CAP_SYS_ADMIN** | yes, system-wide |
| **C — FUSE** | file system in a user process | `/dev/fuse` | yes, depending on `allow_other` |

**Measured on this machine (2026-10-12):**

```
libsmbclient.so.0   /lib/x86_64-linux-gnu/libsmbclient.so.0     ← approach A, SMB
libnfs.so.14        /lib/x86_64-linux-gnu/libnfs.so.14          ← approach A, NFS
libfuse3.so.3       /lib/x86_64-linux-gnu/libfuse3.so.3         ← approach C
libssh.so.4         /lib/x86_64-linux-gnu/libssh.so.4           (already used by aiml, spec/32_6)
/dev/fuse           crw-rw-rw-                                   ← FUSE usable without root
mount.cifs, mount.nfs, smbclient, sshfs, rclone                  NOT installed
```

- **F63-1** The **normal approach for aiml is A**: the client library.  The reason is not
  convenience but the project's privilege requirement: an agent does **not run as root**
  (`cosmos doc/spec/94`), and approach B demands exactly that.  An approach that breaks the most
  important security requirement is not a normal approach.
- **F63-2** Approach B belongs to **node provisioning**, not to a step.  If a mount is needed
  (because third-party programs in the step must see the path), provisioning sets it up
  once (`cosmos doc/spec/94`), with all the requirements from there: password never in argv,
  never in the log, credentials in a file with mode `0600`.  A step that mounts by itself needs
  privileges and is therefore no longer an isolatable step.
- **F63-3** Approach C is the **bridge**: it makes A visible to third-party programs (`cmake`,
  `gcc` see a normal path) without root.  The price: one live process per mount, and without
  `user_allow_other` in `/etc/fuse.conf` **only the mounting user** sees the path — on
  this machine it is not set (measured).

## 63.2 The aiml surface: one namespace, several protocols

One namespace `netfs`, not a separate `smb` and `nfs` namespace.  The operations are
the same (open, read, write, list, metadata); only the protocol differs,
and it is part of the URL.

```yaml
- decl: [cred, netfs_cred, { call: [netfs.cred_from_file, "/etc/cosmos/share.cred"] }]
- decl: [conn, ptr<netfs_conn>, { call: [netfs.connect, "smb://fileserver/builds", cred] }]
- decl: [names, list<string>, { call: [netfs.list, conn, "/nightly"] }]
- decl: [bytes, buffer, { call: [netfs.read_file, conn, "/nightly/sources.tar"] }]
- call: [netfs.write_file, conn, "/nightly/report.json", payload]
- call: [netfs.close, conn]
```

- **F63-4** The **URL carries the protocol**: `smb://`, `nfs://`.  A program that changes its
  target changes a string, not its calls.  An unknown scheme is an error, not a
  fallback to local `io` — a silent fallback would write to the wrong file system.
- **F63-5** `netfs` is **not** `io`.  No mixing through a shared path type: a
  network operation can fail on the wire, a local one normally cannot, and it has a
  connection that can expire.  Making both look the same hides exactly the
  difference that matters when something goes wrong.
- **F63-6** Credentials come from a **file** or a secret handle, never from a
  string in the source code and never from an argument.  The same rule as for the
  enrollment token (`cosmos doc/spec/90` §AP1.9) and the sudo password (`§94.3` S1–S3): whatever
  is in argv is in the node's process list.
- **F63-7** A connection is a **handle with a lifetime** (`ptr<netfs_conn>`, spec/61):
  opened explicitly, closed explicitly, released at scope end.  No implicit
  reconnection in the background — a program that silently carries on after a dropped
  connection has written half the file and reports success.

## 63.3 "RWX" — what is behind it and what aiml can guarantee

`RWX` is the Kubernetes notation for **ReadWriteMany**: a volume that **several nodes
mount for writing at the same time**.  That is not a property of a call but of the
**protocol plus how it is used**.

| Access mode | Meaning | Achievable with A? |
|---|---|---|
| RWO (ReadWriteOnce) | one node writes | yes |
| ROX (ReadOnlyMany) | many read | yes |
| RWX (ReadWriteMany) | many write at the same time | yes — **with locks**, see below |

- **F63-8** aiml promises **no** consistency the protocol does not provide.  SMB2/3 and NFSv4
  offer byte-range locks and leases; NFSv3 needs NLM, which is often switched off.  `netfs`
  therefore exposes locking **explicitly** (`netfs.lock_range` / `netfs.unlock_range`) instead of
  hiding it.  A hidden locking model is the surest way to data loss that
  nobody can trace.
- **F63-9** **Several writers on the same file are a design error in a pipeline**,
  not a use case.  The lane model (`spec/62` §4.3) gives every lane its own directory
  for exactly this reason.  RWX is needed so that **different** lanes on **different** nodes
  can use the same volume — not so that two lanes write the same file.
- **F63-10** An output tree is stored **immutably** (content-addressed, `cosmos
  doc/spec/92`): write to a temporary name, then `rename` to the final one.  `rename`
  is atomic enough on SMB and NFS for this purpose; "writing into the target file" never is.
  The normal case therefore needs **no** lock at all — and that is the real reason the
  lane model holds up.
- **F63-11** `netfs.stat` returns size and modification time, but the modification time is
  **not** key material (`spec/62` §8): file server clocks differ from the node's.  Whoever
  wants to compare contents hashes them.

## 63.3a Time limits are mandatory, not a convenience

**Measured (2026-10-13):** `netfs.connect` on `smb://127.0.0.1/keinshare` returns
**immediately** — it only checks scheme and credentials, the handshake happens on first access
(that is how SMB works).  The first **access** to an unreachable server, on the other hand,
blocked for so long that a test with a 120 s limit had to be aborted: name resolution and
connection setup have their own generous timeouts in libsmbclient.

- **F63-12** Every `netfs` operation needs a **time limit**, and without one it is not
  released.  A step hanging on a dead file server holds a lease
  (`cosmos doc/spec/101` F101-3) until it expires — and until then looks like work.  That is
  exactly the state a build system must not have: busy without doing anything.
- **F63-13** The limit belongs to the **connection**, not to the individual call: a caller
  who has to set it per call forgets it somewhere.  Proposal: `netfs.connect(url, cred,
  timeout_ms)` with a default that errs on the small side rather than the large.
- **F63-14** A test that touches a network drive needs a **server inside the test process** —
  otherwise it is a bet on the environment and, in the bad case, a hang in the suite.  That is why
  `tests/test_netfs_smb.aiml` currently checks only what can be decided without a server:
  binding, scheme, credentials.  This is stated in the test itself, so nobody takes it for more.

## 63.4 What gets built first

1. **`netfs` with SMB (libsmbclient)**: read/write/list/stat, credentials from a file.
   Reason for SMB first: it is the protocol that is present in mixed networks anyway, and
   `libsmbclient` brings authentication along (NFSv3 has none; there everything hinges on uid/gid).
2. **A test server inside the test process**, as for SSH (`spec/32_6` §7).  Without it every test
   is a bet on the environment.  For SMB this means an `smbd` in a throwaway directory or a
   built-in mini server; **which of the two is open** (question for the author).
3. **NFS (libnfs)** after that, same surface.
4. **FUSE (approach C)** last, and only if a step needs third-party programs on the path.

## 63.5 Open questions

See `doc/OPEN_QUESTIONS.md` in the cosmos tree — only the keywords here: a test server for SMB,
`user_allow_other` on nodes, handling Kerberos, and whether mounts (approach B) should be part of
node provisioning at all or whether A+C are enough.
