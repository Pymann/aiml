; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%GenericList = type { i32, i32, ptr }
%Payload = type { i32 }
%__aiml_code_t = type { ptr, ptr, ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [17 x i8] c"worker_task: id=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@b_id = private unnamed_addr constant [9 x i8] c"65557ab3\00", align 1
@b_chunk = private unnamed_addr constant [50 x i8] c"class:\0A  name: Payload\0A  members:\0A    - [id, i32]\00", align 1
@b_chunk.1 = private unnamed_addr constant [97 x i8] c"def:\0A  name: worker_task\0A  args: [[p, Payload]]\0A  body:\0A    - print: [\22worker_task: id=${p.id}\22]\00", align 1
@b_sc = private unnamed_addr constant [25 x i8] c"{\22call\22:[\22worker_task\22]}\00", align 1
@2 = private unnamed_addr constant [29 x i8] c"bundle toString length > 0: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [12 x i8] c"worker_task\00", align 1
@5 = private unnamed_addr constant [23 x i8] c"contains worker_task: \00", align 1
@6 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@7 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [8 x i8] c"Payload\00", align 1
@10 = private unnamed_addr constant [19 x i8] c"contains Payload: \00", align 1
@11 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@12 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [14 x i8] c"xyz_not_there\00", align 1
@15 = private unnamed_addr constant [25 x i8] c"contains xyz_not_there: \00", align 1
@16 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@17 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@bi_chunk = private unnamed_addr constant [50 x i8] c"class:\0A  name: Payload\0A  members:\0A    - [id, i32]\00", align 1
@bi_id = private unnamed_addr constant [9 x i8] c"7cbac7d9\00", align 1
@tn = private unnamed_addr constant [8 x i8] c"Payload\00", align 1
@fk_id = private unnamed_addr constant [3 x i8] c"id\00", align 1
@bi_pfx = private unnamed_addr constant [59 x i8] c"- decl: [_v, Payload]\0A- set: [_v, {call:[sys.deserialize,'\00", align 1
@bi_sfx = private unnamed_addr constant [16 x i8] c"','Payload']}]\0A\00", align 1
@bi_sc = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@19 = private unnamed_addr constant [27 x i8] c"instance bundle toString: \00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [8 x i8] c"Payload\00", align 1
@22 = private unnamed_addr constant [35 x i8] c"instance bundle contains Payload: \00", align 1
@23 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@24 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %pkg = alloca ptr, align 8
  store ptr null, ptr %pkg, align 8
  %bl_mem = call ptr @malloc(i64 16)
  %sizePtr = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 2
  store ptr %0, ptr %dataPtr, align 8
  %sizePtr2 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 0
  %capPtr3 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 1
  %size = load i32, ptr %sizePtr2, align 4
  %capacity = load i32, ptr %capPtr3, align 4
  %1 = icmp eq i32 %size, %capacity
  br i1 %1, label %grow, label %cont

entry1:                                           ; No predecessors!

grow:                                             ; preds = %entry
  %2 = mul i32 %capacity, 2
  store i32 %2, ptr %capPtr3, align 4
  %dataPtr4 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 2
  %3 = load ptr, ptr %dataPtr4, align 8
  %4 = zext i32 %2 to i64
  %5 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %4
  %6 = call ptr @realloc(ptr %3, i64 %5)
  store ptr %6, ptr %dataPtr4, align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataPtr5 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr5, align 8
  %7 = getelementptr inbounds ptr, ptr %dataBase, i32 %size
  store ptr @b_chunk, ptr %7, align 8
  %8 = add i32 %size, 1
  store i32 %8, ptr %sizePtr2, align 4
  %sizePtr6 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 0
  %capPtr7 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 1
  %size8 = load i32, ptr %sizePtr6, align 4
  %capacity9 = load i32, ptr %capPtr7, align 4
  %9 = icmp eq i32 %size8, %capacity9
  br i1 %9, label %grow10, label %cont11

grow10:                                           ; preds = %cont
  %10 = mul i32 %capacity9, 2
  store i32 %10, ptr %capPtr7, align 4
  %dataPtr12 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 2
  %11 = load ptr, ptr %dataPtr12, align 8
  %12 = zext i32 %10 to i64
  %13 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %12
  %14 = call ptr @realloc(ptr %11, i64 %13)
  store ptr %14, ptr %dataPtr12, align 8
  br label %cont11

cont11:                                           ; preds = %grow10, %cont
  %dataPtr13 = getelementptr inbounds %GenericList, ptr %bl_mem, i32 0, i32 2
  %dataBase14 = load ptr, ptr %dataPtr13, align 8
  %15 = getelementptr inbounds ptr, ptr %dataBase14, i32 %size8
  store ptr @b_chunk.1, ptr %15, align 8
  %16 = add i32 %size8, 1
  store i32 %16, ptr %sizePtr6, align 4
  %bl_mem15 = call ptr @malloc(i64 16)
  %sizePtr16 = getelementptr inbounds %GenericList, ptr %bl_mem15, i32 0, i32 0
  store i32 0, ptr %sizePtr16, align 4
  %capPtr17 = getelementptr inbounds %GenericList, ptr %bl_mem15, i32 0, i32 1
  store i32 4, ptr %capPtr17, align 4
  %17 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr18 = getelementptr inbounds %GenericList, ptr %bl_mem15, i32 0, i32 2
  store ptr %17, ptr %dataPtr18, align 8
  %code_new = call ptr @__aiml_code_new(ptr @b_id, ptr @b_sc, ptr %bl_mem, ptr %bl_mem15)
  store ptr %code_new, ptr %pkg, align 8
  %src = alloca ptr, align 8
  store ptr null, ptr %src, align 8
  %code_ptr = load ptr, ptr %pkg, align 8
  %code_tostring = call ptr @__aiml_code_tostring(ptr %code_ptr)
  store ptr %code_tostring, ptr %src, align 8
  %src_val = load ptr, ptr %src, align 8
  %18 = call ptr @__aiml_str_concat(ptr @2, ptr %src_val)
  %print_null_check = icmp eq ptr %18, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.2, ptr %18
  %print_call = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str)
  %has_worker = alloca i8, align 1
  store i8 0, ptr %has_worker, align 1
  %code_ptr19 = load ptr, ptr %pkg, align 8
  %code_contains = call i8 @__aiml_code_contains(ptr %code_ptr19, ptr @4)
  store i8 %code_contains, ptr %has_worker, align 1
  %has_worker_val = load i8, ptr %has_worker, align 1
  %19 = icmp ne i8 %has_worker_val, 0
  %20 = select i1 %19, ptr @6, ptr @7
  %21 = call ptr @__aiml_str_concat(ptr @5, ptr %20)
  %print_null_check20 = icmp eq ptr %21, null
  %print_safe_str21 = select i1 %print_null_check20, ptr @print_null_str.3, ptr %21
  %print_call22 = call i32 (ptr, ...) @printf(ptr @8, ptr %print_safe_str21)
  %has_payload = alloca i8, align 1
  store i8 0, ptr %has_payload, align 1
  %code_ptr23 = load ptr, ptr %pkg, align 8
  %code_contains24 = call i8 @__aiml_code_contains(ptr %code_ptr23, ptr @9)
  store i8 %code_contains24, ptr %has_payload, align 1
  %has_payload_val = load i8, ptr %has_payload, align 1
  %22 = icmp ne i8 %has_payload_val, 0
  %23 = select i1 %22, ptr @11, ptr @12
  %24 = call ptr @__aiml_str_concat(ptr @10, ptr %23)
  %print_null_check25 = icmp eq ptr %24, null
  %print_safe_str26 = select i1 %print_null_check25, ptr @print_null_str.4, ptr %24
  %print_call27 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str26)
  %has_xyz = alloca i8, align 1
  store i8 0, ptr %has_xyz, align 1
  %code_ptr28 = load ptr, ptr %pkg, align 8
  %code_contains29 = call i8 @__aiml_code_contains(ptr %code_ptr28, ptr @14)
  store i8 %code_contains29, ptr %has_xyz, align 1
  %has_xyz_val = load i8, ptr %has_xyz, align 1
  %25 = icmp ne i8 %has_xyz_val, 0
  %26 = select i1 %25, ptr @16, ptr @17
  %27 = call ptr @__aiml_str_concat(ptr @15, ptr %26)
  %print_null_check30 = icmp eq ptr %27, null
  %print_safe_str31 = select i1 %print_null_check30, ptr @print_null_str.5, ptr %27
  %print_call32 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str31)
  %p = alloca %Payload, align 8
  store %Payload zeroinitializer, ptr %p, align 4
  %set_prop_ptr = getelementptr inbounds %Payload, ptr %p, i32 0, i32 0
  store i32 99, ptr %set_prop_ptr, align 4
  %inst_pkg = alloca ptr, align 8
  store ptr null, ptr %inst_pkg, align 8
  %bl_mem33 = call ptr @malloc(i64 16)
  %sizePtr34 = getelementptr inbounds %GenericList, ptr %bl_mem33, i32 0, i32 0
  store i32 0, ptr %sizePtr34, align 4
  %capPtr35 = getelementptr inbounds %GenericList, ptr %bl_mem33, i32 0, i32 1
  store i32 4, ptr %capPtr35, align 4
  %28 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr36 = getelementptr inbounds %GenericList, ptr %bl_mem33, i32 0, i32 2
  store ptr %28, ptr %dataPtr36, align 8
  %sizePtr37 = getelementptr inbounds %GenericList, ptr %bl_mem33, i32 0, i32 0
  %capPtr38 = getelementptr inbounds %GenericList, ptr %bl_mem33, i32 0, i32 1
  %size39 = load i32, ptr %sizePtr37, align 4
  %capacity40 = load i32, ptr %capPtr38, align 4
  %29 = icmp eq i32 %size39, %capacity40
  br i1 %29, label %grow41, label %cont42

