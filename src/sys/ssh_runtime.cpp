/* SSH-Client (spec/32_6) — Bindung an libssh.
 *
 * Bewusst KEINE eigene Protokoll-Implementierung (anders als WebSockets,
 * spec/32_4): über diese Verbindung laufen Authentifizierungs-Geheimnisse und
 * die Host-Identität.  Schlüsselaustausch, Cipher-Aushandlung und Host-Key-
 * Behandlung selbst zu schreiben hiesse, jeden Fehler darin zu einem
 * Sicherheitsfehler zu machen.
 *
 * Zwei Dinge an der Schnittstelle sehen ungewöhnlich aus und sind Absicht:
 *   - Host-Prüfung ist ein EIGENER Schritt (accept_host).  Eine unverifizierte
 *     Sitzung verweigert jede Authentifizierung — es gibt keinen Schalter, der
 *     das überspringt, und keinen Standard, der still akzeptiert.
 *   - stdin ist beschreibbar.  Ein sudo-Passwort darf nicht in argv stehen
 *     (cosmos doc/spec/94 §94.3 S3), also muss es über den Kanal gehen.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)

#include <libssh/libssh.h>
#include <libssh/server.h>

/* RC-verwaltete Kopie (spec/61) — NICHT strdup: der zurückgegebene String trägt
 * den aiml-Refcount-Header, damit das empfangende Local ihn korrekt freigibt.
 * Ein Zeiger auf internen Zustand wäre hier doppelt falsch: read_out und
 * read_err teilten sich sonst einen Puffer, und der zweite Aufruf überschriebe,
 * worauf das Ergebnis des ersten noch zeigt (genau der Fehler, der stdout mit
 * stderr vertauscht erscheinen liess). */
extern "C" char* aiml_str_dup(const char* s);
#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace {

struct AimlSshSession {
    ssh_session  s        = nullptr;
    bool         verified = false;
    std::string  fp;        /* Fingerprint, einmal ermittelt */
    std::string  err;
};

/* Der Kanal puffert SELBST.  Grund: `exit_code` ist erst nach EOF der
 * Gegenseite bekannt, und bis dahin kann noch Ausgabe unterwegs sein.  Ohne
 * eigenen Puffer müsste exit_code entweder lügen (-1 trotz beendetem Befehl)
 * oder ungelesene Ausgabe verwerfen.  So pumpt es in die Puffer und verliert
 * nichts. */
struct AimlSshChannel {
    ssh_channel      c     = nullptr;
    AimlSshSession*  owner = nullptr;
    std::string      out_buf, err_buf;
    bool             eof_seen = false;
    int              status   = -1;
};

struct AimlSshTestServer {
    ssh_bind     b = nullptr;
    std::string  user, pass, fp;
    int          port = 0;
    volatile int running = 0;
    pthread_t    tid{};
};

/* thread_handle-Layout wie in http_runtime.cpp — `.join` erwartet genau das. */
struct ThreadHandle { int64_t tid; int8_t joined; int8_t detached; };

/* Einmal abgefragt statt je Datenblock (AIML_SSH_TRACE=1).  Die Spur hat beim
 * Bauen drei Fehler gezeigt, die von aussen alle gleich aussahen — ein Hänger —
 * und bleibt deshalb drin. */
bool trace_on() {
    static const bool on = getenv("AIML_SSH_TRACE") != nullptr;
    return on;
}

void note_err(AimlSshSession* sess) {
    if (!sess || !sess->s) return;
    const char* m = ssh_get_error(sess->s);
    sess->err = m ? m : "";
}

