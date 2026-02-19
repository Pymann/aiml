; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Circle = type { i32, i32, i32, i32, i32 }
%Shape = type { i32 }

@0 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@1 = private unnamed_addr constant [12 x i8] c"Circle ID: \00", align 1
@2 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@3 = private unnamed_addr constant [9 x i8] c", Pos: (\00", align 1
@4 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@5 = private unnamed_addr constant [3 x i8] c", \00", align 1
@6 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@7 = private unnamed_addr constant [12 x i8] c"), Radius: \00", align 1
@8 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@9 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@10 = private unnamed_addr constant [23 x i8] c"Testing OO Features...\00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@11 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@12 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@13 = private unnamed_addr constant [23 x i8] c"Shape ID (Inherited): \00", align 1
@14 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.2 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@15 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@16 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@17 = private unnamed_addr constant [16 x i8] c"Mixin Field X: \00", align 1
@18 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@19 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %c = alloca ptr, align 8
  store ptr null, ptr %c, align 8
  %malloc_call = call ptr @malloc(i64 20)
  %0 = getelementptr inbounds %Circle, ptr %malloc_call, i32 0, i32 0
  store i32 1, ptr %0, align 4
  %1 = getelementptr inbounds %Circle, ptr %malloc_call, i32 0, i32 0
  store i32 1, ptr %1, align 4
  %2 = getelementptr inbounds %Circle, ptr %malloc_call, i32 0, i32 3
  store i32 5, ptr %2, align 4
  %3 = getelementptr inbounds %Circle, ptr %malloc_call, i32 0, i32 1
  store i32 10, ptr %3, align 4
  %4 = getelementptr inbounds %Circle, ptr %malloc_call, i32 0, i32 2
  store i32 20, ptr %4, align 4
  store ptr %malloc_call, ptr %c, align 8
  %5 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr @10)
  %6 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @11)
  %this_ptr = load ptr, ptr %c, align 8
  %call_method = call void @Circle_print(ptr %this_ptr)
  %shapeId = alloca i32, align 4
  store i32 0, ptr %shapeId, align 4
  %this_ptr1 = load ptr, ptr %c, align 8
  %call_method2 = call i32 @Shape_getId(ptr %this_ptr1)
  store i32 %call_method2, ptr %shapeId, align 4
  %7 = call i64 @strlen(ptr @12)
  %8 = call i64 @strlen(ptr @13)
  %9 = add i64 %7, %8
  %10 = add i64 %9, 1
  %11 = call ptr @malloc(i64 %10)
  %12 = call ptr @strcpy(ptr %11, ptr @12)
  %13 = call ptr @strcat(ptr %11, ptr @13)
  %shapeId_val = load i32, ptr %shapeId, align 4
  %14 = call ptr @malloc(i64 12)
  %15 = call i32 (ptr, ptr, ...) @sprintf(ptr %14, ptr @14, i32 %shapeId_val)
  %16 = call i64 @strlen(ptr %11)
  %17 = call i64 @strlen(ptr %14)
  %18 = add i64 %16, %17
  %19 = add i64 %18, 1
  %20 = call ptr @malloc(i64 %19)
  %21 = call ptr @strcpy(ptr %20, ptr %11)
  %22 = call ptr @strcat(ptr %20, ptr %14)
  %23 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.2, ptr %20)
  %24 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @15)
  %25 = call i64 @strlen(ptr @16)
  %26 = call i64 @strlen(ptr @17)
  %27 = add i64 %25, %26
  %28 = add i64 %27, 1
  %29 = call ptr @malloc(i64 %28)
  %30 = call ptr @strcpy(ptr %29, ptr @16)
  %31 = call ptr @strcat(ptr %29, ptr @17)
  %c_ptr = load ptr, ptr %c, align 8
  %member_addr = getelementptr inbounds %Circle, ptr %c_ptr, i32 0, i32 1
  %x_val = load i32, ptr %member_addr, align 4
  %32 = call ptr @malloc(i64 12)
  %33 = call i32 (ptr, ptr, ...) @sprintf(ptr %32, ptr @18, i32 %x_val)
  %34 = call i64 @strlen(ptr %29)
  %35 = call i64 @strlen(ptr %32)
  %36 = add i64 %34, %35
  %37 = add i64 %36, 1
  %38 = call ptr @malloc(i64 %37)
  %39 = call ptr @strcpy(ptr %38, ptr %29)
  %40 = call ptr @strcat(ptr %38, ptr %32)
  %41 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr %38)
  %42 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @19)
  %c_val = load ptr, ptr %c, align 8
  call void @free(ptr %c_val)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

define i32 @Shape_getId(ptr %"<") {
entry:
  %"<_addr" = alloca ptr, align 8
  store ptr %"<", ptr %"<_addr", align 8
  %"<_ptr" = load ptr, ptr %"<_addr", align 8
  %member_addr = getelementptr inbounds %Shape, ptr %"<_ptr", i32 0, i32 0
  %id_val = load i32, ptr %member_addr, align 4
  ret i32 %id_val
}

