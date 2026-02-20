; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@1 = private unnamed_addr constant [6 x i8] c"<???>\00", align 1
@2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@3 = private unnamed_addr constant [11 x i8] c"10 + 20 = \00", align 1
@4 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@5 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main(i32 %argc, ptr %argv) personality ptr @__gxx_personality_v0 {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @my.lib.add(i32, i32) personality ptr @__gxx_personality_v0

define i32 @main.1(i32 %argc, ptr %argv) personality ptr @__gxx_personality_v0 {
entry:
  %argv2 = alloca ptr, align 8
  %argc1 = alloca i32, align 4
  store i32 %argc, ptr %argc1, align 4
  store ptr %argv, ptr %argv2, align 8
  %result = alloca i32, align 4
  %0 = call i64 @strlen(ptr @0)
  %1 = call i64 @strlen(ptr @1)
  %2 = add i64 %0, %1
  %3 = add i64 %2, 1
  %4 = call ptr @malloc(i64 %3)
  %5 = call ptr @strcpy(ptr %4, ptr @0)
  %6 = call ptr @strcat(ptr %4, ptr @1)
  store ptr %4, ptr %result, align 8
  %7 = call i64 @strlen(ptr @2)
  %8 = call i64 @strlen(ptr @3)
  %9 = add i64 %7, %8
  %10 = add i64 %9, 1
  %11 = call ptr @malloc(i64 %10)
  %12 = call ptr @strcpy(ptr %11, ptr @2)
  %13 = call ptr @strcat(ptr %11, ptr @3)
  %result_val = load i32, ptr %result, align 4
  %14 = call ptr @malloc(i64 12)
  %15 = call i32 (ptr, ptr, ...) @sprintf(ptr %14, ptr @4, i32 %result_val)
  %16 = call i64 @strlen(ptr %11)
  %17 = call i64 @strlen(ptr %14)
  %18 = add i64 %16, %17
  %19 = add i64 %18, 1
  %20 = call ptr @malloc(i64 %19)
  %21 = call ptr @strcpy(ptr %20, ptr %11)
  %22 = call ptr @strcat(ptr %20, ptr %14)
  %23 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr %20)
  %24 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @5)
  ret i32 0
}

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @sprintf(ptr, ptr, ...)

declare i32 @dprintf(i32, ptr, ...)
