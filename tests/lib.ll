; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [22 x i8] c"Inside my.lib.add, a=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@1 = private unnamed_addr constant [5 x i8] c", b=\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @my.lib.add, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @my.lib.add]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_add = private constant [4 x i8] c"add\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_add]
@__aiml_func_count = constant i32 1
@__aiml_schema_count = constant i32 0

define ptr @my.lib.add(i32 %0, i32 %1) personality ptr @__gxx_personality_v0 {
entry:
  %a = alloca i32, align 4
  store i32 %0, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 %1, ptr %b, align 4
  %a_val = load i32, ptr %a, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %a_val)
  %3 = call ptr @__aiml_str_concat(ptr @0, ptr %2)
  %4 = call ptr @__aiml_str_concat(ptr %3, ptr @1)
  %b_val = load i32, ptr %b, align 4
  %5 = call ptr @__aiml_int_to_string(i32 %b_val)
  %6 = call ptr @__aiml_str_concat(ptr %4, ptr %5)
  %print_null_check = icmp eq ptr %6, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %6
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %a_val1 = load i32, ptr %a, align 4
  %b_val2 = load i32, ptr %b, align 4
  %7 = add i32 %a_val1, %b_val2
  %8 = call ptr @__aiml_int_to_string(i32 %7)
  ret ptr %8

ret_dead:                                         ; No predecessors!
  ret ptr null
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @malloc(i64)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare i64 @strlen(ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

; Function Attrs: noinline
define internal ptr @__aiml_str_concat(ptr %0, ptr %1) #0 {
entry:
  %2 = icmp eq ptr %0, null
  %3 = select i1 %2, ptr @empty, ptr %0
  %4 = icmp eq ptr %1, null
  %5 = select i1 %4, ptr @empty, ptr %1
  %6 = call i64 @strlen(ptr %3)
  %7 = call i64 @strlen(ptr %5)
  %8 = add i64 %6, %7
  %9 = add i64 %8, 1
  %10 = call ptr @malloc(i64 %9)
  %11 = call ptr @strcpy(ptr %10, ptr %3)
  %12 = call ptr @strcat(ptr %10, ptr %5)
  ret ptr %10
}

declare i32 @printf(ptr, ...)

attributes #0 = { noinline }
