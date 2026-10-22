# 63 — Netzlaufwerke: SMB, NFS, und was „RWX" wirklich heißt

Status: Entwurf.  Ausgangsfrage: *kriegt man Netzlaufwerke über aiml hin, mit einer Samba-Lib
oder so etwas?*  Die kurze Antwort ist **ja, und zwar ohne root** — aber nur, wenn man die drei
Wege sauber trennt, die sonst ständig verwechselt werden.

## 63.1 Drei Wege, und sie sind nicht austauschbar

| Weg | Was es ist | Rechte | Sichtbar für andere Prozesse |
|---|---|---|---|
| **A — Client-Bibliothek** | `libsmbclient`, `libnfs`: Protokoll im eigenen Prozess | **keine besonderen** | nein |
| **B — Kernel-Mount** | `mount.cifs`, `mount.nfs` | **root / CAP_SYS_ADMIN** | ja, systemweit |
| **C — FUSE** | Dateisystem im Benutzerprozess | `/dev/fuse` | ja, je nach `allow_other` |

**Gemessen auf dieser Maschine (2026-10-12):**

```
libsmbclient.so.0   /lib/x86_64-linux-gnu/libsmbclient.so.0     ← Weg A, SMB
libnfs.so.14        /lib/x86_64-linux-gnu/libnfs.so.14          ← Weg A, NFS
libfuse3.so.3       /lib/x86_64-linux-gnu/libfuse3.so.3         ← Weg C
libssh.so.4         /lib/x86_64-linux-gnu/libssh.so.4           (nutzt aiml schon, spec/32_6)
/dev/fuse           crw-rw-rw-                                   ← FUSE ohne root nutzbar
mount.cifs, mount.nfs, smbclient, sshfs, rclone                  NICHT installiert
```

- **F63-1** Der **Normalweg für aiml ist A**: die Client-Bibliothek.  Begründung ist nicht
  Bequemlichkeit, sondern die Rechte-Auflage des Projekts: ein Agent läuft **nicht als root**
  (`cosmos doc/spec/94`), und Weg B verlangt genau das.  Ein Weg, der die wichtigste
  Sicherheitsauflage bricht, ist kein Normalweg.
- **F63-2** Weg B gehört zur **Knoten-Einrichtung**, nicht zu einem Schritt.  Wenn ein Mount nötig
  ist (weil fremde Programme im Schritt den Pfad sehen müssen), richtet ihn die Provisionierung
  einmalig ein (`cosmos doc/spec/94`), mit allen Auflagen von dort: Passwort nie in argv, nie im
  Log, Anmeldedaten in einer Datei mit `0600`.  Ein Schritt, der selbst mountet, braucht
  Privilegien und ist damit kein isolierbarer Schritt mehr.
- **F63-3** Weg C ist die **Brücke**: er macht A für fremde Programme sichtbar (`cmake`, `gcc`
  sehen einen normalen Pfad), ohne root.  Preis: ein lebender Prozess je Mount, und ohne
  `user_allow_other` in `/etc/fuse.conf` sieht **nur der mountende Benutzer** den Pfad — auf
  dieser Maschine ist es nicht gesetzt (gemessen).

## 63.2 Die aiml-Oberfläche: ein Namensraum, mehrere Protokolle

Ein Namensraum `netfs`, nicht je ein `smb`- und ein `nfs`-Namensraum.  Die Operationen sind
dieselben (öffnen, lesen, schreiben, auflisten, Metadaten); nur das Protokoll unterscheidet sich,
und es steht in der URL.

```yaml
- decl: [cred, netfs_cred, { call: [netfs.cred_from_file, "/etc/cosmos/share.cred"] }]
- decl: [conn, ptr<netfs_conn>, { call: [netfs.connect, "smb://fileserver/builds", cred] }]
- decl: [names, list<string>, { call: [netfs.list, conn, "/nightly"] }]
- decl: [bytes, buffer, { call: [netfs.read_file, conn, "/nightly/sources.tar"] }]
- call: [netfs.write_file, conn, "/nightly/report.json", payload]
- call: [netfs.close, conn]
```

- **F63-4** Die **URL trägt das Protokoll**: `smb://`, `nfs://`.  Ein Programm, das sein Ziel
  wechselt, ändert eine Zeichenkette und keine Aufrufe.  Unbekanntes Schema ist ein Fehler, kein
  Rückfall auf lokales `io` — ein stiller Rückfall schriebe ins falsche Dateisystem.
- **F63-5** `netfs` ist **nicht** `io`.  Kein Vermischen über einen gemeinsamen Pfadtyp: eine
  Netzoperation kann an der Leitung scheitern, eine lokale im Normalfall nicht, und sie hat eine
  Verbindung, die ablaufen kann.  Wer beides gleich aussehen lässt, versteckt genau den
  Unterschied, der im Fehlerfall zählt.
- **F63-6** Anmeldedaten kommen aus einer **Datei** oder einem Geheimnis-Griff, nie aus einer
  Zeichenkette im Quelltext und nie aus einem Argument.  Dieselbe Regel wie beim
  Enrollment-Token (`cosmos doc/spec/90` §AP1.9) und beim sudo-Passwort (`§94.3` S1–S3): was in
  argv steht, steht in der Prozessliste des Knotens.
- **F63-7** Eine Verbindung ist ein **Griff mit Lebensdauer** (`ptr<netfs_conn>`, spec/61):
  explizit geöffnet, explizit geschlossen, am Scope-Ende freigegeben.  Keine implizite
  Wiederverbindung im Hintergrund — ein Programm, das nach einem Abriss stillschweigend
  weiterläuft, hat die halbe Datei geschrieben und meldet Erfolg.

## 63.3 „RWX" — was dahinter steckt und was aiml davon halten kann

