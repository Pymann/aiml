Der Fehler

Instruction does not dominate all uses!
%153 = add i32 %148, 1
store i32 %153, ptr %i_rm, align 4


bedeutet nicht Speicherfehler – sondern ein SSA-Form Fehler im Control-Flow.

LLVM verlangt:

Eine Definition muss alle ihre Verwendungen dominieren.

Das heißt:
Die Variable %153 wird in einem Basic Block benutzt, der nicht garantiert nach der Definition ausgeführt wird.

🔎 Was heißt „dominieren“?

Block A dominiert Block B, wenn jeder Weg zum Block B durch A führt.

Typischer Fehler:

br i1 %cond, label %then, label %else

then:
  %x = add i32 %a, 1
  br label %merge

else:
  br label %merge

merge:
  store i32 %x, ptr %ptr   ; ❌ FEHLER


Problem:
%x existiert nur im then-Block,
aber wird im merge-Block benutzt.

Wenn else ausgeführt wird → %x existiert nicht.

LLVM sagt dann:

Instruction does not dominate all uses!

💥 Lösung: PHI-Node benutzen

Richtig wäre:

br i1 %cond, label %then, label %else

then:
  %x_then = add i32 %a, 1
  br label %merge

else:
  %x_else = add i32 %a, 0
  br label %merge

merge:
  %x = phi i32 [ %x_then, %then ], [ %x_else, %else ]
  store i32 %x, ptr %ptr

🎯 Was bei dir sehr wahrscheinlich passiert

Du hast vermutlich sowas wie:

loop:
  br i1 %cond, label %body, label %end

body:
  %153 = add i32 %148, 1
  br label %loop

end:
  store i32 %153, ptr %i_rm   ; ❌


Wenn die Schleife nie betreten wird,
existiert %153 nicht → Dominance-Fehler.

🛠 Konkrete Fix-Strategien
✅ Fall 1: Wert nur im selben Block benutzen

Dann store im gleichen Block lassen.

✅ Fall 2: Wert nach einem if verwenden

→ PHI-Node einfügen.

✅ Fall 3: Schleifen-Zähler

Bei Loops brauchst du IMMER PHI:

Richtiges Loop-Muster:

entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %next, %loop ]
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, 10
  br i1 %cond, label %loop, label %end

end:
  ret void

🧠 Warum tritt das oft bei selbstgebauten Maps auf?

Weil man:

Zähler in einem if berechnet

ihn aber außerhalb speichert

oder in einer Schleife inkrementiert

aber nach dem Loop weiterverwendet

LLVM ist extrem strikt bei SSA.

🔍 Wie findest du die Stelle?

Gib dir den IR mit Blocknamen aus:

opt -verify test_map.ll


oder

llvm-as test_map.ll


Das zeigt oft die exakte Stelle.

🚨 WICHTIG

Das ist KEIN Laufzeitfehler.
Das Modul ist invalid IR.
Deshalb sagt lli:

Bad module


LLVM führt es gar nicht erst aus.