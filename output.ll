; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"

@fmt_int = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1

define i32 @main() {
entry:
  %answer = alloca i32, align 4
  store i32 42, ptr %answer, align 4
  %answer_val = load i32, ptr %answer, align 4
  %0 = call i32 (ptr, ...) @printf(ptr @fmt_int, i32 %answer_val)
  ret i32 0
}

declare i32 @printf(ptr, ...)
