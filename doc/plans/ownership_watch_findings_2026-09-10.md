# Findings aus der laufenden Ownership-Überwachung

Stand: 2026-09-10, Prüfung nach Watcher-Meldungen bis 06:31 Uhr. Direkte Quelltextprüfung, kein Git, keine ausgeführten Repros. Zeilen beziehen sich auf den gelesenen Arbeitsbaum; parallel laufende Änderungen können sie verschieben.

## W1 — P1: Fehlende Analyse erlaubt weiterhin ungeprüfte Drops

`src/core/ast_compiler_stmt.cpp:1575–1578`: mayDrop wird nur bei vorhandenem Plan, Exit-Fakt und von null verschiedener ID ausgewertet. Fehlt eine Voraussetzung, wird trotzdem freigegeben. Insbesondere fehlt in `ownership_analyzer.cpp:58–72,106–171` die Behandlung von TryStmt/Catch-Blöcken. Deren Deklarationen bekommen keine IDs/Exit-Fakten und fallen dadurch auf die alte unsichere Freigabe zurück. Auch globale suppressFunctionDrops kann für diese Kandidaten umgangen werden.

Vorschlag: Fehlender Besitznachweis darf keinen neuen automatischen Drop erlauben. Try/Catch analysieren oder die nicht bewiesenen Kandidaten konservativ unterdrücken. Consumed-Parameter brauchen einen expliziten Vertrag statt eines ID-0-Bypasses.

## W2 — P1: Fremder Heap in Modellfeldern bleibt fälschlich Owned

`ownership_analyzer.cpp:85–89,123–143`: Feld-/Indexziele werden ausgelassen. Ein frisch deklarierter Modell-Owner bleibt Owned, auch wenn ein Feld per flacher Zuweisung einen fremden inneren Container übernimmt. Der rekursive Modell-Drop kann diesen Buffer weiterhin freigeben; die andere Besitzkette wird nicht erfasst. Nur die Neuzuweisung des gesamten lokalen Slots auf Unknown zu setzen behebt diesen Fall nicht.

Vorschlag: Feld-/Container-Ressourcen mit dem wirklichen Owner verknüpfen; bei ungeklärter Feldbindung mindestens den rekursiven Drop des Ziel-Owners unterdrücken. Beide beteiligten Owner und die Lebensdauer bestehender Views berücksichtigen.

## W3 — P1: Calls und Bedingungen werden als effektfrei behandelt

`ownership_analyzer.cpp:145–170`: Call-Ausdrücke, Bedingungen und unbekannte Statements aktualisieren keinen Besitzstatus. Eine Funktion kann über einen by-ref-Listenparameter die frisch deklarierte Liste auf einen fremden Buffer umbiegen. Der Analyzer lässt sie Owned und erlaubt später die falsche Freigabe. Auch retained/shared Modellparameter über Funktionsgrenzen bleiben unerkannt.

Vorschlag: aufgelöste Call-Effekte anwenden; ohne Vertrag mögliche Mutation/Retention konservativ berücksichtigen. Vorhandenes consumeLocalDrop beim Lowering ersetzt keinen Alias-/Call-Datenfluss.

## Einordnung

Die neue Anbindung schützt einen Teil der direkten lokalen Rebind-Fälle. Sie ist noch kein allgemeiner Owned/Borrowed/SharedView-Analyzer. Die Bezeichnung „fail-safe, nur Leaks“ ist für die drei obigen Pfade nicht belegt. Findings werden während der weiteren Überwachung erneut geprüft; sie sind kein Abbruch der Prüfung.
