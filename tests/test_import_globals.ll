; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [16 x i8] c"A counter_init=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [22 x i8] c"B counter_after_3inc=\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [6 x i8] c"world\00", align 1
@5 = private unnamed_addr constant [12 x i8] c"C greeting=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"hey\00", align 1
@8 = private unnamed_addr constant [6 x i8] c"there\00", align 1
@9 = private unnamed_addr constant [13 x i8] c"D greeting2=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [22 x i8] c"[PASS] A counter_init\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [38 x i8] c"[FAIL] A counter_init expected 0 got \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [28 x i8] c"[PASS] B counter_after_3inc\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [44 x i8] c"[FAIL] B counter_after_3inc expected 3 got \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"---\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [9 x i8] c"Passed: \00", align 1
@22 = private unnamed_addr constant [4 x i8] c" / \00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [9 x i8] c"Failed: \00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [5 x [16 x ptr]] [[16 x ptr] [ptr @testlib.get_counter, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @testlib.inc_counter, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @testlib.get_greeting, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @testlib.set_prefix, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [5 x ptr] [ptr @testlib.get_counter, ptr @testlib.inc_counter, ptr @testlib.get_greeting, ptr @testlib.set_prefix, ptr @main]
@__aiml_fptable_versions = global [5 x i32] [i32 1, i32 1, i32 1, i32 1, i32 1]
@__aiml_fname_get_counter = private constant [12 x i8] c"get_counter\00"
@__aiml_fname_inc_counter = private constant [12 x i8] c"inc_counter\00"
@__aiml_fname_get_greeting = private constant [13 x i8] c"get_greeting\00"
@__aiml_fname_set_prefix = private constant [11 x i8] c"set_prefix\00"
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [5 x ptr] [ptr @__aiml_fname_get_counter, ptr @__aiml_fname_inc_counter, ptr @__aiml_fname_get_greeting, ptr @__aiml_fname_set_prefix, ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 5
@__aiml_schema_count = constant i32 0

declare i32 @testlib.get_counter()

declare void @testlib.inc_counter()

declare ptr @testlib.get_greeting(ptr)

declare void @testlib.set_prefix(ptr)

define void @__aiml_init() {
entry:
  ret void
}

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %c0 = alloca i32, align 4
  store i32 0, ptr %c0, align 4
  %0 = call i32 @testlib.get_counter()
  store i32 %0, ptr %c0, align 4
  %c0_val = load i32, ptr %c0, align 4
  %1 = call ptr @__aiml_int_to_string(i32 %c0_val)
  %2 = call ptr @__aiml_str_concat(ptr @0, ptr %1)
  %print_null_check = icmp eq ptr %2, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %2
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr %print_safe_str)
  call void @testlib.inc_counter()
  call void @testlib.inc_counter()
  call void @testlib.inc_counter()
  %c3 = alloca i32, align 4
  store i32 0, ptr %c3, align 4
  %3 = call i32 @testlib.get_counter()
  store i32 %3, ptr %c3, align 4
  %c3_val = load i32, ptr %c3, align 4
  %4 = call ptr @__aiml_int_to_string(i32 %c3_val)
  %5 = call ptr @__aiml_str_concat(ptr @2, ptr %4)
  %print_null_check1 = icmp eq ptr %5, null
  %print_safe_str2 = select i1 %print_null_check1, ptr @print_null_str.1, ptr %5
  %print_call3 = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str2)
  %g1 = alloca ptr, align 8
  store ptr null, ptr %g1, align 8
  %6 = call ptr @testlib.get_greeting(ptr @4)
  store ptr %6, ptr %g1, align 8
  %g1_val = load ptr, ptr %g1, align 8
  %7 = call ptr @__aiml_str_concat(ptr @5, ptr %g1_val)
  %print_null_check4 = icmp eq ptr %7, null
  %print_safe_str5 = select i1 %print_null_check4, ptr @print_null_str.2, ptr %7
  %print_call6 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str5)
  call void @testlib.set_prefix(ptr @7)
  %g2 = alloca ptr, align 8
  store ptr null, ptr %g2, align 8
  %8 = call ptr @testlib.get_greeting(ptr @8)
  store ptr %8, ptr %g2, align 8
  %g2_val = load ptr, ptr %g2, align 8
  %9 = call ptr @__aiml_str_concat(ptr @9, ptr %g2_val)
  %print_null_check7 = icmp eq ptr %9, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.3, ptr %9
  %print_call9 = call i32 (ptr, ...) @printf(ptr @10, ptr %print_safe_str8)
  %pass = alloca i32, align 4
  store i32 0, ptr %pass, align 4
  store i32 0, ptr %pass, align 4
  %fail = alloca i32, align 4
  store i32 0, ptr %fail, align 4
  store i32 0, ptr %fail, align 4
  %c0_val10 = load i32, ptr %c0, align 4
  %10 = icmp eq i32 %c0_val10, 0
  br i1 %10, label %if_then, label %if_else

