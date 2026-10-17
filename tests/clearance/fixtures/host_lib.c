/* host_lib.c — ein WIRT als Shared Library (spec/65 F65-24, zweiter Weg).
 *
 * spec/65 kannte bisher nur einen Weg, wie eine ADRESSE zum Skript kommt:
 * `sys.export_symbol` im selben Prozess (Probe 09).  Ein echter Wirt, der fremden
 * Quelltext uebersetzen laesst, kann das nicht — er IST der Uebersetzer, und der kennt
 * das Skript erst zur Laufzeit.  Dort kommt die Adresse aus einer vorgeladenen
 * Bibliothek, und Name + Level aus der Export-Tabelle.
 *
 * Diese Datei ist der kleinste Wirt, der das pruefbar macht.  cosmos doc/spec/108 F108-9
 * baut darauf. */
int clearance_host_answer(void) { return 42; }

/* Auf Level 2 angeboten: eine Sitzung mit Freigabe 7 darf den Namen nicht nennen,
 * obwohl die Adresse im Prozess vorhanden ist.  Das ist der Punkt — die Zulassung
 * haengt an der Tabelle, nicht an der Erreichbarkeit des Symbols. */
int clearance_host_secret(void) { return 7; }