grow41:                                           ; preds = %cont11
  %30 = mul i32 %capacity40, 2
  store i32 %30, ptr %capPtr38, align 4
  %dataPtr43 = getelementptr inbounds %GenericList, ptr %bl_mem33, i32 0, i32 2
  %31 = load ptr, ptr %dataPtr43, align 8
  %32 = zext i32 %30 to i64
  %33 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %32
  %34 = call ptr @realloc(ptr %31, i64 %33)
  store ptr %34, ptr %dataPtr43, align 8
  br label %cont42

cont42:                                           ; preds = %grow41, %cont11
  %dataPtr44 = getelementptr inbounds %GenericList, ptr %bl_mem33, i32 0, i32 2
  %dataBase45 = load ptr, ptr %dataPtr44, align 8
  %35 = getelementptr inbounds ptr, ptr %dataBase45, i32 %size39
  store ptr @bi_chunk, ptr %35, align 8
  %36 = add i32 %size39, 1
  store i32 %36, ptr %sizePtr37, align 4
  %ser_r = call ptr @__aiml_ser_Payload(ptr %p)
  %37 = call ptr @__aiml_str_concat(ptr @bi_pfx, ptr %ser_r)
  %38 = call ptr @__aiml_str_concat(ptr %37, ptr @bi_sfx)
  %bl_mem46 = call ptr @malloc(i64 16)
  %sizePtr47 = getelementptr inbounds %GenericList, ptr %bl_mem46, i32 0, i32 0
  store i32 0, ptr %sizePtr47, align 4
  %capPtr48 = getelementptr inbounds %GenericList, ptr %bl_mem46, i32 0, i32 1
  store i32 4, ptr %capPtr48, align 4
  %39 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr49 = getelementptr inbounds %GenericList, ptr %bl_mem46, i32 0, i32 2
  store ptr %39, ptr %dataPtr49, align 8
  %sizePtr50 = getelementptr inbounds %GenericList, ptr %bl_mem46, i32 0, i32 0
  %capPtr51 = getelementptr inbounds %GenericList, ptr %bl_mem46, i32 0, i32 1
  %size52 = load i32, ptr %sizePtr50, align 4
  %capacity53 = load i32, ptr %capPtr51, align 4
  %40 = icmp eq i32 %size52, %capacity53
  br i1 %40, label %grow54, label %cont55