define void @Circle_print(ptr %"<") {
entry:
  %"<_addr" = alloca ptr, align 8
  store ptr %"<", ptr %"<_addr", align 8
  %0 = call i64 @strlen(ptr @0)
  %1 = call i64 @strlen(ptr @1)
  %2 = add i64 %0, %1
  %3 = add i64 %2, 1
  %4 = call ptr @malloc(i64 %3)
  %5 = call ptr @strcpy(ptr %4, ptr @0)
  %6 = call ptr @strcat(ptr %4, ptr @1)
  %"<_ptr" = load ptr, ptr %"<_addr", align 8
  %member_addr = getelementptr inbounds %Circle, ptr %"<_ptr", i32 0, i32 0
  %id_val = load i32, ptr %member_addr, align 4
  %7 = call ptr @malloc(i64 12)
  %8 = call i32 (ptr, ptr, ...) @sprintf(ptr %7, ptr @2, i32 %id_val)
  %9 = call i64 @strlen(ptr %4)
  %10 = call i64 @strlen(ptr %7)
  %11 = add i64 %9, %10
  %12 = add i64 %11, 1
  %13 = call ptr @malloc(i64 %12)
  %14 = call ptr @strcpy(ptr %13, ptr %4)
  %15 = call ptr @strcat(ptr %13, ptr %7)
  %16 = call i64 @strlen(ptr %13)
  %17 = call i64 @strlen(ptr @3)
  %18 = add i64 %16, %17
  %19 = add i64 %18, 1
  %20 = call ptr @malloc(i64 %19)
  %21 = call ptr @strcpy(ptr %20, ptr %13)
  %22 = call ptr @strcat(ptr %20, ptr @3)
  %"<_ptr1" = load ptr, ptr %"<_addr", align 8
  %member_addr2 = getelementptr inbounds %Circle, ptr %"<_ptr1", i32 0, i32 1
  %x_val = load i32, ptr %member_addr2, align 4
  %23 = call ptr @malloc(i64 12)
  %24 = call i32 (ptr, ptr, ...) @sprintf(ptr %23, ptr @4, i32 %x_val)
  %25 = call i64 @strlen(ptr %20)
  %26 = call i64 @strlen(ptr %23)
  %27 = add i64 %25, %26
  %28 = add i64 %27, 1
  %29 = call ptr @malloc(i64 %28)
  %30 = call ptr @strcpy(ptr %29, ptr %20)
  %31 = call ptr @strcat(ptr %29, ptr %23)
  %32 = call i64 @strlen(ptr %29)
  %33 = call i64 @strlen(ptr @5)
  %34 = add i64 %32, %33
  %35 = add i64 %34, 1
  %36 = call ptr @malloc(i64 %35)
  %37 = call ptr @strcpy(ptr %36, ptr %29)
  %38 = call ptr @strcat(ptr %36, ptr @5)
  %"<_ptr3" = load ptr, ptr %"<_addr", align 8
  %member_addr4 = getelementptr inbounds %Circle, ptr %"<_ptr3", i32 0, i32 2
  %y_val = load i32, ptr %member_addr4, align 4
  %39 = call ptr @malloc(i64 12)
  %40 = call i32 (ptr, ptr, ...) @sprintf(ptr %39, ptr @6, i32 %y_val)
  %41 = call i64 @strlen(ptr %36)
  %42 = call i64 @strlen(ptr %39)
  %43 = add i64 %41, %42
  %44 = add i64 %43, 1
  %45 = call ptr @malloc(i64 %44)
  %46 = call ptr @strcpy(ptr %45, ptr %36)
  %47 = call ptr @strcat(ptr %45, ptr %39)
  %48 = call i64 @strlen(ptr %45)
  %49 = call i64 @strlen(ptr @7)
  %50 = add i64 %48, %49
  %51 = add i64 %50, 1
  %52 = call ptr @malloc(i64 %51)
  %53 = call ptr @strcpy(ptr %52, ptr %45)
  %54 = call ptr @strcat(ptr %52, ptr @7)
  %"<_ptr5" = load ptr, ptr %"<_addr", align 8
  %member_addr6 = getelementptr inbounds %Circle, ptr %"<_ptr5", i32 0, i32 3
  %radius_val = load i32, ptr %member_addr6, align 4
  %55 = call ptr @malloc(i64 12)
  %56 = call i32 (ptr, ptr, ...) @sprintf(ptr %55, ptr @8, i32 %radius_val)
  %57 = call i64 @strlen(ptr %52)
  %58 = call i64 @strlen(ptr %55)
  %59 = add i64 %57, %58
  %60 = add i64 %59, 1
  %61 = call ptr @malloc(i64 %60)
  %62 = call ptr @strcpy(ptr %61, ptr %52)
  %63 = call ptr @strcat(ptr %61, ptr %55)
  %64 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr %61)
  %65 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @9)
  ret void
}

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @sprintf(ptr, ptr, ...)

declare i32 @dprintf(i32, ptr, ...)

declare void @free(ptr)
