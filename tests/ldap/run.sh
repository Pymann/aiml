#!/usr/bin/env bash
# run.sh — LDAP builtins (spec/67 §3) against a throwaway slapd.
#
#   bash tests/ldap/run.sh
#   AIML_WRAP="valgrind --leak-check=full" bash tests/ldap/run.sh   # memory check
#
# Starts slapd (mdb backend, own temp dir, 127.0.0.1:<port>) with two users and
# a group, runs tests/ldap/ldap_login.aiml against it, stops slapd.  Exit 0 iff
# the probe passes.  Skips (exit 0, with a note) when slapd is not installed —
# the pure parts (escaping) are covered by tests/test_ldap_escape.aiml.
set -uo pipefail
cd "$(dirname "$0")/../.."                 # → aiml project root
AIML="${AIML:-out/aiml/aiml}"
SLAPD="${SLAPD:-$(command -v slapd || echo /usr/sbin/slapd)}"
if [ ! -x "$SLAPD" ] || ! command -v slapadd >/dev/null 2>&1 && [ ! -x /usr/sbin/slapadd ]; then
    echo "SKIP: slapd not installed"; exit 0
fi
SLAPADD="$(command -v slapadd || echo /usr/sbin/slapadd)"
SLAPPASSWD="$(command -v slappasswd || echo /usr/sbin/slappasswd)"
PORT="${LDAP_TEST_PORT:-23389}"
WORK="$(mktemp -d)"
cleanup() { [ -f "$WORK/slapd.pid" ] && kill "$(cat "$WORK/slapd.pid")" 2>/dev/null; sleep 0.3; rm -rf "$WORK"; }
trap cleanup EXIT
mkdir -p "$WORK/db"

MODLINE=""
[ -e /usr/lib/ldap/back_mdb.so ] || [ -e /usr/lib/ldap/back_mdb.la ] && MODLINE="modulepath /usr/lib/ldap
moduleload back_mdb"
cat > "$WORK/slapd.conf" <<CONF
include /etc/ldap/schema/core.schema
include /etc/ldap/schema/cosine.schema
include /etc/ldap/schema/inetorgperson.schema
pidfile $WORK/slapd.pid
$MODLINE
database mdb
maxsize 10485760
suffix "dc=example,dc=org"
rootdn "cn=admin,dc=example,dc=org"
rootpw admin-secret
directory $WORK/db
access to attrs=userPassword by anonymous auth by * none
access to * by * read
CONF

ALICE_PW=$("$SLAPPASSWD" -s 'alice-pw')
BOB_PW=$("$SLAPPASSWD" -s 'bob-pw')
cat > "$WORK/data.ldif" <<LDIF
dn: dc=example,dc=org
objectClass: dcObject
objectClass: organization
o: Example
dc: example

dn: ou=people,dc=example,dc=org
objectClass: organizationalUnit
ou: people

dn: ou=groups,dc=example,dc=org
objectClass: organizationalUnit
ou: groups

dn: uid=alice,ou=people,dc=example,dc=org
objectClass: inetOrgPerson
uid: alice
cn: Alice Example
sn: Example
mail: alice@example.org
userPassword: $ALICE_PW

dn: uid=bob,ou=people,dc=example,dc=org
objectClass: inetOrgPerson
uid: bob
cn: Bob (Ops), Example
sn: Example
mail: bob@example.org
userPassword: $BOB_PW

dn: cn=devs,ou=groups,dc=example,dc=org
objectClass: groupOfNames
cn: devs
member: uid=alice,ou=people,dc=example,dc=org
LDIF

if ! "$SLAPADD" -f "$WORK/slapd.conf" -l "$WORK/data.ldif" >"$WORK/slapadd.log" 2>&1; then
    echo "FAIL: slapadd"; cat "$WORK/slapadd.log"; exit 1
fi
"$SLAPD" -f "$WORK/slapd.conf" -h "ldap://127.0.0.1:$PORT/" >"$WORK/slapd.log" 2>&1
for _ in $(seq 50); do [ -f "$WORK/slapd.pid" ] && break; sleep 0.1; done
if [ ! -f "$WORK/slapd.pid" ]; then echo "FAIL: slapd did not start"; cat "$WORK/slapd.log"; exit 1; fi
sleep 0.3

out=$(AIML_LIFETIME_DROPS=1 AIML_MEM_V3=1 ${AIML_WRAP:-} "$AIML" tests/ldap/ldap_login.aiml -- "ldap://127.0.0.1:$PORT" 2>&1); rc=$?
echo "$out" | grep -v WARNING
exit $rc