grow54:                                           ; preds = %cont42
  %41 = mul i32 %capacity53, 2
  store i32 %41, ptr %capPtr51, align 4
  %dataPtr56 = getelementptr inbounds %GenericList, ptr %bl_mem46, i32 0, i32 2
  %42 = load ptr, ptr %dataPtr56, align 8
  %43 = zext i32 %41 to i64
  %44 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %43
  %45 = call ptr @realloc(ptr %42, i64 %44)
  store ptr %45, ptr %dataPtr56, align 8
  br label %cont55

cont55:                                           ; preds = %grow54, %cont42
  %dataPtr57 = getelementptr inbounds %GenericList, ptr %bl_mem46, i32 0, i32 2
  %dataBase58 = load ptr, ptr %dataPtr57, align 8
  %46 = getelementptr inbounds ptr, ptr %dataBase58, i32 %size52
  store ptr %38, ptr %46, align 8
  %47 = add i32 %size52, 1
  store i32 %47, ptr %sizePtr50, align 4
  %code_new59 = call ptr @__aiml_code_new(ptr @bi_id, ptr @bi_sc, ptr %bl_mem33, ptr %bl_mem46)
  store ptr %code_new59, ptr %inst_pkg, align 8
  %inst_src = alloca ptr, align 8
  store ptr null, ptr %inst_src, align 8
  %code_ptr60 = load ptr, ptr %inst_pkg, align 8
  %code_tostring61 = call ptr @__aiml_code_tostring(ptr %code_ptr60)
  store ptr %code_tostring61, ptr %inst_src, align 8
  %inst_src_val = load ptr, ptr %inst_src, align 8
  %48 = call ptr @__aiml_str_concat(ptr @19, ptr %inst_src_val)
  %print_null_check62 = icmp eq ptr %48, null
  %print_safe_str63 = select i1 %print_null_check62, ptr @print_null_str.6, ptr %48
  %print_call64 = call i32 (ptr, ...) @printf(ptr @20, ptr %print_safe_str63)
  %inst_has_payload = alloca i8, align 1
  store i8 0, ptr %inst_has_payload, align 1
  %code_ptr65 = load ptr, ptr %inst_pkg, align 8
  %code_contains66 = call i8 @__aiml_code_contains(ptr %code_ptr65, ptr @21)
  store i8 %code_contains66, ptr %inst_has_payload, align 1
  %inst_has_payload_val = load i8, ptr %inst_has_payload, align 1
  %49 = icmp ne i8 %inst_has_payload_val, 0
  %50 = select i1 %49, ptr @23, ptr @24
  %51 = call ptr @__aiml_str_concat(ptr @22, ptr %50)
  %print_null_check67 = icmp eq ptr %51, null
  %print_safe_str68 = select i1 %print_null_check67, ptr @print_null_str.7, ptr %51
  %print_call69 = call i32 (ptr, ...) @printf(ptr @25, ptr %print_safe_str68)
  ret void
}