/* Liest, was gerade da ist, in die Puffer.  timeout_ms gilt je Strom. */
void pump(AimlSshChannel* ch, int timeout_ms) {
    if (!ch || !ch->c) return;
    char buf[16384];
    for (int stream = 0; stream < 2; stream++) {
        for (;;) {
            int avail = ssh_channel_poll_timeout(ch->c, stream == 0 ? timeout_ms : 0, stream);
            if (avail == SSH_EOF) { ch->eof_seen = true; break; }
            if (avail <= 0) break;
            int want = avail > (int)sizeof(buf) ? (int)sizeof(buf) : avail;
            int got = ssh_channel_read(ch->c, buf, (uint32_t)want, stream);
            if (got <= 0) break;
            (stream == 0 ? ch->out_buf : ch->err_buf).append(buf, (size_t)got);
            if (trace_on())
                fprintf(stderr, "[ssh-cli] pump stream=%d got=%d\n", stream, got);
        }
    }
    if (ssh_channel_is_eof(ch->c)) ch->eof_seen = true;
}

std::string take(std::string& from, int32_t max_bytes) {
    if (max_bytes <= 0 || from.empty()) return std::string();
    size_t n = (size_t)max_bytes < from.size() ? (size_t)max_bytes : from.size();
    std::string out = from.substr(0, n);
    from.erase(0, n);
    return out;
}

/* ── Test-Server: einen Befehl ausführen und relayen ─────────────────────────
 * Getrennte Rohre für stdout/stderr (die Trennung ist eine Zusage der
 * Schnittstelle), und der Kanal-Eingang wird an das stdin des Kindes
 * weitergegeben — sonst liesse sich `write`/`send_eof` nicht prüfen. */
void relay_command(ssh_channel chan, const char* cmd) {
    if (!chan || !cmd) return;
    int in_p[2], out_p[2], err_p[2];
    if (pipe(in_p) || pipe(out_p) || pipe(err_p)) return;

    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        dup2(in_p[0], 0); dup2(out_p[1], 1); dup2(err_p[1], 2);
        close(in_p[0]); close(in_p[1]);
        close(out_p[0]); close(out_p[1]);
        close(err_p[0]); close(err_p[1]);
        execl("/bin/sh", "sh", "-c", cmd, (char*)nullptr);
        _exit(127);
    }
    close(in_p[0]); close(out_p[1]); close(err_p[1]);

    bool child_in_open = true;
    bool reaped = false;
    int  st = 0;
    char buf[16384];
    for (;;) {
        /* Kanal → Kind */
        int cin = ssh_channel_poll_timeout(chan, 20, 0);
        if (cin == SSH_EOF) {
            if (child_in_open) { close(in_p[1]); child_in_open = false; }
        } else if (cin > 0) {
            int want = cin > (int)sizeof(buf) ? (int)sizeof(buf) : cin;
            int got = ssh_channel_read(chan, buf, (uint32_t)want, 0);
            if (got > 0 && child_in_open) {
                ssize_t off = 0;
                while (off < got) {
                    ssize_t w = write(in_p[1], buf + off, (size_t)(got - off));
                    if (w <= 0) break;
                    off += w;
                }
            }
        }

        /* Kind → Kanal */
        struct pollfd pfd[2] = {{out_p[0], POLLIN, 0}, {err_p[0], POLLIN, 0}};
        int pr = poll(pfd, 2, 20);
        bool any = false;
        if (pr > 0) {
            for (int i = 0; i < 2; i++) {
                if (!(pfd[i].revents & (POLLIN | POLLHUP))) continue;
                ssize_t n = read(i == 0 ? out_p[0] : err_p[0], buf, sizeof buf);
                if (n > 0) {
                    any = true;
                    int w = (i == 0) ? ssh_channel_write(chan, buf, (uint32_t)n)
                                     : ssh_channel_write_stderr(chan, buf, (uint32_t)n);
                    if (trace_on())
                        fprintf(stderr, "[ssh-srv] relay %s %zd bytes (write rc=%d)\n",
                                i == 0 ? "stdout" : "stderr", (ssize_t)n, w);
                }
            }
        }

        /* Das Kind wird GENAU EINMAL geerntet und das Ergebnis gemerkt.
         * Vorher lautete die Bedingung `waitpid(...) == pid && !any`: in der
         * Runde mit den letzten Daten war `any` wahr UND das Kind wurde dabei
         * geerntet — danach gab waitpid nur noch -1, die Bedingung konnte nie
         * mehr zutreffen, und die Schleife lief endlos (der Server schickte
         * nie einen Exit-Status, der Client wartete darauf). */
        if (!reaped) {
            pid_t done = waitpid(pid, &st, WNOHANG);
            if (done == pid)      reaped = true;
            else if (done < 0)    { reaped = true; st = -1; }
        }
        if (reaped && !any) {
            /* Reste abholen, bevor der Status geht — sonst fehlt die letzte Zeile. */
            for (int i = 0; i < 2; i++) {
                for (;;) {
                    ssize_t n = read(i == 0 ? out_p[0] : err_p[0], buf, sizeof buf);
                    if (n <= 0) break;
                    if (i == 0) ssh_channel_write(chan, buf, (uint32_t)n);
                    else        ssh_channel_write_stderr(chan, buf, (uint32_t)n);
                }
            }
            int code = (st >= 0 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
            int sx = ssh_channel_request_send_exit_status(chan, code);
            if (trace_on())
                fprintf(stderr, "[ssh-srv] exit status %d sent (rc=%d)\n", code, sx);
            break;
        }
    }

    if (child_in_open) close(in_p[1]);
    close(out_p[0]); close(err_p[0]);
    ssh_channel_send_eof(chan);
    ssh_channel_close(chan);
}

