; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%buffer = type { i32, i32, i32, i32, ptr }
%PacketHeader = type { i32, i32 }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [10 x i8] c"Read u8: \00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [6 x i8] c"%s%d\0A\00", align 1
@2 = private unnamed_addr constant [11 x i8] c"Read i32: \00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [6 x i8] c"%s%d\0A\00", align 1
@4 = private unnamed_addr constant [17 x i8] c"Read Header ID: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [6 x i8] c"%s%d\0A\00", align 1
@6 = private unnamed_addr constant [19 x i8] c"Read Header Type: \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [6 x i8] c"%s%d\0A\00", align 1
@8 = private unnamed_addr constant [25 x i8] c"SUCCESS: Header ID Match\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [25 x i8] c"FAIL: Header ID Mismatch\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %buf = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %buf, align 8
  %0 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  store i32 0, ptr %0, align 4
  %1 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 1
  store i32 1024, ptr %1, align 4
  %2 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  store i32 0, ptr %2, align 4
  %3 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  store i32 0, ptr %3, align 4
  %4 = call ptr @malloc(i64 1024)
  %5 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  store ptr %4, ptr %5, align 8
  %6 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  %7 = load i32, ptr %6, align 4
  %8 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %9 = load ptr, ptr %8, align 8
  %10 = getelementptr inbounds i8, ptr %9, i32 %7
  store i32 170, ptr %10, align 4
  %11 = add i32 %7, 1
  store i32 %11, ptr %6, align 4
  %12 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  %13 = load i32, ptr %12, align 4
  %14 = icmp sgt i32 %11, %13
  %15 = select i1 %14, i32 %11, i32 %13
  store i32 %15, ptr %12, align 4
  %16 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  %17 = load i32, ptr %16, align 4
  %18 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %19 = load ptr, ptr %18, align 8
  %20 = getelementptr inbounds i8, ptr %19, i32 %17
  store i32 305419896, ptr %20, align 4
  %21 = add i32 %17, 4
  store i32 %21, ptr %16, align 4
  %22 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  %23 = load i32, ptr %22, align 4
  %24 = icmp sgt i32 %21, %23
  %25 = select i1 %24, i32 %21, i32 %23
  store i32 %25, ptr %22, align 4
  %h = alloca %PacketHeader, align 8
  store %PacketHeader zeroinitializer, ptr %h, align 4
  %set_prop_ptr = getelementptr inbounds %PacketHeader, ptr %h, i32 0, i32 0
  store i32 99, ptr %set_prop_ptr, align 4
  %set_prop_ptr1 = getelementptr inbounds %PacketHeader, ptr %h, i32 0, i32 1
  store i32 2, ptr %set_prop_ptr1, align 4
  %h_val = load %PacketHeader, ptr %h, align 4
  %26 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  %27 = load i32, ptr %26, align 4
  %28 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %29 = load ptr, ptr %28, align 8
  %30 = getelementptr inbounds i8, ptr %29, i32 %27
  store i64 1, ptr %30, align 8
  %31 = add i32 %27, 8
  store i32 %31, ptr %26, align 4
  %32 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  %33 = load i32, ptr %32, align 4
  %34 = icmp sgt i32 %31, %33
  %35 = select i1 %34, i32 %31, i32 %33
  store i32 %35, ptr %32, align 4
  %36 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  %37 = load i32, ptr %36, align 4
  %38 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %39 = load ptr, ptr %38, align 8
  %40 = getelementptr inbounds i8, ptr %39, i32 %37
  store %PacketHeader %h_val, ptr %40, align 4
  %41 = add i32 %37, 8
  store i32 %41, ptr %36, align 4
  %42 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  %43 = load i32, ptr %42, align 4
  %44 = icmp sgt i32 %41, %43
  %45 = select i1 %44, i32 %41, i32 %43
  store i32 %45, ptr %42, align 4
  %b1 = alloca i8, align 1
  store i8 0, ptr %b1, align 1
  %46 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  %47 = load i32, ptr %46, align 4
  %48 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %49 = load ptr, ptr %48, align 8
  %50 = getelementptr inbounds i8, ptr %49, i32 %47
  %51 = load i8, ptr %50, align 1
  %52 = add i32 %47, 1
  store i32 %52, ptr %46, align 4
  store i8 %51, ptr %b1, align 1
  %b1_val = load i8, ptr %b1, align 1
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0, i8 %b1_val)
  %i1 = alloca i32, align 4
  store i32 0, ptr %i1, align 4
  %53 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  %54 = load i32, ptr %53, align 4
  %55 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %56 = load ptr, ptr %55, align 8
  %57 = getelementptr inbounds i8, ptr %56, i32 %54
  %58 = load i32, ptr %57, align 4
  %59 = add i32 %54, 4
  store i32 %59, ptr %53, align 4
  store i32 %58, ptr %i1, align 4
  %i1_val = load i32, ptr %i1, align 4
  %print_call2 = call i32 (ptr, ...) @printf(ptr @3, ptr @2, i32 %i1_val)
  %h2 = alloca %PacketHeader, align 8
  store %PacketHeader zeroinitializer, ptr %h2, align 4
  %60 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  %61 = load i32, ptr %60, align 4
  %62 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %63 = load ptr, ptr %62, align 8
  %64 = getelementptr inbounds i8, ptr %63, i32 %61
  %65 = load i64, ptr %64, align 8
  %66 = add i32 %61, 8
  store i32 %66, ptr %60, align 4
  %67 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  %68 = load i32, ptr %67, align 4
  %69 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %70 = load ptr, ptr %69, align 8
  %71 = getelementptr inbounds i8, ptr %70, i32 %68
  %72 = load %PacketHeader, ptr %71, align 4
  %73 = add i32 %68, 8
  store i32 %73, ptr %67, align 4
  store %PacketHeader %72, ptr %h2, align 4
  %prop_ptr = getelementptr inbounds %PacketHeader, ptr %h2, i32 0, i32 0
  %id_val = load i32, ptr %prop_ptr, align 4
  %print_call3 = call i32 (ptr, ...) @printf(ptr @5, ptr @4, i32 %id_val)
  %prop_ptr4 = getelementptr inbounds %PacketHeader, ptr %h2, i32 0, i32 1
  %type_val = load i32, ptr %prop_ptr4, align 4
  %print_call5 = call i32 (ptr, ...) @printf(ptr @7, ptr @6, i32 %type_val)
  %prop_ptr6 = getelementptr inbounds %PacketHeader, ptr %h2, i32 0, i32 0
  %id_val7 = load i32, ptr %prop_ptr6, align 4
  %74 = icmp eq i32 %id_val7, 99
  br i1 %74, label %if_then, label %if_else

if_then:                                          ; preds = %entry
  %print_call8 = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  br label %if_merge

if_else:                                          ; preds = %entry
  %print_call9 = call i32 (ptr, ...) @printf(ptr @11, ptr @10)
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  ret void
}

declare ptr @malloc(i64)

declare i32 @printf(ptr, ...)