if_then:                                          ; preds = %entry
  %print_call11 = call i32 (ptr, ...) @printf(ptr @12, ptr @11)
  %pass_val = load i32, ptr %pass, align 4
  %11 = add i32 %pass_val, 1
  store i32 %11, ptr %pass, align 4
  br label %if_merge

if_else:                                          ; preds = %entry
  %c0_val12 = load i32, ptr %c0, align 4
  %12 = call ptr @__aiml_int_to_string(i32 %c0_val12)
  %13 = call ptr @__aiml_str_concat(ptr @13, ptr %12)
  %print_null_check13 = icmp eq ptr %13, null
  %print_safe_str14 = select i1 %print_null_check13, ptr @print_null_str.5, ptr %13
  %print_call15 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str14)
  %fail_val = load i32, ptr %fail, align 4
  %14 = add i32 %fail_val, 1
  store i32 %14, ptr %fail, align 4
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  %c3_val16 = load i32, ptr %c3, align 4
  %15 = icmp eq i32 %c3_val16, 3
  br i1 %15, label %if_then17, label %if_else18

if_then17:                                        ; preds = %if_merge
  %print_call20 = call i32 (ptr, ...) @printf(ptr @16, ptr @15)
  %pass_val21 = load i32, ptr %pass, align 4
  %16 = add i32 %pass_val21, 1
  store i32 %16, ptr %pass, align 4
  br label %if_merge19

if_else18:                                        ; preds = %if_merge
  %c3_val22 = load i32, ptr %c3, align 4
  %17 = call ptr @__aiml_int_to_string(i32 %c3_val22)
  %18 = call ptr @__aiml_str_concat(ptr @17, ptr %17)
  %print_null_check23 = icmp eq ptr %18, null
  %print_safe_str24 = select i1 %print_null_check23, ptr @print_null_str.7, ptr %18
  %print_call25 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str24)
  %fail_val26 = load i32, ptr %fail, align 4
  %19 = add i32 %fail_val26, 1
  store i32 %19, ptr %fail, align 4
  br label %if_merge19

if_merge19:                                       ; preds = %if_else18, %if_then17
  %print_call27 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
  %pass_val28 = load i32, ptr %pass, align 4
  %20 = call ptr @__aiml_int_to_string(i32 %pass_val28)
  %21 = call ptr @__aiml_str_concat(ptr @21, ptr %20)
  %22 = call ptr @__aiml_str_concat(ptr %21, ptr @22)
  %pass_val29 = load i32, ptr %pass, align 4
  %fail_val30 = load i32, ptr %fail, align 4
  %23 = add i32 %pass_val29, %fail_val30
  %24 = call ptr @__aiml_int_to_string(i32 %23)
  %25 = call ptr @__aiml_str_concat(ptr %22, ptr %24)
  %print_null_check31 = icmp eq ptr %25, null
  %print_safe_str32 = select i1 %print_null_check31, ptr @print_null_str.9, ptr %25
  %print_call33 = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str32)
  %fail_val34 = load i32, ptr %fail, align 4
  %26 = call ptr @__aiml_int_to_string(i32 %fail_val34)
  %27 = call ptr @__aiml_str_concat(ptr @24, ptr %26)
  %print_null_check35 = icmp eq ptr %27, null
  %print_safe_str36 = select i1 %print_null_check35, ptr @print_null_str.10, ptr %27
  %print_call37 = call i32 (ptr, ...) @printf(ptr @25, ptr %print_safe_str36)
  %fail_val38 = load i32, ptr %fail, align 4
  ret i32 %fail_val38
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
