; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [6 x i8] c"World\00", align 1
@1 = private unnamed_addr constant [7 x i8] c"Hello \00", align 1
@2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@3 = private unnamed_addr constant [35 x i8] c"[RT] concat: s1=%p, s2=%p, res=%p\0A\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [11 x i8] c"10 + 20 = \00", align 1
@6 = private unnamed_addr constant [33 x i8] c"[RT] malloc for intToString: %p\0A\00", align 1
@7 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@8 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@9 = private unnamed_addr constant [35 x i8] c"[RT] concat: s1=%p, s2=%p, res=%p\0A\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [18 x i8] c"Result from lib: \00", align 1
@12 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@13 = private unnamed_addr constant [35 x i8] c"[RT] concat: s1=%p, s2=%p, res=%p\0A\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [24 x i8] c"String variable holds: \00", align 1
@16 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@17 = private unnamed_addr constant [35 x i8] c"[RT] concat: s1=%p, s2=%p, res=%p\0A\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

declare ptr @my.lib.add(i32, i32) personality ptr @__gxx_personality_v0

declare i32 @__gxx_personality_v0(...)

define void @__aiml_init() {
entry:
  ret void

entry1:                                           ; No predecessors!
}

define i32 @main(i32 %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %argc = alloca i32, align 4
  store i32 %0, ptr %argc, align 4
  %argv = alloca ptr, align 8
  store ptr %1, ptr %argv, align 8
  %name = alloca ptr, align 8
  store ptr null, ptr %name, align 8
  store ptr @0, ptr %name, align 8
  %a = alloca i32, align 4
  store i32 0, ptr %a, align 4
  store i32 10, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 0, ptr %b, align 4
  store i32 20, ptr %b, align 4
  %name_val = load ptr, ptr %name, align 8
  %2 = icmp eq ptr %name_val, null
  %3 = select i1 %2, ptr @2, ptr %name_val
  %4 = call i64 @strlen(ptr @1)
  %5 = call i64 @strlen(ptr %3)
  %6 = add i64 %4, %5
  %7 = add i64 %6, 1
  %8 = call ptr @malloc(i64 %7)
  %9 = call i32 (ptr, ...) @printf(ptr @3, ptr @1, ptr %3, ptr %8)
  %10 = call ptr @strcpy(ptr %8, ptr @1)
  %11 = call ptr @strcat(ptr %8, ptr %3)
  %print_null_check = icmp eq ptr %8, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %8
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr %print_safe_str)
  %a_val = load i32, ptr %a, align 4
  %b_val = load i32, ptr %b, align 4
  %12 = add i32 %a_val, %b_val
  %13 = call ptr @malloc(i64 12)
  %14 = call i32 (ptr, ...) @printf(ptr @6, ptr %13)
  %15 = call i32 (ptr, ptr, ...) @sprintf(ptr %13, ptr @7, i32 %12)
  %16 = icmp eq ptr %13, null
  %17 = select i1 %16, ptr @8, ptr %13
  %18 = call i64 @strlen(ptr @5)
  %19 = call i64 @strlen(ptr %17)
  %20 = add i64 %18, %19
  %21 = add i64 %20, 1
  %22 = call ptr @malloc(i64 %21)
  %23 = call i32 (ptr, ...) @printf(ptr @9, ptr @5, ptr %17, ptr %22)
  %24 = call ptr @strcpy(ptr %22, ptr @5)
  %25 = call ptr @strcat(ptr %22, ptr %17)
  %print_null_check1 = icmp eq ptr %22, null
  %print_safe_str2 = select i1 %print_null_check1, ptr @print_null_str.1, ptr %22
  %print_call3 = call i32 (ptr, ...) @printf(ptr @10, ptr %print_safe_str2)
  %a_val4 = load i32, ptr %a, align 4
  %b_val5 = load i32, ptr %b, align 4
  %26 = call ptr @my.lib.add(i32 %a_val4, i32 %b_val5)
  %27 = icmp eq ptr %26, null
  %28 = select i1 %27, ptr @12, ptr %26
  %29 = call i64 @strlen(ptr @11)
  %30 = call i64 @strlen(ptr %28)
  %31 = add i64 %29, %30
  %32 = add i64 %31, 1
  %33 = call ptr @malloc(i64 %32)
  %34 = call i32 (ptr, ...) @printf(ptr @13, ptr @11, ptr %28, ptr %33)
  %35 = call ptr @strcpy(ptr %33, ptr @11)
  %36 = call ptr @strcat(ptr %33, ptr %28)
  %print_null_check6 = icmp eq ptr %33, null
  %print_safe_str7 = select i1 %print_null_check6, ptr @print_null_str.2, ptr %33
  %print_call8 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str7)
  %res_str = alloca ptr, align 8
  store ptr null, ptr %res_str, align 8
  %a_val9 = load i32, ptr %a, align 4
  %b_val10 = load i32, ptr %b, align 4
  %37 = add i32 %a_val9, %b_val10
  store i32 %37, ptr %res_str, align 4
  %res_str_val = load ptr, ptr %res_str, align 8
  %38 = icmp eq ptr %res_str_val, null
  %39 = select i1 %38, ptr @16, ptr %res_str_val
  %40 = call i64 @strlen(ptr @15)
  %41 = call i64 @strlen(ptr %39)
  %42 = add i64 %40, %41
  %43 = add i64 %42, 1
  %44 = call ptr @malloc(i64 %43)
  %45 = call i32 (ptr, ...) @printf(ptr @17, ptr @15, ptr %39, ptr %44)
  %46 = call ptr @strcpy(ptr %44, ptr @15)
  %47 = call ptr @strcat(ptr %44, ptr %39)
  %print_null_check11 = icmp eq ptr %44, null
  %print_safe_str12 = select i1 %print_null_check11, ptr @print_null_str.3, ptr %44
  %print_call13 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str12)
  ret i32 0
}

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @printf(ptr, ...)

declare i32 @sprintf(ptr, ptr, ...)