void serve_session(AimlSshTestServer* srv, ssh_session s) {
    ssh_set_auth_methods(s, SSH_AUTH_METHOD_PASSWORD);
    bool authed = false;
    ssh_channel chan = nullptr;

    /* ssh_message_get blockiert; ohne Zeitschranke könnte stop() den Thread
     * nicht beenden (der pthread_join hing).  Den Sitzungs-fd zu pollen war
     * der falsche Weg: libssh puffert intern, also liegt eine Nachricht
     * bereit, während auf dem Socket nichts Neues ankommt — nach dem ersten
     * Kommando kam dadurch nie wieder eine Nachricht an.  Stattdessen eine
     * kurze Zeitschranke auf der Sitzung: message_get kehrt regelmässig
     * zurück, und die Schleife prüft `running`. */
    long tmo_usec = 100000;
    ssh_options_set(s, SSH_OPTIONS_TIMEOUT_USEC, &tmo_usec);
    for (;;) {
        if (!srv->running) break;
        ssh_message m = ssh_message_get(s);
        if (!m) {
            /* Kein Message heisst NICHT zwangsläufig "Verbindung weg" — nach
             * einem beendeten Kanal kam hier sonst ein break, die Sitzung wurde
             * getrennt, und jedes weitere exec des Clients scheiterte. */
            if (ssh_is_connected(s)) continue;
            break;
        }
        int type = ssh_message_type(m);
        int sub  = ssh_message_subtype(m);
        if (trace_on())
            fprintf(stderr, "[ssh-srv] msg type=%d sub=%d authed=%d chan=%p\n",
                    type, sub, (int)authed, (void*)chan);

        if (!authed) {
            if (type == SSH_REQUEST_AUTH && sub == SSH_AUTH_METHOD_PASSWORD) {
                const char* u = ssh_message_auth_user(m);
                const char* p = ssh_message_auth_password(m);
                if (u && p && srv->user == u && srv->pass == p) {
                    authed = true;
                    ssh_message_auth_reply_success(m, 0);
                    ssh_message_free(m);
                    continue;
                }
            }
            ssh_message_reply_default(m);   /* abgelehnt */
            ssh_message_free(m);
            continue;
        }

        if (type == SSH_REQUEST_CHANNEL_OPEN && sub == SSH_CHANNEL_SESSION) {
            chan = ssh_message_channel_request_open_reply_accept(m);
            ssh_message_free(m);
            continue;
        }
        if (type == SSH_REQUEST_CHANNEL && sub == SSH_CHANNEL_REQUEST_EXEC && chan) {
            const char* cmd = ssh_message_channel_request_command(m);
            std::string copy = cmd ? cmd : "";
            ssh_message_channel_request_reply_success(m);
            ssh_message_free(m);
            relay_command(chan, copy.c_str());
            ssh_channel_free(chan);
            chan = nullptr;
            continue;
        }
        ssh_message_reply_default(m);
        ssh_message_free(m);
    }
    if (chan) ssh_channel_free(chan);
}