define void @worker_task(%Payload %0) personality ptr @__gxx_personality_v0 {
entry:
  %p = alloca %Payload, align 8
  store %Payload %0, ptr %p, align 4
  %prop_ptr = getelementptr inbounds %Payload, ptr %p, i32 0, i32 0
  %id_val = load i32, ptr %prop_ptr, align 4
  %1 = call ptr @__aiml_int_to_string(i32 %id_val)
  %2 = call ptr @__aiml_str_concat(ptr @0, ptr %1)
  %print_null_check = icmp eq ptr %2, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %2
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr %print_safe_str)
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @malloc(i64)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare i64 @strlen(ptr)

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

declare i32 @printf(ptr, ...)

declare ptr @realloc(ptr, i64)

; Function Attrs: noinline
define internal ptr @__aiml_code_new(ptr %0, ptr %1, ptr %2, ptr %3) #0 {
entry:
  %code_raw = call ptr @malloc(i64 32)
  %id_gep = getelementptr inbounds %__aiml_code_t, ptr %code_raw, i32 0, i32 0
  store ptr %0, ptr %id_gep, align 8
  %sc_gep = getelementptr inbounds %__aiml_code_t, ptr %code_raw, i32 0, i32 1
  store ptr %1, ptr %sc_gep, align 8
  %cc_gep = getelementptr inbounds %__aiml_code_t, ptr %code_raw, i32 0, i32 2
  store ptr %2, ptr %cc_gep, align 8
  %dc_gep = getelementptr inbounds %__aiml_code_t, ptr %code_raw, i32 0, i32 3
  store ptr %3, ptr %dc_gep, align 8
  ret ptr %code_raw
}

; Function Attrs: noinline
define internal ptr @__aiml_code_tostring(ptr %0) #0 {
entry:
  %str = call ptr @aiml_code_tostring(ptr %0)
  ret ptr %str
}

declare ptr @aiml_code_tostring(ptr)

; Function Attrs: noinline
define internal i8 @__aiml_code_contains(ptr %0, ptr %1) #0 {
entry:
  %res = call i32 @aiml_code_contains(ptr %0, ptr %1)
  %res_i8 = trunc i32 %res to i8
  ret i8 %res_i8
}

declare i32 @aiml_code_contains(ptr, ptr)

; Function Attrs: noinline
define internal ptr @__aiml_ser_Payload(ptr %sp) #0 {
entry:
  %w = call ptr @aiml_jw_new()
  call void @aiml_jw_set_type(ptr %w, ptr @tn)
  %fp = getelementptr inbounds %Payload, ptr %sp, i32 0, i32 0
  %v = load i32, ptr %fp, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_id, i32 %v)
  %json = call ptr @aiml_jw_done(ptr %w)
  ret ptr %json
}

declare ptr @aiml_jw_new()

declare void @aiml_jw_set_type(ptr, ptr)

declare void @aiml_jw_set_i32(ptr, ptr, i32)

declare ptr @aiml_jw_done(ptr)

attributes #0 = { noinline }
