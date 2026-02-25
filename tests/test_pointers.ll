; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [13 x i8] c"Original X: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [20 x i8] c"Value via pointer: \00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [25 x i8] c"Modified X via pointer: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [25 x i8] c"Value of X via PP (??): \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [34 x i8] c"Value via pp_temp (should be x): \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [31 x i8] c"Modified X via PP deep store: \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %x = alloca i32, align 4
  store i32 0, ptr %x, align 4
  store i32 10, ptr %x, align 4
  %x_val = load i32, ptr %x, align 4
  %0 = call ptr @__aiml_int_to_string(i32 %x_val)
  %1 = call ptr @__aiml_str_concat(ptr @0, ptr %0)
  %print_null_check = icmp eq ptr %1, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %1
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr %print_safe_str)
  %p = alloca ptr, align 8
  store ptr null, ptr %p, align 8
  %x_val1 = load i32, ptr %x, align 4
  store i32 %x_val1, ptr %p, align 4
  %p_val = load ptr, ptr %p, align 8
  %deref = load i32, ptr %p_val, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %deref)
  %3 = call ptr @__aiml_str_concat(ptr @2, ptr %2)
  %print_null_check2 = icmp eq ptr %3, null
  %print_safe_str3 = select i1 %print_null_check2, ptr @print_null_str.1, ptr %3
  %print_call4 = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str3)
  %x_val5 = load i32, ptr %x, align 4
  %4 = call ptr @__aiml_int_to_string(i32 %x_val5)
  %5 = call ptr @__aiml_str_concat(ptr @4, ptr %4)
  %print_null_check6 = icmp eq ptr %5, null
  %print_safe_str7 = select i1 %print_null_check6, ptr @print_null_str.2, ptr %5
  %print_call8 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str7)
  %pp = alloca ptr, align 8
  store ptr null, ptr %pp, align 8
  %p_val9 = load ptr, ptr %p, align 8
  store ptr %p_val9, ptr %pp, align 8
  %pp_val = load ptr, ptr %pp, align 8
  %deref10 = load ptr, ptr %pp_val, align 8
  %deref11 = load i32, ptr %deref10, align 4
  %6 = call ptr @__aiml_int_to_string(i32 %deref11)
  %7 = call ptr @__aiml_str_concat(ptr @6, ptr %6)
  %print_null_check12 = icmp eq ptr %7, null
  %print_safe_str13 = select i1 %print_null_check12, ptr @print_null_str.3, ptr %7
  %print_call14 = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str13)
  %pp_temp = alloca ptr, align 8
  store ptr null, ptr %pp_temp, align 8
  %x_val15 = load i32, ptr %x, align 4
  store i32 %x_val15, ptr %pp_temp, align 4
  %pp_temp_val = load ptr, ptr %pp_temp, align 8
  %deref16 = load ptr, ptr %pp_temp_val, align 8
  %deref17 = load i32, ptr %deref16, align 4
  %8 = call ptr @__aiml_int_to_string(i32 %deref17)
  %9 = call ptr @__aiml_str_concat(ptr @8, ptr %8)
  %print_null_check18 = icmp eq ptr %9, null
  %print_safe_str19 = select i1 %print_null_check18, ptr @print_null_str.4, ptr %9
  %print_call20 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str19)
  %x_val21 = load i32, ptr %x, align 4
  %10 = call ptr @__aiml_int_to_string(i32 %x_val21)
  %11 = call ptr @__aiml_str_concat(ptr @10, ptr %10)
  %print_null_check22 = icmp eq ptr %11, null
  %print_safe_str23 = select i1 %print_null_check22, ptr @print_null_str.5, ptr %11
  %print_call24 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str23)
  ret void
}

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