void* serve_loop(void* arg) {
    AimlSshTestServer* srv = static_cast<AimlSshTestServer*>(arg);
    /* Auf dem LISTENER-fd zu pollen ist hier richtig (anders als beim
     * Sitzungs-fd, wo libssh intern puffert): "lesbar" heisst bei einem
     * Listener genau "eine Verbindung wartet".  `ssh_bind_set_blocking(b, 0)`
     * allein genügte nicht — ssh_bind_accept blieb blockierend stehen, der
     * Thread sah `running == 0` nie, und stop()+join hing. */
    int bfd = ssh_bind_get_fd(srv->b);
    while (srv->running) {
        struct pollfd lpf = { bfd, POLLIN, 0 };
        int lp = poll(&lpf, 1, 50);
        if (lp <= 0) continue;

        ssh_session s = ssh_new();
        if (!s) break;
        int rc = ssh_bind_accept(srv->b, s);
        if (rc != SSH_OK) {
            ssh_free(s);
            continue;
        }
        /* Der nicht-blockierende Modus des bind geht auf die angenommene
         * Sitzung über; dann liefert der Schlüsselaustausch nur SSH_AGAIN.
         * Ab hier wird blockierend bedient — der Thread gehört ohnehin dieser
         * einen Sitzung. */
        ssh_set_blocking(s, 1);
        int cfd = ssh_get_fd(s);
        if (cfd >= 0) fcntl(cfd, F_SETFD, fcntl(cfd, F_GETFD) | FD_CLOEXEC);
        if (ssh_handle_key_exchange(s) == SSH_OK) serve_session(srv, s);
        else fprintf(stderr, "[ssh] test server: key exchange failed: %s\n", ssh_get_error(s));
        ssh_disconnect(s);
        ssh_free(s);
    }
    if (srv->b) { ssh_bind_free(srv->b); srv->b = nullptr; }
    return nullptr;
}

} // namespace

/* ── Client ─────────────────────────────────────────────────────────────────*/

extern "C" void* aiml_ssh_connect(const char* host, int32_t port,
                                  const char* user, int32_t timeout_ms) {
    if (!host || !user) return nullptr;
    AimlSshSession* sess = new AimlSshSession();
    sess->s = ssh_new();
    if (!sess->s) { delete sess; return nullptr; }

    int p = port > 0 ? port : 22;
    long tmo = timeout_ms > 0 ? timeout_ms / 1000 : 10;
    if (tmo < 1) tmo = 1;
    ssh_options_set(sess->s, SSH_OPTIONS_HOST, host);
    ssh_options_set(sess->s, SSH_OPTIONS_PORT, &p);
    ssh_options_set(sess->s, SSH_OPTIONS_USER, user);
    ssh_options_set(sess->s, SSH_OPTIONS_TIMEOUT, &tmo);

    if (ssh_connect(sess->s) != SSH_OK) {
        note_err(sess);
        fprintf(stderr, "[ssh] connect %s:%d failed: %s\n", host, p, sess->err.c_str());
        ssh_free(sess->s);
        delete sess;
        return nullptr;
    }

    /* Fingerprint sofort ermitteln: ab hier existiert der Host-Key, das
     * Passwort noch nicht — genau die Reihenfolge, die §3 braucht. */
    ssh_key key = nullptr;
    if (ssh_get_server_publickey(sess->s, &key) == SSH_OK) {
        unsigned char* hash = nullptr;
        size_t hlen = 0;
        if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen) == 0) {
            char* fp = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
            if (fp) { sess->fp = fp; ssh_string_free_char(fp); }
            ssh_clean_pubkey_hash(&hash);
        }
        ssh_key_free(key);
    }
    return sess;
}

extern "C" const char* aiml_ssh_fingerprint(void* s) {
    AimlSshSession* sess = static_cast<AimlSshSession*>(s);
    return aiml_str_dup(sess ? sess->fp.c_str() : "");
}