`RWX` ist die Kubernetes-Schreibweise für **ReadWriteMany**: ein Volume, das **mehrere Knoten
gleichzeitig** schreibend einhängen.  Das ist keine Eigenschaft eines Aufrufs, sondern des
**Protokolls plus der Nutzung**.

| Zugriffsart | Bedeutung | Mit A erreichbar? |
|---|---|---|
| RWO (ReadWriteOnce) | ein Knoten schreibt | ja |
| ROX (ReadOnlyMany) | viele lesen | ja |
| RWX (ReadWriteMany) | viele schreiben gleichzeitig | ja — **mit Sperren**, siehe unten |

- **F63-8** aiml verspricht **keine** Konsistenz, die das Protokoll nicht hält.  SMB2/3 und NFSv4
  bieten Byte-Bereichs-Sperren und Leases; NFSv3 braucht NLM, das oft abgeschaltet ist.  `netfs`
  gibt die Sperre deshalb **ausdrücklich** heraus (`netfs.lock_range` / `netfs.unlock_range`) statt
  sie zu verstecken.  Ein verstecktes Sperrmodell ist der sicherste Weg zu einem Datenverlust, den
  niemand nachvollziehen kann.
- **F63-9** **Mehrere Schreiber auf dieselbe Datei sind in einer Pipeline ein Entwurfsfehler**,
  kein Anwendungsfall.  Das Lane-Modell (`spec/62` §4.3) gibt jeder Lane ihr eigenes Verzeichnis
  genau deshalb.  RWX wird gebraucht, damit **verschiedene** Lanes auf **verschiedenen** Knoten
  dasselbe Volume benutzen — nicht, damit zwei Lanes dieselbe Datei schreiben.
- **F63-10** Ein Ausgabebaum wird **unveränderlich** abgelegt (inhaltsadressiert, `cosmos
  doc/spec/92`): schreiben an einen temporären Namen, dann `rename` auf den endgültigen.  `rename`
  ist auf SMB und NFS atomar genug für diesen Zweck; „in die Zieldatei schreiben" ist es nie.
  Damit braucht der Normalfall **gar keine** Sperre — und das ist der eigentliche Grund, warum das
  Lane-Modell trägt.
- **F63-11** `netfs.stat` liefert Größe und Änderungszeit, aber die Änderungszeit ist **kein**
  Schlüsselmaterial (`spec/62` §8): Uhren der Dateiserver gehen anders als die des Knotens.  Wer
  Inhalte vergleichen will, hasht sie.

## 63.3a Zeitgrenzen sind Pflicht, nicht Komfort

**Gemessen (2026-10-13):** `netfs.connect` auf `smb://127.0.0.1/keinshare` kehrt **sofort**
zurück — es prüft nur Schema und Anmeldedaten, der Handschlag kommt erst beim Zugriff (so ist
SMB).  Der erste **Zugriff** auf einen nicht erreichbaren Server blockierte dagegen so lange, dass
ein Test mit 120 s Grenze abgebrochen werden musste: Namensauflösung und Verbindungsaufbau haben
in libsmbclient ihre eigenen, großzügigen Fristen.

- **F63-12** Jede `netfs`-Operation braucht eine **Zeitgrenze**, und ohne sie wird sie nicht
  freigegeben.  Ein Schritt, der an einem toten Dateiserver hängt, belegt ein Lehen
  (`cosmos doc/spec/101` F101-3), bis es abläuft — und sieht bis dahin aus wie Arbeit.  Das ist
  genau der Zustand, den ein Bausystem nicht haben darf: beschäftigt, ohne etwas zu tun.
- **F63-13** Die Grenze gehört an die **Verbindung**, nicht an den einzelnen Aufruf: ein Aufrufer,
  der sie je Aufruf setzen müsste, vergisst sie irgendwo.  Vorschlag: `netfs.connect(url, cred,
  timeout_ms)` mit einer Vorgabe, die lieber zu klein als zu groß ist.
- **F63-14** Ein Test, der ein Netzlaufwerk anfasst, braucht einen **Server im Testprozess** —
  sonst ist er eine Umgebungswette und im schlechten Fall ein Hänger in der Suite.  Deshalb prüft
  `tests/test_netfs_smb.aiml` heute nur, was ohne Server entscheidbar ist: Bindung, Schema,
  Anmeldedaten.  Das steht im Test selbst, damit ihn niemand für mehr hält.

## 63.4 Was zuerst gebaut wird

1. **`netfs` mit SMB (libsmbclient)**, lesen/schreiben/auflisten/stat, Anmeldedaten aus Datei.
   Grund für SMB zuerst: es ist das Protokoll, das in gemischten Netzen ohnehin da ist, und
   `libsmbclient` bringt Authentifizierung mit (NFSv3 kennt keine; dort hängt alles an uid/gid).
2. **Ein Testserver im Testprozess**, wie bei SSH (`spec/32_6` §7).  Ohne ihn ist jeder Test eine
   Umgebungswette.  Für SMB heißt das ein `smbd` in einem Wegwerf-Verzeichnis oder ein
   eingebauter Mini-Server; **welcher Weg, ist offen** (Frage an den Autor).
3. **NFS (libnfs)** danach, gleiche Oberfläche.
4. **FUSE (Weg C)** zuletzt und nur, wenn ein Schritt fremde Programme auf dem Pfad braucht.

## 63.5 Offene Fragen

Siehe `doc/OPEN_QUESTIONS.md` im cosmos-Baum — hier nur die Stichworte: Testserver für SMB,
`user_allow_other` auf Knoten, Umgang mit Kerberos, und ob Mounts (Weg B) überhaupt Teil der
Knoten-Einrichtung werden sollen oder ob A+C genügen.
