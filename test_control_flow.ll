; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@0 = private unnamed_addr constant [21 x i8] c"Testing Control Flow\00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@3 = private unnamed_addr constant [6 x i8] c"X is \00", align 1
@4 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@5 = private unnamed_addr constant [8 x i8] c", Y is \00", align 1
@6 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@7 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@8 = private unnamed_addr constant [27 x i8] c"X is less than Y (Correct)\00", align 1
@fmt_str.2 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@9 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@10 = private unnamed_addr constant [29 x i8] c"X is NOT less than Y (Wrong)\00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@11 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@12 = private unnamed_addr constant [28 x i8] c"X is greater than Y (Wrong)\00", align 1
@fmt_str.4 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@13 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@14 = private unnamed_addr constant [34 x i8] c"X is NOT greater than Y (Correct)\00", align 1
@fmt_str.5 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@15 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@16 = private unnamed_addr constant [17 x i8] c"Starting Loop...\00", align 1
@fmt_str.6 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@17 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@18 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@19 = private unnamed_addr constant [10 x i8] c"Counter: \00", align 1
@20 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.7 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@21 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@22 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@23 = private unnamed_addr constant [37 x i8] c"Loop Finished. Counter should be 5: \00", align 1
@24 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.8 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@25 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %x = alloca i32, align 4
  store i32 10, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 20, ptr %y, align 4
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr @0)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @1)
  %2 = call i64 @strlen(ptr @2)
  %3 = call i64 @strlen(ptr @3)
  %4 = add i64 %2, %3
  %5 = add i64 %4, 1
  %6 = call ptr @malloc(i64 %5)
  %7 = call ptr @strcpy(ptr %6, ptr @2)
  %8 = call ptr @strcat(ptr %6, ptr @3)
  %x_val = load i32, ptr %x, align 4
  %9 = call ptr @malloc(i64 12)
  %10 = call i32 (ptr, ptr, ...) @sprintf(ptr %9, ptr @4, i32 %x_val)
  %11 = call i64 @strlen(ptr %6)
  %12 = call i64 @strlen(ptr %9)
  %13 = add i64 %11, %12
  %14 = add i64 %13, 1
  %15 = call ptr @malloc(i64 %14)
  %16 = call ptr @strcpy(ptr %15, ptr %6)
  %17 = call ptr @strcat(ptr %15, ptr %9)
  %18 = call i64 @strlen(ptr %15)
  %19 = call i64 @strlen(ptr @5)
  %20 = add i64 %18, %19
  %21 = add i64 %20, 1
  %22 = call ptr @malloc(i64 %21)
  %23 = call ptr @strcpy(ptr %22, ptr %15)
  %24 = call ptr @strcat(ptr %22, ptr @5)
  %y_val = load i32, ptr %y, align 4
  %25 = call ptr @malloc(i64 12)
  %26 = call i32 (ptr, ptr, ...) @sprintf(ptr %25, ptr @6, i32 %y_val)
  %27 = call i64 @strlen(ptr %22)
  %28 = call i64 @strlen(ptr %25)
  %29 = add i64 %27, %28
  %30 = add i64 %29, 1
  %31 = call ptr @malloc(i64 %30)
  %32 = call ptr @strcpy(ptr %31, ptr %22)
  %33 = call ptr @strcat(ptr %31, ptr %25)
  %34 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr %31)
  %35 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @7)
  %x_val1 = load i32, ptr %x, align 4
  %y_val2 = load i32, ptr %y, align 4
  %lt_tmp = icmp slt i32 %x_val1, %y_val2
  br i1 %lt_tmp, label %if_then, label %if_else

if_then:                                          ; preds = %entry
  %36 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.2, ptr @8)
  %37 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @9)
  br label %if_merge

if_else:                                          ; preds = %entry
  %38 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr @10)
  %39 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @11)
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  %x_val3 = load i32, ptr %x, align 4
  %y_val4 = load i32, ptr %y, align 4
  %gt_tmp = icmp sgt i32 %x_val3, %y_val4
  br i1 %gt_tmp, label %if_then5, label %if_else6

if_then5:                                         ; preds = %if_merge
  %40 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.4, ptr @12)
  %41 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @13)
  br label %if_merge7

if_else6:                                         ; preds = %if_merge
  %42 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.5, ptr @14)
  %43 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @15)
  br label %if_merge7

if_merge7:                                        ; preds = %if_else6, %if_then5
  %counter = alloca i32, align 4
  store i32 0, ptr %counter, align 4
  %44 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.6, ptr @16)
  %45 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @17)
  br label %while_loop

while_loop:                                       ; preds = %while_body, %if_merge7
  %counter_val = load i32, ptr %counter, align 4
  %lt_tmp8 = icmp slt i32 %counter_val, 5
  br i1 %lt_tmp8, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %46 = call i64 @strlen(ptr @18)
  %47 = call i64 @strlen(ptr @19)
  %48 = add i64 %46, %47
  %49 = add i64 %48, 1
  %50 = call ptr @malloc(i64 %49)
  %51 = call ptr @strcpy(ptr %50, ptr @18)
  %52 = call ptr @strcat(ptr %50, ptr @19)
  %counter_val9 = load i32, ptr %counter, align 4
  %53 = call ptr @malloc(i64 12)
  %54 = call i32 (ptr, ptr, ...) @sprintf(ptr %53, ptr @20, i32 %counter_val9)
  %55 = call i64 @strlen(ptr %50)
  %56 = call i64 @strlen(ptr %53)
  %57 = add i64 %55, %56
  %58 = add i64 %57, 1
  %59 = call ptr @malloc(i64 %58)
  %60 = call ptr @strcpy(ptr %59, ptr %50)
  %61 = call ptr @strcat(ptr %59, ptr %53)
  %62 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.7, ptr %59)
  %63 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @21)
  %counter_val10 = load i32, ptr %counter, align 4
  %add_tmp = add i32 %counter_val10, 1
  store i32 %add_tmp, ptr %counter, align 4
  br label %while_loop

while_end:                                        ; preds = %while_loop
  %64 = call i64 @strlen(ptr @22)
  %65 = call i64 @strlen(ptr @23)
  %66 = add i64 %64, %65
  %67 = add i64 %66, 1
  %68 = call ptr @malloc(i64 %67)
  %69 = call ptr @strcpy(ptr %68, ptr @22)
  %70 = call ptr @strcat(ptr %68, ptr @23)
  %counter_val11 = load i32, ptr %counter, align 4
  %71 = call ptr @malloc(i64 12)
  %72 = call i32 (ptr, ptr, ...) @sprintf(ptr %71, ptr @24, i32 %counter_val11)
  %73 = call i64 @strlen(ptr %68)
  %74 = call i64 @strlen(ptr %71)
  %75 = add i64 %73, %74
  %76 = add i64 %75, 1
  %77 = call ptr @malloc(i64 %76)
  %78 = call ptr @strcpy(ptr %77, ptr %68)
  %79 = call ptr @strcat(ptr %77, ptr %71)
  %80 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.8, ptr %77)
  %81 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @25)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @dprintf(i32, ptr, ...)

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @sprintf(ptr, ptr, ...)
