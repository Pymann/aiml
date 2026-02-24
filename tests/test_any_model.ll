; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%TestModel = type { i32, ptr }
%map_string_any = type { i32, i32, ptr, ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [14 x i8] c"ID from Any: \00", align 1
@1 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@3 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [16 x i8] c"Name from Any: \00", align 1
@6 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@7 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [19 x i8] c"Processing Model: \00", align 1
@10 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@11 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [11 x i8] c"TestObject\00", align 1
@14 = private unnamed_addr constant [30 x i8] c"--- Calling process_model ---\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [38 x i8] c"--- Calling print_any_id directly ---\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [30 x i8] c"--- Map<String, Any> Test ---\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [5 x i8] c"item\00", align 1
@24 = private unnamed_addr constant [5 x i8] c"item\00", align 1
@25 = private unnamed_addr constant [19 x i8] c"Restored Type ID: \00", align 1
@26 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@27 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@28 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %myModel = alloca %TestModel, align 8
  store %TestModel zeroinitializer, ptr %myModel, align 8
  %set_prop_ptr = getelementptr inbounds %TestModel, ptr %myModel, i32 0, i32 0
  store i32 123, ptr %set_prop_ptr, align 4
  %set_prop_ptr3 = getelementptr inbounds %TestModel, ptr %myModel, i32 0, i32 1
  store ptr @13, ptr %set_prop_ptr3, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @16, ptr @14)
  %myModel_val = load %TestModel, ptr %myModel, align 8
  call void @process_model(ptr %myModel)
  %print_call4 = call i32 (ptr, ...) @printf(ptr @19, ptr @17)
  %myModel_val5 = load %TestModel, ptr %myModel, align 8
  call void @print_any_id(ptr %myModel)
  %print_call6 = call i32 (ptr, ...) @printf(ptr @22, ptr @20)
  %genericMap = alloca %map_string_any, align 8
  store %map_string_any zeroinitializer, ptr %genericMap, align 8
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 2
  store ptr %0, ptr %occPtr, align 8
  %1 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 3
  store ptr %1, ptr %keysPtr, align 8
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %genericMap, i32 0, i32 4
  store ptr %2, ptr %valsPtr, align 8
  %myModel_val7 = load %TestModel, ptr %myModel, align 8
  %sizePtr8 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 0
  %capPtr9 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 1
  %size = load i32, ptr %sizePtr8, align 4
  %capacity = load i32, ptr %capPtr9, align 4
  %3 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %keysBase = load ptr, ptr %3, align 8
  %4 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %valsBase = load ptr, ptr %4, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

entry1:                                           ; No predecessors!

entry2:                                           ; No predecessors!

put_loop:                                         ; preds = %put_next, %entry
  %5 = load i32, ptr %i_put, align 4
  %6 = icmp slt i32 %5, %size
  br i1 %6, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %7 = getelementptr inbounds ptr, ptr %keysBase, i32 %5
  %8 = load ptr, ptr %7, align 8
  %9 = call i32 @strcmp(ptr @23, ptr %8)
  %str_eq = icmp eq i32 %9, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %10 = getelementptr inbounds %TestModel, ptr %valsBase, i32 %5
  store %TestModel %myModel_val7, ptr %10, align 8
  br label %put_end

put_next:                                         ; preds = %put_check
  %11 = add i32 %5, 1
  store i32 %11, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %12 = icmp eq i32 %size, %capacity
  br i1 %12, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %retrieved = alloca ptr, align 8
  store ptr null, ptr %retrieved, align 8
  %13 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 0
  %14 = load i32, ptr %13, align 4
  %15 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %16 = load ptr, ptr %15, align 8
  %17 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %18 = load ptr, ptr %17, align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca ptr, align 8
  store ptr null, ptr %res_get, align 8
  br label %get_loop

put_grow:                                         ; preds = %put_not_found
  %19 = mul i32 %capacity, 2
  store i32 %19, ptr %capPtr9, align 4
  %20 = zext i32 %19 to i64
  %21 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %20
  %22 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %23 = load ptr, ptr %22, align 8
  %24 = call ptr @realloc(ptr %23, i64 %21)
  store ptr %24, ptr %22, align 8
  %25 = zext i32 %19 to i64
  %26 = mul i64 ptrtoint (ptr getelementptr inbounds (%TestModel, ptr null, i32 1) to i64), %25
  %27 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %28 = load ptr, ptr %27, align 8
  %29 = call ptr @realloc(ptr %28, i64 %26)
  store ptr %29, ptr %27, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %30 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 3
  %31 = load ptr, ptr %30, align 8
  %32 = getelementptr inbounds %map_string_any, ptr %genericMap, i32 0, i32 4
  %33 = load ptr, ptr %32, align 8
  %34 = getelementptr inbounds ptr, ptr %31, i32 %size
  store ptr @23, ptr %34, align 8
  %35 = getelementptr inbounds %TestModel, ptr %33, i32 %size
  store %TestModel %myModel_val7, ptr %35, align 8
  %36 = add i32 %size, 1
  store i32 %36, ptr %sizePtr8, align 4
  br label %put_end

