; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [21 x i8] c"Testing Control Flow\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [6 x i8] c"X is \00", align 1
@4 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@5 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@6 = private unnamed_addr constant [8 x i8] c", Y is \00", align 1
@7 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@8 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@9 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@10 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [17 x i8] c"Starting Loop...\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [37 x i8] c"Loop Finished. Counter should be 5: \00", align 1
@16 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@17 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@18 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [21 x i8] c"Testing Control Flow\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [6 x i8] c"X is \00", align 1
@24 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@25 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@26 = private unnamed_addr constant [8 x i8] c", Y is \00", align 1
@27 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@28 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@29 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@30 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@32 = private unnamed_addr constant [17 x i8] c"Starting Loop...\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@35 = private unnamed_addr constant [37 x i8] c"Loop Finished. Counter should be 5: \00", align 1
@36 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@37 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@38 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %x = alloca i32, align 4
  store i32 0, ptr %x, align 4
  store i32 10, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 0, ptr %y, align 4
  store i32 20, ptr %y, align 4
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr @0)
  %x_val = load i32, ptr %x, align 4
  %0 = call ptr @malloc(i64 12)
  %1 = call i32 (ptr, ptr, ...) @sprintf(ptr %0, ptr @4, i32 %x_val)
  %2 = icmp eq ptr %0, null
  %3 = select i1 %2, ptr @5, ptr %0
  %4 = call i64 @strlen(ptr @3)
  %5 = call i64 @strlen(ptr %3)
  %6 = add i64 %4, %5
  %7 = add i64 %6, 1
  %8 = call ptr @malloc(i64 %7)
  %9 = call ptr @strcpy(ptr %8, ptr @3)
  %10 = call ptr @strcat(ptr %8, ptr %3)
  %11 = icmp eq ptr %8, null
  %12 = select i1 %11, ptr @7, ptr %8
  %13 = call i64 @strlen(ptr %12)
  %14 = call i64 @strlen(ptr @6)
  %15 = add i64 %13, %14
  %16 = add i64 %15, 1
  %17 = call ptr @malloc(i64 %16)
  %18 = call ptr @strcpy(ptr %17, ptr %12)
  %19 = call ptr @strcat(ptr %17, ptr @6)
  %y_val = load i32, ptr %y, align 4
  %20 = call ptr @malloc(i64 12)
  %21 = call i32 (ptr, ptr, ...) @sprintf(ptr %20, ptr @8, i32 %y_val)
  %22 = icmp eq ptr %17, null
  %23 = select i1 %22, ptr @9, ptr %17
  %24 = icmp eq ptr %20, null
  %25 = select i1 %24, ptr @9, ptr %20
  %26 = call i64 @strlen(ptr %23)
  %27 = call i64 @strlen(ptr %25)
  %28 = add i64 %26, %27
  %29 = add i64 %28, 1
  %30 = call ptr @malloc(i64 %29)
  %31 = call ptr @strcpy(ptr %30, ptr %23)
  %32 = call ptr @strcat(ptr %30, ptr %25)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @11, ptr %30)
  %x_val2 = load i32, ptr %x, align 4
  %y_val3 = load i32, ptr %y, align 4
  %33 = icmp slt i32 %x_val2, %y_val3
  br i1 %33, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %x_val4 = load i32, ptr %x, align 4
  %y_val5 = load i32, ptr %y, align 4
  %34 = icmp sgt i32 %x_val4, %y_val5
  br i1 %34, label %if_then6, label %if_merge7

if_then6:                                         ; preds = %if_merge
  br label %if_merge7

if_merge7:                                        ; preds = %if_then6, %if_merge
  %counter = alloca i32, align 4
  store i32 0, ptr %counter, align 4
  store i32 0, ptr %counter, align 4
  %print_call8 = call i32 (ptr, ...) @printf(ptr @14, ptr @12)
  br label %while_loop

while_loop:                                       ; preds = %while_body, %if_merge7
  %counter_val = load i32, ptr %counter, align 4
  %35 = icmp slt i32 %counter_val, 5
  br i1 %35, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  br label %while_loop

while_end:                                        ; preds = %while_loop
  %counter_val9 = load i32, ptr %counter, align 4
  %36 = call ptr @malloc(i64 12)
  %37 = call i32 (ptr, ptr, ...) @sprintf(ptr %36, ptr @16, i32 %counter_val9)
  %38 = icmp eq ptr %36, null
  %39 = select i1 %38, ptr @17, ptr %36
  %40 = call i64 @strlen(ptr @15)
  %41 = call i64 @strlen(ptr %39)
  %42 = add i64 %40, %41
  %43 = add i64 %42, 1
  %44 = call ptr @malloc(i64 %43)
  %45 = call ptr @strcpy(ptr %44, ptr @15)
  %46 = call ptr @strcat(ptr %44, ptr %39)
  %print_call10 = call i32 (ptr, ...) @printf(ptr @19, ptr %44)
  ret void
}

