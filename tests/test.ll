; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@__global_name = global ptr null
@__global_age = global i32 0
@0 = private unnamed_addr constant [24 x i8] c"Testing System Calls...\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@3 = private unnamed_addr constant [22 x i8] c"Hello from io.write!\0A\00", align 1
@4 = private unnamed_addr constant [22 x i8] c"Sleeping for 500ms...\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [15 x i8] c"Done sleeping.\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [28 x i8] c"This should not be printed.\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [5 x i8] c"User\00", align 1
@11 = private unnamed_addr constant [7 x i8] c"Hello \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@12 = private unnamed_addr constant [11 x i8] c", you are \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@13 = private unnamed_addr constant [2 x i8] c".\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [23 x i8] c"Next year you will be \00", align 1
@16 = private unnamed_addr constant [2 x i8] c".\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0)
  %0 = call ptr @fdopen(i32 1, ptr @2)
  %1 = icmp eq ptr %0, null
  br i1 %1, label %invalid_write_handle, label %valid_write_handle

valid_write_handle:                               ; preds = %entry
  %2 = call i64 @strlen(ptr @3)
  %3 = call i64 @fwrite(ptr @3, i64 1, i64 %2, ptr %0)
  br label %merge_write

invalid_write_handle:                             ; preds = %entry
  br label %merge_write

merge_write:                                      ; preds = %valid_write_handle, %invalid_write_handle
  %write_res = phi i64 [ 0, %invalid_write_handle ], [ %3, %valid_write_handle ]
  %4 = trunc i64 %write_res to i32
  %print_call1 = call i32 (ptr, ...) @printf(ptr @5, ptr @4)
  call void @time_sleep_ms(i32 500)
  %print_call2 = call i32 (ptr, ...) @printf(ptr @7, ptr @6)
  %print_call3 = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  store ptr @10, ptr @__global_name, align 8
  store i32 25, ptr @__global_age, align 4
  %name_val = load ptr, ptr @__global_name, align 8
  %5 = call ptr @__aiml_str_concat(ptr @11, ptr %name_val)
  %6 = call ptr @__aiml_str_concat(ptr %5, ptr @12)
  %age_val = load i32, ptr @__global_age, align 4
  %7 = call ptr @__aiml_int_to_string(i32 %age_val)
  %8 = call ptr @__aiml_str_concat(ptr %6, ptr %7)
  %9 = call ptr @__aiml_str_concat(ptr %8, ptr @13)
  %print_null_check = icmp eq ptr %9, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.4, ptr %9
  %print_call4 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str)
  %age_val5 = load i32, ptr @__global_age, align 4
  %10 = add i32 %age_val5, 1
  store i32 %10, ptr @__global_age, align 4
  %age_val6 = load i32, ptr @__global_age, align 4
  %11 = call ptr @__aiml_int_to_string(i32 %age_val6)
  %12 = call ptr @__aiml_str_concat(ptr @15, ptr %11)
  %13 = call ptr @__aiml_str_concat(ptr %12, ptr @16)
  %print_null_check7 = icmp eq ptr %13, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.5, ptr %13
  %print_call9 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str8)
  call void @exit(i32 42)
  ret void
}

declare i32 @printf(ptr, ...)

declare ptr @fdopen(i32, ptr)

declare i64 @strlen(ptr)

declare i64 @fwrite(ptr, i64, i64, ptr)

declare void @time_sleep_ms(i32)

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

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare void @exit(i32)

attributes #0 = { noinline }