extern "C" int32_t aiml_ssh_accept_host(void* s, const char* expected) {
    AimlSshSession* sess = static_cast<AimlSshSession*>(s);
    if (!sess) return 0;
    if (sess->fp.empty()) return 0;          /* ohne Key kein Vertrauen */
    if (!expected || !*expected) {            /* trust on first use, ausgeschrieben */
        sess->verified = true;
        return 1;
    }
    sess->verified = (sess->fp == expected);
    if (!sess->verified)
        fprintf(stderr, "[ssh] host key mismatch: have %s\n", sess->fp.c_str());
    return sess->verified ? 1 : 0;
}

extern "C" int32_t aiml_ssh_auth_password(void* s, const char* pw) {
    AimlSshSession* sess = static_cast<AimlSshSession*>(s);
    if (!sess || !sess->s || !sess->verified) return 0;
    int rc = ssh_userauth_password(sess->s, nullptr, pw ? pw : "");
    if (rc != SSH_AUTH_SUCCESS) note_err(sess);
    return rc == SSH_AUTH_SUCCESS ? 1 : 0;
}

extern "C" int32_t aiml_ssh_auth_pubkey_auto(void* s) {
    AimlSshSession* sess = static_cast<AimlSshSession*>(s);
    if (!sess || !sess->s || !sess->verified) return 0;
    int rc = ssh_userauth_publickey_auto(sess->s, nullptr, nullptr);
    if (rc != SSH_AUTH_SUCCESS) note_err(sess);
    return rc == SSH_AUTH_SUCCESS ? 1 : 0;
}

extern "C" void* aiml_ssh_exec(void* s, const char* cmd) {
    AimlSshSession* sess = static_cast<AimlSshSession*>(s);
    if (!sess || !sess->s || !sess->verified || !cmd) return nullptr;
    ssh_channel c = ssh_channel_new(sess->s);
    if (!c) { note_err(sess); return nullptr; }
    if (ssh_channel_open_session(c) != SSH_OK) {
        note_err(sess); ssh_channel_free(c); return nullptr;
    }
    if (ssh_channel_request_exec(c, cmd) != SSH_OK) {
        note_err(sess); ssh_channel_close(c); ssh_channel_free(c); return nullptr;
    }
    AimlSshChannel* ch = new AimlSshChannel();
    ch->c = c;
    ch->owner = sess;
    return ch;
}

extern "C" int32_t aiml_ssh_ch_write(void* c, const char* text) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch || !ch->c || !text) return 0;
    int n = ssh_channel_write(ch->c, text, (uint32_t)strlen(text));
    return n > 0 ? n : 0;
}

extern "C" int32_t aiml_ssh_ch_write_buffer(void* c, void* data, int32_t len) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch || !ch->c || !data || len <= 0) return 0;
    int n = ssh_channel_write(ch->c, data, (uint32_t)len);
    return n > 0 ? n : 0;
}

extern "C" int32_t aiml_ssh_ch_send_eof(void* c) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch || !ch->c) return 0;
    return ssh_channel_send_eof(ch->c) == SSH_OK ? 1 : 0;
}

extern "C" const char* aiml_ssh_ch_read_out(void* c, int32_t max_bytes) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch) return "";
    if (ch->out_buf.empty()) pump(ch, 2000);
    std::string taken = take(ch->out_buf, max_bytes);
    return aiml_str_dup(taken.c_str());
}

extern "C" const char* aiml_ssh_ch_read_err(void* c, int32_t max_bytes) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch) return "";
    if (ch->err_buf.empty()) pump(ch, 2000);
    std::string taken = take(ch->err_buf, max_bytes);
    return aiml_str_dup(taken.c_str());
}

extern "C" int32_t aiml_ssh_ch_is_eof(void* c) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch || !ch->c) return 1;
    pump(ch, 0);
    return ch->eof_seen ? 1 : 0;
}