declare i32 @printf(ptr, ...)

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

define void @__aiml_init.1() {
entry:
  %x = alloca i32, align 4
  store i32 0, ptr %x, align 4
  store i32 10, ptr %x, align 4
  %y = alloca i32, align 4
  store i32 0, ptr %y, align 4
  store i32 20, ptr %y, align 4
  %print_call = call i32 (ptr, ...) @printf(ptr @22, ptr @20)
  %x_val = load i32, ptr %x, align 4
  %0 = call ptr @malloc(i64 12)
  %1 = call i32 (ptr, ptr, ...) @sprintf(ptr %0, ptr @24, i32 %x_val)
  %2 = icmp eq ptr %0, null
  %3 = select i1 %2, ptr @25, ptr %0
  %4 = call i64 @strlen(ptr @23)
  %5 = call i64 @strlen(ptr %3)
  %6 = add i64 %4, %5
  %7 = add i64 %6, 1
  %8 = call ptr @malloc(i64 %7)
  %9 = call ptr @strcpy(ptr %8, ptr @23)
  %10 = call ptr @strcat(ptr %8, ptr %3)
  %11 = icmp eq ptr %8, null
  %12 = select i1 %11, ptr @27, ptr %8
  %13 = call i64 @strlen(ptr %12)
  %14 = call i64 @strlen(ptr @26)
  %15 = add i64 %13, %14
  %16 = add i64 %15, 1
  %17 = call ptr @malloc(i64 %16)
  %18 = call ptr @strcpy(ptr %17, ptr %12)
  %19 = call ptr @strcat(ptr %17, ptr @26)
  %y_val = load i32, ptr %y, align 4
  %20 = call ptr @malloc(i64 12)
  %21 = call i32 (ptr, ptr, ...) @sprintf(ptr %20, ptr @28, i32 %y_val)
  %22 = icmp eq ptr %17, null
  %23 = select i1 %22, ptr @29, ptr %17
  %24 = icmp eq ptr %20, null
  %25 = select i1 %24, ptr @29, ptr %20
  %26 = call i64 @strlen(ptr %23)
  %27 = call i64 @strlen(ptr %25)
  %28 = add i64 %26, %27
  %29 = add i64 %28, 1
  %30 = call ptr @malloc(i64 %29)
  %31 = call ptr @strcpy(ptr %30, ptr %23)
  %32 = call ptr @strcat(ptr %30, ptr %25)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @31, ptr %30)
  %x_val2 = load i32, ptr %x, align 4
  %y_val3 = load i32, ptr %y, align 4
  %33 = icmp slt i32 %x_val2, %y_val3
  br i1 %33, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %x_val4 = load i32, ptr %x, align 4
  %y_val5 = load i32, ptr %y, align 4
  %34 = icmp sgt i32 %x_val4, %y_val5
  br i1 %34, label %if_then6, label %if_merge7

if_then6:                                         ; preds = %if_merge
  br label %if_merge7

if_merge7:                                        ; preds = %if_then6, %if_merge
  %counter = alloca i32, align 4
  store i32 0, ptr %counter, align 4
  store i32 0, ptr %counter, align 4
  %print_call8 = call i32 (ptr, ...) @printf(ptr @34, ptr @32)
  br label %while_loop

while_loop:                                       ; preds = %while_body, %if_merge7
  %counter_val = load i32, ptr %counter, align 4
  %35 = icmp slt i32 %counter_val, 5
  br i1 %35, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  br label %while_loop

while_end:                                        ; preds = %while_loop
  %counter_val9 = load i32, ptr %counter, align 4
  %36 = call ptr @malloc(i64 12)
  %37 = call i32 (ptr, ptr, ...) @sprintf(ptr %36, ptr @36, i32 %counter_val9)
  %38 = icmp eq ptr %36, null
  %39 = select i1 %38, ptr @37, ptr %36
  %40 = call i64 @strlen(ptr @35)
  %41 = call i64 @strlen(ptr %39)
  %42 = add i64 %40, %41
  %43 = add i64 %42, 1
  %44 = call ptr @malloc(i64 %43)
  %45 = call ptr @strcpy(ptr %44, ptr @35)
  %46 = call ptr @strcat(ptr %44, ptr %39)
  %print_call10 = call i32 (ptr, ...) @printf(ptr @39, ptr %44)
  ret void
}
