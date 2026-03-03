# Function Pointers

Funktionszeiger sind erstklassige Werte in AIML. Der Typ wird mit `fn<>` notiert.

## Syntax

```
fn<>                          # keine Parameter, kein Rückgabewert
fn<i32>                       # ein Parameter, kein Rückgabewert
fn<i32, string>               # zwei Parameter, kein Rückgabewert
fn<:i32>                      # keine Parameter, gibt i32 zurück
fn<i32:string>                # ein Parameter, gibt string zurück
fn<i32, string:bool>          # zwei Parameter, gibt bool zurück
```

Parameter sind positional und nur durch ihren Typ beschrieben.
Der Rückgabetyp wird mit `:` nach dem letzten Parameter angegeben.
Ohne `:` ist der Rückgabewert `void`.

## Deklaration und Zuweisung

```aiml
- def:
    name: add
    args: [[a, i32], [b, i32]]
    return: i32
    body:
      - return: { +: [a, b] }

- decl: [fp, fn<i32, i32:i32>]
- set: [fp, add]

- decl: [result, i32]
- set: [result, { call: [fp, 3, 4] }]
# result == 7
```

## Aufruf

Ein Funktionszeiger wird mit `call` aufgerufen — identisch zu direkten Funktionsaufrufen:

```aiml
- call: [fp, arg1, arg2]
- set: [x, { call: [fp, arg1] }]
```

## Als Parameter

```aiml
- def:
    name: apply
    args: [[f, fn<i32:i32>], [x, i32]]
    return: i32
    body:
      - return: { call: [f, x] }

- def:
    name: double
    args: [[n, i32]]
    return: i32
    body:
      - return: { *: [n, 2] }

- decl: [r, i32]
- set: [r, { call: [apply, double, 5] }]
# r == 10
```

## In Collections

```aiml
- decl: [handlers, map<string:fn<http_request, http_response>>]
- call: [handlers.put, "home", handle_home]
- call: [handlers.put, "about", handle_about]

- decl: [h, fn<http_request, http_response>]
- set: [h, { call: [handlers.get, "home"] }]
- call: [h, req, resp]
```

## Null-Prüfung

Ein nicht initialisierter Funktionszeiger ist `null`.
Der Aufruf eines `null`-Zeigers löst eine `NullPointerException` aus.

```aiml
- decl: [cb, fn<>]
# cb ist null

- if: [{ !=: [cb, null] }, true]
  then:
    - call: [cb]
```

## Nested Generics

Typen mit eigenen Typparametern funktionieren als Parameter und Rückgabewert:

```aiml
fn<list<i32>:i32>             # nimmt list<i32>, gibt i32 zurück
fn<buffer<u8>>                # nimmt buffer<u8>, kein Rückgabewert
fn<map<string:string>:bool>   # nimmt map, gibt bool — : nach > eindeutig
```

## Typübereinstimmung

Zwei Funktionszeiger-Typen sind identisch, wenn Parameteranzahl, Parametertypen (in Reihenfolge)
und Rückgabetyp übereinstimmen. Keinerlei implizite Konvertierung.

```aiml
- def:
    name: greet
    args: [[name, string]]
    return: string
    body:
      - return: "Hello ${name}"

- decl: [f, fn<string:string>]
- set: [f, greet]  # OK

- decl: [g, fn<i32:string>]
- set: [g, greet]  # Fehler: Parametertyp stimmt nicht überein
```