extern "C" int32_t aiml_ssh_ch_exit_code(void* c) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch || !ch->c) return -1;
    /* Pumpen, bis der Status da ist — die Ausgabe landet dabei in den Puffern
     * und bleibt für read_out/read_err abholbar.  NICHT bei EOF abbrechen: die
     * exit-status-Anfrage kommt als eigener Kanal-Request und kann nach dem
     * EOF eintreffen; bei einem Abbruch auf EOF blieb der Status -1. */
    for (int i = 0; i < 250; i++) {
        if (ch->status >= 0) break;
        pump(ch, 20);
        int st = ssh_channel_get_exit_status(ch->c);
        if (trace_on() && (i % 25) == 0)
            fprintf(stderr, "[ssh-cli] exit_code try %d: st=%d eof=%d\n", i, st, (int)ch->eof_seen);
        if (st != -1) { ch->status = st; break; }
    }
    return ch->status;
}

extern "C" void aiml_ssh_ch_close(void* c) {
    AimlSshChannel* ch = static_cast<AimlSshChannel*>(c);
    if (!ch) return;
    if (ch->c) {
        ssh_channel_close(ch->c);
        ssh_channel_free(ch->c);
        ch->c = nullptr;
    }
    delete ch;
}

extern "C" void aiml_ssh_close(void* s) {
    AimlSshSession* sess = static_cast<AimlSshSession*>(s);
    if (!sess) return;
    if (sess->s) { ssh_disconnect(sess->s); ssh_free(sess->s); sess->s = nullptr; }
    delete sess;
}

extern "C" const char* aiml_ssh_last_error(void* s) {
    AimlSshSession* sess = static_cast<AimlSshSession*>(s);
    return aiml_str_dup(sess ? sess->err.c_str() : "");
}

/* ── Test-Server (spec/32_6 §7) — NUR für Tests ─────────────────────────────
 * Führt aus, was er empfängt.  Deshalb eingezäunt: nur 127.0.0.1, ein einziges
 * Zugangspaar, Einmal-Hostkey je Lauf, und `test_` im Namen. */

extern "C" void* aiml_ssh_test_listen(int32_t port, const char* user, const char* pass) {
    if (!user || !pass || port <= 0) return nullptr;
    AimlSshTestServer* srv = new AimlSshTestServer();
    srv->user = user; srv->pass = pass; srv->port = port;
    srv->b = ssh_bind_new();
    if (!srv->b) { delete srv; return nullptr; }

    ssh_key host_key = nullptr;
    if (ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &host_key) != SSH_OK || !host_key) {
        ssh_bind_free(srv->b); delete srv; return nullptr;
    }
    /* IMPORT_KEY nimmt den Schlüssel im Speicher — keine Datei auf der Platte,
     * die liegen bleiben könnte.  Der bind übernimmt den Besitz. */
    const char* addr = "127.0.0.1";
    int p = port;
    ssh_bind_options_set(srv->b, SSH_BIND_OPTIONS_BINDADDR, addr);
    ssh_bind_options_set(srv->b, SSH_BIND_OPTIONS_BINDPORT, &p);
    if (ssh_bind_options_set(srv->b, SSH_BIND_OPTIONS_IMPORT_KEY, host_key) != SSH_OK) {
        ssh_key_free(host_key);
        ssh_bind_free(srv->b); delete srv; return nullptr;
    }

    unsigned char* hash = nullptr;
    size_t hlen = 0;
    if (ssh_get_publickey_hash(host_key, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen) == 0) {
        char* fp = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
        if (fp) { srv->fp = fp; ssh_string_free_char(fp); }
        ssh_clean_pubkey_hash(&hash);
    }

    if (ssh_bind_listen(srv->b) != SSH_OK) {
        fprintf(stderr, "[ssh] test_listen on 127.0.0.1:%d failed: %s\n",
                port, ssh_get_error(srv->b));
        ssh_bind_free(srv->b); delete srv; return nullptr;
    }
    /* FD_CLOEXEC: relay_command forkt und exec't /bin/sh.  Ohne das erbt jedes
     * Kind den Listener — ein überlebendes Kind hielt den Port nach dem Ende
     * des Servers weiter belegt, und der nächste Lauf fand ihn besetzt. */
    int lfd = ssh_bind_get_fd(srv->b);
    if (lfd >= 0) fcntl(lfd, F_SETFD, fcntl(lfd, F_GETFD) | FD_CLOEXEC);
    return srv;
}

