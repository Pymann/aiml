; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@1 = private unnamed_addr constant [2 x i8] c" \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@__aiml_fptable = global [4 x [16 x ptr]] [[16 x ptr] [ptr @testlib.get_counter, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @testlib.inc_counter, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @testlib.get_greeting, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @testlib.set_prefix, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [4 x ptr] [ptr @testlib.get_counter, ptr @testlib.inc_counter, ptr @testlib.get_greeting, ptr @testlib.set_prefix]
@__aiml_fptable_versions = global [4 x i32] [i32 1, i32 1, i32 1, i32 1]
@__aiml_fname_get_counter = private constant [12 x i8] c"get_counter\00"
@__aiml_fname_inc_counter = private constant [12 x i8] c"inc_counter\00"
@__aiml_fname_get_greeting = private constant [13 x i8] c"get_greeting\00"
@__aiml_fname_set_prefix = private constant [11 x i8] c"set_prefix\00"
@__aiml_func_names = constant [4 x ptr] [ptr @__aiml_fname_get_counter, ptr @__aiml_fname_inc_counter, ptr @__aiml_fname_get_greeting, ptr @__aiml_fname_set_prefix]
@__aiml_func_count = constant i32 4
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %counter = alloca i32, align 4
  store i32 0, ptr %counter, align 4
  %prefix = alloca ptr, align 8
  store ptr null, ptr %prefix, align 8
  store ptr @0, ptr %prefix, align 8
  ret void
}

define i32 @testlib.get_counter() personality ptr @__gxx_personality_v0 {
entry:
  %counter_val = load i32, ptr %counter, align 4
  ret i32 %counter_val
}

declare i32 @__gxx_personality_v0(...)

define void @testlib.inc_counter() personality ptr @__gxx_personality_v0 {
entry:
  %counter_val = load i32, ptr %counter, align 4
  %0 = add i32 %counter_val, 1
  store i32 %0, ptr %counter, align 4
  ret void
}

define ptr @testlib.get_greeting(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %name = alloca ptr, align 8
  store ptr %0, ptr %name, align 8
  %prefix_val = load ptr, ptr %prefix, align 8
  %1 = call ptr @__aiml_str_concat(ptr %prefix_val, ptr @1)
  %name_val = load ptr, ptr %name, align 8
  %2 = call ptr @__aiml_str_concat(ptr %1, ptr %name_val)
  ret ptr %2
}

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

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

define void @testlib.set_prefix(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %p = alloca ptr, align 8
  store ptr %0, ptr %p, align 8
  %p_val = load ptr, ptr %p, align 8
  store ptr %p_val, ptr %prefix, align 8
  ret void
}

attributes #0 = { noinline }
