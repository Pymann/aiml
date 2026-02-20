; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@1 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@3 = private unnamed_addr constant [3 x i8] c"%d\00", align 1

define internal void @__aiml_init() {
entry:
  ret void
}

define ptr @my.lib.add(i32 %a, i32 %b) personality ptr @__gxx_personality_v0 {
entry:
  %b2 = alloca i32, align 4
  %a1 = alloca i32, align 4
  store i32 %a, ptr %a1, align 4
  store i32 %b, ptr %b2, align 4
  %a_val = load i32, ptr %a1, align 4
  %b_val = load i32, ptr %b2, align 4
  %add_tmp = add i32 %a_val, %b_val
  %0 = call ptr @malloc(i64 12)
  %1 = call i32 (ptr, ptr, ...) @sprintf(ptr %0, ptr @1, i32 %add_tmp)
  %2 = call i64 @strlen(ptr @0)
  %3 = call i64 @strlen(ptr %0)
  %4 = add i64 %2, %3
  %5 = add i64 %4, 1
  %6 = call ptr @malloc(i64 %5)
  %7 = call ptr @strcpy(ptr %6, ptr @0)
  %8 = call ptr @strcat(ptr %6, ptr %0)
  ret ptr %6
}

declare i32 @__gxx_personality_v0(...)

declare ptr @malloc(i64)

declare i32 @sprintf(ptr, ptr, ...)

declare i64 @strlen(ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

define i32 @main(i32 %argc, ptr %argv) {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  call void @__aiml_init()
  ret i32 0
}

define internal void @__aiml_init.1() {
entry:
  ret void
}

define ptr @my.lib.add.2(i32 %a, i32 %b) personality ptr @__gxx_personality_v0 {
entry:
  %b2 = alloca i32, align 4
  %a1 = alloca i32, align 4
  store i32 %a, ptr %a1, align 4
  store i32 %b, ptr %b2, align 4
  %a_val = load i32, ptr %a1, align 4
  %b_val = load i32, ptr %b2, align 4
  %add_tmp = add i32 %a_val, %b_val
  %0 = call ptr @malloc(i64 12)
  %1 = call i32 (ptr, ptr, ...) @sprintf(ptr %0, ptr @3, i32 %add_tmp)
  %2 = call i64 @strlen(ptr @2)
  %3 = call i64 @strlen(ptr %0)
  %4 = add i64 %2, %3
  %5 = add i64 %4, 1
  %6 = call ptr @malloc(i64 %5)
  %7 = call ptr @strcpy(ptr %6, ptr @2)
  %8 = call ptr @strcat(ptr %6, ptr %0)
  ret ptr %6
}