get_loop:                                         ; preds = %get_next, %put_end
  %37 = load i32, ptr %i_get, align 4
  %38 = icmp slt i32 %37, %14
  br i1 %38, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %39 = getelementptr inbounds ptr, ptr %16, i32 %37
  %40 = load ptr, ptr %39, align 8
  %41 = call i32 @strcmp(ptr @24, ptr %40)
  %str_eq10 = icmp eq i32 %41, 0
  br i1 %str_eq10, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %42 = getelementptr inbounds ptr, ptr %18, i32 %37
  %43 = load ptr, ptr %42, align 8
  store ptr %43, ptr %res_get, align 8
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load ptr, ptr %res_get, align 8
  store ptr %get_res, ptr %retrieved, align 8
  %restored = alloca ptr, align 8
  store ptr null, ptr %restored, align 8
  %retrieved_val = load ptr, ptr %retrieved, align 8
  store ptr %retrieved_val, ptr %restored, align 8
  %prop_ptr_base = load ptr, ptr %restored, align 8
  %prop_ptr = getelementptr inbounds %TestModel, ptr %prop_ptr_base, i32 0, i32 0
  %id_val = load i32, ptr %prop_ptr, align 4
  %44 = call ptr @malloc(i64 12)
  %45 = call i32 (ptr, ptr, ...) @sprintf(ptr %44, ptr @26, i32 %id_val)
  %46 = icmp eq ptr %44, null
  %47 = select i1 %46, ptr @27, ptr %44
  %48 = call i64 @strlen(ptr @25)
  %49 = call i64 @strlen(ptr %47)
  %50 = add i64 %48, %49
  %51 = add i64 %50, 1
  %52 = call ptr @malloc(i64 %51)
  %53 = call ptr @strcpy(ptr %52, ptr @25)
  %54 = call ptr @strcat(ptr %52, ptr %47)
  %print_call11 = call i32 (ptr, ...) @printf(ptr @29, ptr %52)
  ret void

get_next:                                         ; preds = %get_check
  %55 = add i32 %37, 1
  store i32 %55, ptr %i_get, align 4
  br label %get_loop
}

define void @print_any_id(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %obj = alloca ptr, align 8
  store ptr %0, ptr %obj, align 8
  %m = alloca ptr, align 8
  store ptr null, ptr %m, align 8
  %obj_val = load ptr, ptr %obj, align 8
  store ptr %obj_val, ptr %m, align 8
  %prop_ptr_base = load ptr, ptr %m, align 8
  %prop_ptr = getelementptr inbounds %TestModel, ptr %prop_ptr_base, i32 0, i32 0
  %id_val = load i32, ptr %prop_ptr, align 4
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @1, i32 %id_val)
  %3 = icmp eq ptr %1, null
  %4 = select i1 %3, ptr @2, ptr %1
  %5 = call i64 @strlen(ptr @0)
  %6 = call i64 @strlen(ptr %4)
  %7 = add i64 %5, %6
  %8 = add i64 %7, 1
  %9 = call ptr @malloc(i64 %8)
  %10 = call ptr @strcpy(ptr %9, ptr @0)
  %11 = call ptr @strcat(ptr %9, ptr %4)
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr %9)
  %prop_ptr_base1 = load ptr, ptr %m, align 8
  %prop_ptr2 = getelementptr inbounds %TestModel, ptr %prop_ptr_base1, i32 0, i32 1
  %name_val = load ptr, ptr %prop_ptr2, align 8
  %12 = icmp eq ptr %name_val, null
  %13 = select i1 %12, ptr @6, ptr %name_val
  %14 = call i64 @strlen(ptr @5)
  %15 = call i64 @strlen(ptr %13)
  %16 = add i64 %14, %15
  %17 = add i64 %16, 1
  %18 = call ptr @malloc(i64 %17)
  %19 = call ptr @strcpy(ptr %18, ptr @5)
  %20 = call ptr @strcat(ptr %18, ptr %13)
  %print_call3 = call i32 (ptr, ...) @printf(ptr @8, ptr %18)
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @malloc(i64)

declare i32 @sprintf(ptr, ptr, ...)

declare i64 @strlen(ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @printf(ptr, ...)

define void @process_model(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %m = alloca ptr, align 8
  store ptr %0, ptr %m, align 8
  %prop_ptr_base = load ptr, ptr %m, align 8
  %prop_ptr = getelementptr inbounds %TestModel, ptr %prop_ptr_base, i32 0, i32 1
  %name_val = load ptr, ptr %prop_ptr, align 8
  %1 = icmp eq ptr %name_val, null
  %2 = select i1 %1, ptr @10, ptr %name_val
  %3 = call i64 @strlen(ptr @9)
  %4 = call i64 @strlen(ptr %2)
  %5 = add i64 %3, %4
  %6 = add i64 %5, 1
  %7 = call ptr @malloc(i64 %6)
  %8 = call ptr @strcpy(ptr %7, ptr @9)
  %9 = call ptr @strcat(ptr %7, ptr %2)
  %print_call = call i32 (ptr, ...) @printf(ptr @12, ptr %7)
  %m_val = load ptr, ptr %m, align 8
  call void @print_any_id(ptr %m_val)
  ret void
}

declare ptr @calloc(i64, i64)

declare i32 @strcmp(ptr, ptr)

declare ptr @realloc(ptr, i64)
