; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [20 x i8] c"Phase A: TRIANGLES=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@1 = private unnamed_addr constant [8 x i8] c" LINES=\00", align 1
@2 = private unnamed_addr constant [9 x i8] c" POINTS=\00", align 1
@3 = private unnamed_addr constant [17 x i8] c" TRIANGLE_STRIP=\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [11 x i8] c"Phase A OK\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [17 x i8] c"Phase B: REPEAT=\00", align 1
@8 = private unnamed_addr constant [16 x i8] c" CLAMP_TO_EDGE=\00", align 1
@9 = private unnamed_addr constant [18 x i8] c" MIRRORED_REPEAT=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [17 x i8] c"Phase B: LINEAR=\00", align 1
@12 = private unnamed_addr constant [10 x i8] c" NEAREST=\00", align 1
@13 = private unnamed_addr constant [23 x i8] c" LINEAR_MIPMAP_LINEAR=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [11 x i8] c"Phase B OK\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [20 x i8] c"All gl tests passed\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %tri = alloca i32, align 4
  store i32 0, ptr %tri, align 4
  store i32 4, ptr %tri, align 4
  %lin = alloca i32, align 4
  store i32 0, ptr %lin, align 4
  store i32 1, ptr %lin, align 4
  %pts = alloca i32, align 4
  store i32 0, ptr %pts, align 4
  store i32 0, ptr %pts, align 4
  %ts = alloca i32, align 4
  store i32 0, ptr %ts, align 4
  store i32 5, ptr %ts, align 4
  %tri_val = load i32, ptr %tri, align 4
  %0 = call ptr @__aiml_int_to_string(i32 %tri_val)
  %1 = call ptr @__aiml_str_concat(ptr @0, ptr %0)
  %2 = call ptr @__aiml_str_concat(ptr %1, ptr @1)
  %lin_val = load i32, ptr %lin, align 4
  %3 = call ptr @__aiml_int_to_string(i32 %lin_val)
  %4 = call ptr @__aiml_str_concat(ptr %2, ptr %3)
  %5 = call ptr @__aiml_str_concat(ptr %4, ptr @2)
  %pts_val = load i32, ptr %pts, align 4
  %6 = call ptr @__aiml_int_to_string(i32 %pts_val)
  %7 = call ptr @__aiml_str_concat(ptr %5, ptr %6)
  %8 = call ptr @__aiml_str_concat(ptr %7, ptr @3)
  %ts_val = load i32, ptr %ts, align 4
  %9 = call ptr @__aiml_int_to_string(i32 %ts_val)
  %10 = call ptr @__aiml_str_concat(ptr %8, ptr %9)
  %print_null_check = icmp eq ptr %10, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %10
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr %print_safe_str)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @6, ptr @5)
  %rep = alloca i32, align 4
  store i32 0, ptr %rep, align 4
  store i32 10497, ptr %rep, align 4
  %cte = alloca i32, align 4
  store i32 0, ptr %cte, align 4
  store i32 33071, ptr %cte, align 4
  %mr = alloca i32, align 4
  store i32 0, ptr %mr, align 4
  store i32 33648, ptr %mr, align 4
  %rep_val = load i32, ptr %rep, align 4
  %11 = call ptr @__aiml_int_to_string(i32 %rep_val)
  %12 = call ptr @__aiml_str_concat(ptr @7, ptr %11)
  %13 = call ptr @__aiml_str_concat(ptr %12, ptr @8)
  %cte_val = load i32, ptr %cte, align 4
  %14 = call ptr @__aiml_int_to_string(i32 %cte_val)
  %15 = call ptr @__aiml_str_concat(ptr %13, ptr %14)
  %16 = call ptr @__aiml_str_concat(ptr %15, ptr @9)
  %mr_val = load i32, ptr %mr, align 4
  %17 = call ptr @__aiml_int_to_string(i32 %mr_val)
  %18 = call ptr @__aiml_str_concat(ptr %16, ptr %17)
  %print_null_check2 = icmp eq ptr %18, null
  %print_safe_str3 = select i1 %print_null_check2, ptr @print_null_str.2, ptr %18
  %print_call4 = call i32 (ptr, ...) @printf(ptr @10, ptr %print_safe_str3)
  %lin2 = alloca i32, align 4
  store i32 0, ptr %lin2, align 4
  store i32 9729, ptr %lin2, align 4
  %near = alloca i32, align 4
  store i32 0, ptr %near, align 4
  store i32 9728, ptr %near, align 4
  %lml = alloca i32, align 4
  store i32 0, ptr %lml, align 4
  store i32 9987, ptr %lml, align 4
  %lin2_val = load i32, ptr %lin2, align 4
  %19 = call ptr @__aiml_int_to_string(i32 %lin2_val)
  %20 = call ptr @__aiml_str_concat(ptr @11, ptr %19)
  %21 = call ptr @__aiml_str_concat(ptr %20, ptr @12)
  %near_val = load i32, ptr %near, align 4
  %22 = call ptr @__aiml_int_to_string(i32 %near_val)
  %23 = call ptr @__aiml_str_concat(ptr %21, ptr %22)
  %24 = call ptr @__aiml_str_concat(ptr %23, ptr @13)
  %lml_val = load i32, ptr %lml, align 4
  %25 = call ptr @__aiml_int_to_string(i32 %lml_val)
  %26 = call ptr @__aiml_str_concat(ptr %24, ptr %25)
  %print_null_check5 = icmp eq ptr %26, null
  %print_safe_str6 = select i1 %print_null_check5, ptr @print_null_str.3, ptr %26
  %print_call7 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str6)
  %print_call8 = call i32 (ptr, ...) @printf(ptr @16, ptr @15)
  %print_call9 = call i32 (ptr, ...) @printf(ptr @18, ptr @17)
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
