Kurzfassung:
LLVM selbst kann nicht schlafen und kennt keine Uhr.
LLVM erzeugt nur Code. Die eigentliche Zeitmagie passiert durch:

Aufruf von externen Funktionen

oder durch eigene Runtime-Wrapper

Die elegante Lösung ist fast immer: Wrapper-Funktionen deklarieren und per call oder invoke benutzen.

🛌 Sleep verwenden
Option 1: Direkt libc sleep aufrufen

Unter Unix gibt es:

unsigned int sleep(unsigned int seconds);


In LLVM IR deklarierst du einfach:

FunctionType* sleepTy =
    FunctionType::get(builder.getInt32Ty(),
                      {builder.getInt32Ty()},
                      false);

Function* sleepFn =
    Function::Create(sleepTy,
                     Function::ExternalLinkage,
                     "sleep",
                     module);


Und dann:

builder.CreateCall(sleepFn, builder.getInt32(2));


Das war’s. Beim Linken wird libc das auflösen.

Wenn du Exceptions unterstützen willst, kannst du auch CreateInvoke benutzen.

⏱ Unix Timestamp holen

libc hat:

time_t time(time_t* tloc);


Meist ist time_t ein long.

Declaration in LLVM:

FunctionType* timeTy =
    FunctionType::get(builder.getInt64Ty(),
                      {builder.getInt64Ty()->getPointerTo()},
                      false);

Function* timeFn =
    Function::Create(timeTy,
                     Function::ExternalLinkage,
                     "time",
                     module);


Aufruf mit nullptr:

Value* nullPtr =
    ConstantPointerNull::get(
        builder.getInt64Ty()->getPointerTo());

Value* ts = builder.CreateCall(timeFn, nullPtr);


ts enthält jetzt den Unix-Timestamp.

🎩 Eleganter: Eigene Runtime-Wrappers

Ich empfehle meist eine kleine Runtime-Datei:

extern "C" long my_timestamp() {
    return std::time(nullptr);
}

extern "C" void my_sleep(int sec) {
    sleep(sec);
}


Warum?

Du kapselst Plattformdetails

Du kontrollierst Typen

Du kannst später Verhalten ändern

Dein IR bleibt sauber

Dann deklarierst du nur:

declare i64 @my_timestamp()
declare void @my_sleep(i32)


Das ist deutlich stabiler.

🧠 Warum Wrapper oft besser sind

Direkt libc aufzurufen ist simpel, aber:

time_t kann 32 oder 64 Bit sein

sleep kann von Signalen unterbrochen werden

Manche Systeme nutzen nanosleep

Plattformunterschiede schleichen sich ein

Wrapper isolieren das.

Dein LLVM-Code bleibt neutral und minimalistisch.

🚀 Wenn du JIT benutzt

Wenn du ORC JIT verwendest, musst du:

jit->getMainJITDylib().addGenerator(
    cantFail(DynamicLibrarySearchGenerator::GetForCurrentProcess(
        jit->getDataLayout().getGlobalPrefix()
    ))
);


Damit findet der JIT sleep oder time im aktuellen Prozess.

🧩 Architektur-Empfehlung

Für eine eigene Sprache oder VM:

[Dein IR]
     ↓
[Runtime API Layer]
     ↓
[libc / syscalls]


LLVM bleibt Codegenerator.
Die Runtime ist dein Betriebssystem-Flüster