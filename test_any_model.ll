; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%TestModel = type { i32, ptr }
%map_string_any = type { i32, i32, ptr, ptr, ptr }

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [14 x i8] c"ID from Any: \00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@2 = private unnamed_addr constant [16 x i8] c"Name from Any: \00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_str.2 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@3 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@4 = private unnamed_addr constant [19 x i8] c"Processing Model: \00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_str.4 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@5 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@6 = private unnamed_addr constant [11 x i8] c"TestObject\00", align 1
@7 = private unnamed_addr constant [30 x i8] c"--- Calling process_model ---\00", align 1
@fmt_str.5 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@8 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@9 = private unnamed_addr constant [38 x i8] c"--- Calling print_any_id directly ---\00", align 1
@fmt_str.6 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@10 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@11 = private unnamed_addr constant [30 x i8] c"--- Map<String, Any> Test ---\00", align 1
@fmt_str.7 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@12 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@13 = private unnamed_addr constant [5 x i8] c"item\00", align 1
@14 = private unnamed_addr constant [5 x i8] c"item\00", align 1
@15 = private unnamed_addr constant [19 x i8] c"Restored Type ID: \00", align 1
@fmt_str.8 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int.9 = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@16 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main(i32 %argc, ptr %argv) personality ptr @__gxx_personality_v0 {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  %myModel = alloca %TestModel, align 8
  store %TestModel zeroinitializer, ptr %myModel, align 8
  %member_addr = getelementptr inbounds %TestModel, ptr %myModel, i32 0, i32 0
  store i32 123, ptr %member_addr, align 4
  %member_addr1 = getelementptr inbounds %TestModel, ptr %myModel, i32 0, i32 1
  store ptr @6, ptr %member_addr1, align 8
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.5, ptr @7)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @8)
  call void @process_model(ptr %myModel)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.6, ptr @9)
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @10)
  call void @print_any_id(ptr %myModel)
  %4 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.7, ptr @11)
  %5 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @12)
  %genericMap = alloca %map_string_any, align 8
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %6 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 2
  store ptr %6, ptr %occPtr, align 8
  %7 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 3
  store ptr %7, ptr %keysPtr, align 8
  %8 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 4
  store ptr %8, ptr %valsPtr, align 8
  %sizePtr2 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 0
  %capPtr3 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 1
  %size = load i32, ptr %sizePtr2, align 4
  %capacity = load i32, ptr %capPtr3, align 4
  %9 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %keysBase = load ptr, ptr %9, align 8
  %10 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %valsBase = load ptr, ptr %10, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %entry
  %11 = load i32, ptr %i_put, align 4
  %12 = icmp slt i32 %11, %size
  br i1 %12, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %13 = getelementptr inbounds ptr, ptr %keysBase, i32 %11
  %14 = load ptr, ptr %13, align 8
  %15 = call i32 @strcmp(ptr @13, ptr %14)
  %str_eq = icmp eq i32 %15, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %16 = getelementptr inbounds ptr, ptr %valsBase, i32 %11
  store ptr %myModel, ptr %16, align 8
  br label %put_end

put_next:                                         ; preds = %put_check
  %17 = add i32 %11, 1
  store i32 %17, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %18 = icmp eq i32 %size, %capacity
  br i1 %18, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %retrieved = alloca ptr, align 8
  store ptr null, ptr %retrieved, align 8
  %19 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 0
  %20 = load i32, ptr %19, align 4
  %21 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %22 = load ptr, ptr %21, align 8
  %23 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %24 = load ptr, ptr %23, align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca ptr, align 8
  store ptr null, ptr %res_get, align 8
  br label %get_loop