extern "C" const char* aiml_ssh_test_fingerprint(void* s) {
    AimlSshTestServer* srv = static_cast<AimlSshTestServer*>(s);
    return aiml_str_dup(srv ? srv->fp.c_str() : "");
}

extern "C" void* aiml_ssh_test_serve_async(void* s) {
    AimlSshTestServer* srv = static_cast<AimlSshTestServer*>(s);
    if (!srv) return nullptr;
    srv->running = 1;
    ThreadHandle* h = static_cast<ThreadHandle*>(calloc(1, sizeof(ThreadHandle)));
    pthread_create(&srv->tid, nullptr, serve_loop, srv);
    if (h) h->tid = static_cast<int64_t>(srv->tid);
    return h;
}

/* Nur SIGNALISIEREN — nicht joinen.  Geerntet wird über den thread_handle, den
 * serve_async geliefert hat (Muster wie http_server_stop).  Hier zusätzlich zu
 * joinen hiess, dass das anschliessende `srv_thread.join` des Aufrufers dasselbe
 * pthread ein ZWEITES Mal joint — undefiniert, und in der Praxis ein Hänger
 * genau am Testende, nachdem alle Prüfungen schon durchgelaufen waren.
 *
 * Der ssh_bind wird vom Thread freigegeben, wenn er endet: ihn hier
 * freizugeben, während der Thread noch im accept steht, wäre eine
 * Use-after-free.  Die kleine Server-Struktur bleibt liegen (Testbereich,
 * ein Objekt je Lauf) — sie freizugeben wäre unsicher, weil `fingerprint`
 * danach noch aufgerufen werden darf. */
extern "C" void aiml_ssh_test_stop(void* s) {
    AimlSshTestServer* srv = static_cast<AimlSshTestServer*>(s);
    if (!srv) return;
    srv->running = 0;
}

#else /* wasm32 / Android: kein TCP, kein SSH (spec/32_6 §8) */

extern "C" void*       aiml_ssh_connect(const char*, int32_t, const char*, int32_t) { return nullptr; }
extern "C" const char* aiml_ssh_fingerprint(void*)                { return ""; }
extern "C" int32_t     aiml_ssh_accept_host(void*, const char*)   { return 0; }
extern "C" int32_t     aiml_ssh_auth_password(void*, const char*)  { return 0; }
extern "C" int32_t     aiml_ssh_auth_pubkey_auto(void*)           { return 0; }
extern "C" void*       aiml_ssh_exec(void*, const char*)          { return nullptr; }
extern "C" int32_t     aiml_ssh_ch_write(void*, const char*)      { return 0; }
extern "C" int32_t     aiml_ssh_ch_write_buffer(void*, void*, int32_t) { return 0; }
extern "C" int32_t     aiml_ssh_ch_send_eof(void*)                { return 0; }
extern "C" const char* aiml_ssh_ch_read_out(void*, int32_t)       { return ""; }
extern "C" const char* aiml_ssh_ch_read_err(void*, int32_t)       { return ""; }
extern "C" int32_t     aiml_ssh_ch_is_eof(void*)                  { return 1; }
extern "C" int32_t     aiml_ssh_ch_exit_code(void*)               { return -1; }
extern "C" void        aiml_ssh_ch_close(void*)                   {}
extern "C" void        aiml_ssh_close(void*)                      {}
extern "C" const char* aiml_ssh_last_error(void*)                 { return ""; }
extern "C" void*       aiml_ssh_test_listen(int32_t, const char*, const char*) { return nullptr; }
extern "C" const char* aiml_ssh_test_fingerprint(void*)           { return ""; }
extern "C" void*       aiml_ssh_test_serve_async(void*)           { return nullptr; }
extern "C" void        aiml_ssh_test_stop(void*)                  {}

#endif