put_grow:                                         ; preds = %put_not_found
  %25 = mul i32 %capacity, 2
  store i32 %25, ptr %capPtr3, align 4
  %26 = zext i32 %25 to i64
  %27 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %26
  %28 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %29 = load ptr, ptr %28, align 8
  %30 = call ptr @realloc(ptr %29, i64 %27)
  store ptr %30, ptr %28, align 8
  %31 = zext i32 %25 to i64
  %32 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %31
  %33 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %34 = load ptr, ptr %33, align 8
  %35 = call ptr @realloc(ptr %34, i64 %32)
  store ptr %35, ptr %33, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %36 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %37 = load ptr, ptr %36, align 8
  %38 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %39 = load ptr, ptr %38, align 8
  %40 = getelementptr inbounds ptr, ptr %37, i32 %size
  store ptr @13, ptr %40, align 8
  %41 = getelementptr inbounds ptr, ptr %39, i32 %size
  store ptr %myModel, ptr %41, align 8
  %42 = add i32 %size, 1
  store i32 %42, ptr %sizePtr2, align 4
  br label %put_end

get_loop:                                         ; preds = %get_next, %put_end
  %43 = load i32, ptr %i_get, align 4
  %44 = icmp slt i32 %43, %20
  br i1 %44, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %45 = getelementptr inbounds ptr, ptr %22, i32 %43
  %46 = load ptr, ptr %45, align 8
  %47 = call i32 @strcmp(ptr @14, ptr %46)
  %str_eq4 = icmp eq i32 %47, 0
  br i1 %str_eq4, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %48 = getelementptr inbounds ptr, ptr %24, i32 %43
  %49 = load ptr, ptr %48, align 8
  store ptr %49, ptr %res_get, align 8
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load ptr, ptr %res_get, align 8
  store ptr %get_res, ptr %retrieved, align 8
  %restored = alloca ptr, align 8
  store ptr null, ptr %restored, align 8
  %retrieved_val = load ptr, ptr %retrieved, align 8
  store ptr %retrieved_val, ptr %restored, align 8
  %restored_ptr = load ptr, ptr %restored, align 8
  %member_addr5 = getelementptr inbounds %TestModel, ptr %restored_ptr, i32 0, i32 0
  %id_val = load i32, ptr %member_addr5, align 4
  %50 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.8, ptr @15)
  %51 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int.9, i32 %id_val)
  %52 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @16)
  ret i32 0

get_next:                                         ; preds = %get_check
  %53 = add i32 %43, 1
  store i32 %53, ptr %i_get, align 4
  br label %get_loop
}

declare i32 @__gxx_personality_v0(...)

define void @print_any_id(ptr %obj) personality ptr @__gxx_personality_v0 {
entry:
  %obj1 = alloca ptr, align 8
  store ptr %obj, ptr %obj1, align 8
  %m = alloca ptr, align 8
  store ptr null, ptr %m, align 8
  %obj_val = load ptr, ptr %obj1, align 8
  store ptr %obj_val, ptr %m, align 8
  %m_ptr = load ptr, ptr %m, align 8
  %member_addr = getelementptr inbounds %TestModel, ptr %m_ptr, i32 0, i32 0
  %id_val = load i32, ptr %member_addr, align 4
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr @0)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int, i32 %id_val)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @1)
  %m_ptr2 = load ptr, ptr %m, align 8
  %member_addr3 = getelementptr inbounds %TestModel, ptr %m_ptr2, i32 0, i32 1
  %name_val = load ptr, ptr %member_addr3, align 8
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr @2)
  %4 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.2, ptr %name_val)
  %5 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @3)
  ret void
}

declare i32 @dprintf(i32, ptr, ...)

define void @process_model(ptr %m) personality ptr @__gxx_personality_v0 {
entry:
  %m1 = alloca ptr, align 8
  store ptr %m, ptr %m1, align 8
  %m_ptr = load ptr, ptr %m1, align 8
  %member_addr = getelementptr inbounds %TestModel, ptr %m_ptr, i32 0, i32 1
  %name_val = load ptr, ptr %member_addr, align 8
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr @4)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.4, ptr %name_val)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @5)
  %m_val = load ptr, ptr %m1, align 8
  call void @print_any_id(ptr %m_val)
  ret void
}

declare ptr @calloc(i64, i64)

declare ptr @malloc(i64)

declare i32 @strcmp(ptr, ptr)

declare ptr @realloc(ptr, i64)
