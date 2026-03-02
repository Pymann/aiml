; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [32 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Entity = type { i32, i32 }
%list_f32 = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%"list_ptr<VecResult>" = type { i32, i32, ptr }
%VecResult = type { ptr, float }
%GameState = type { i32, i32 }
%Health = type { i32 }
%Pos = type { i32 }
%"list_ptr<Health>" = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [9 x i8] c"entities\00", align 1
@1 = private unnamed_addr constant [7 x i8] c"Entity\00", align 1
@2 = private unnamed_addr constant [14 x i8] c"inserted id: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [5 x i8] c"hp: \00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [6 x i8] c"len: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"cfg\00", align 1
@9 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@10 = private unnamed_addr constant [5 x i8] c"dark\00", align 1
@11 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@12 = private unnamed_addr constant [8 x i8] c"theme: \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@15 = private unnamed_addr constant [6 x i8] c"has: \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [7 x i8] c"klen: \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [5 x i8] c"vecs\00", align 1
@20 = private unnamed_addr constant [5 x i8] c"doc1\00", align 1
@21 = private unnamed_addr constant [5 x i8] c"doc2\00", align 1
@22 = private unnamed_addr constant [9 x i8] c"top id: \00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [13 x i8] c"game_history\00", align 1
@25 = private unnamed_addr constant [9 x i8] c"ts len: \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [16 x i8] c"score at 1500: \00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@29 = private unnamed_addr constant [12 x i8] c"health_data\00", align 1
@30 = private unnamed_addr constant [7 x i8] c"Health\00", align 1
@31 = private unnamed_addr constant [9 x i8] c"pos_data\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"Pos\00", align 1
@33 = private unnamed_addr constant [9 x i8] c"sys hp: \00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@35 = private unnamed_addr constant [14 x i8] c"query count: \00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@36 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [2 x [16 x ptr]] [[16 x ptr] [ptr @tick_hp, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @is_high_hp, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [2 x ptr] [ptr @tick_hp, ptr @is_high_hp]
@__aiml_fptable_versions = global [2 x i32] [i32 1, i32 1]
@__aiml_fname_tick_hp = private constant [8 x i8] c"tick_hp\00"
@__aiml_fname_is_high_hp = private constant [11 x i8] c"is_high_hp\00"
@__aiml_func_names = constant [2 x ptr] [ptr @__aiml_fname_tick_hp, ptr @__aiml_fname_is_high_hp]
@__aiml_func_count = constant i32 2
@__aiml_sname_Entity = private constant [7 x i8] c"Entity\00"
@__aiml_sfield_Entity_id = private constant [3 x i8] c"id\00"
@__aiml_sfield_Entity_hp = private constant [3 x i8] c"hp\00"
@__aiml_sname_GameState = private constant [10 x i8] c"GameState\00"
@__aiml_sfield_GameState_score = private constant [6 x i8] c"score\00"
@__aiml_sfield_GameState_hp = private constant [3 x i8] c"hp\00"
@__aiml_sname_Health = private constant [7 x i8] c"Health\00"
@__aiml_sfield_Health_hp = private constant [3 x i8] c"hp\00"
@__aiml_sname_Pos = private constant [4 x i8] c"Pos\00"
@__aiml_sfield_Pos_x = private constant [2 x i8] c"x\00"
@__aiml_schemas = global [4 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_Entity, i32 1, i32 2, i32 8, i32 8, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Entity_id }, %AimlFieldInfo { ptr @__aiml_sfield_Entity_hp }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_GameState, i32 1, i32 2, i32 8, i32 8, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_GameState_score }, %AimlFieldInfo { ptr @__aiml_sfield_GameState_hp }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Health, i32 1, i32 1, i32 4, i32 4, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Health_hp }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Pos, i32 1, i32 1, i32 4, i32 4, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Pos_x }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 4

define void @__aiml_init() {
entry:
  %entities = alloca ptr, align 8
  store ptr null, ptr %entities, align 8
  %0 = call ptr @aiml_db_store_open(ptr @0, ptr @1, i64 8)
  store ptr %0, ptr %entities, align 8
  %e = alloca %Entity, align 8
  store %Entity zeroinitializer, ptr %e, align 4
  %set_prop_ptr = getelementptr inbounds %Entity, ptr %e, i32 0, i32 0
  store i32 42, ptr %set_prop_ptr, align 4
  %set_prop_ptr1 = getelementptr inbounds %Entity, ptr %e, i32 0, i32 1
  store i32 100, ptr %set_prop_ptr1, align 4
  %eid = alloca i32, align 4
  store i32 0, ptr %eid, align 4
  %e_val = load %Entity, ptr %e, align 4
  %store_ptr = load ptr, ptr %entities, align 8
  %1 = call i32 @aiml_db_store_insert(ptr %store_ptr, ptr %e, i64 8)
  store i32 %1, ptr %eid, align 4
  %eid_val = load i32, ptr %eid, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %eid_val)
  %3 = call ptr @__aiml_str_concat(ptr @2, ptr %2)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str)
  %p = alloca ptr, align 8
  store ptr null, ptr %p, align 8
  %store_ptr2 = load ptr, ptr %entities, align 8
  %4 = call ptr @aiml_db_store_get(ptr %store_ptr2, i32 1)
  store ptr %4, ptr %p, align 8
  %prop_ptr_base = load ptr, ptr %p, align 8
  %prop_ptr = getelementptr inbounds %Entity, ptr %prop_ptr_base, i32 0, i32 1
  %hp_val = load i32, ptr %prop_ptr, align 4
  %5 = call ptr @__aiml_int_to_string(i32 %hp_val)
  %6 = call ptr @__aiml_str_concat(ptr @4, ptr %5)
  %print_null_check3 = icmp eq ptr %6, null
  %print_safe_str4 = select i1 %print_null_check3, ptr @print_null_str.1, ptr %6
  %print_call5 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str4)
  %n = alloca i32, align 4
  store i32 0, ptr %n, align 4
  %store_ptr6 = load ptr, ptr %entities, align 8
  %7 = call i32 @aiml_db_store_len(ptr %store_ptr6)
  store i32 %7, ptr %n, align 4
  %n_val = load i32, ptr %n, align 4
  %8 = call ptr @__aiml_int_to_string(i32 %n_val)
  %9 = call ptr @__aiml_str_concat(ptr @6, ptr %8)
  %print_null_check7 = icmp eq ptr %9, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.2, ptr %9
  %print_call9 = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str8)
  %cfg = alloca ptr, align 8
  store ptr null, ptr %cfg, align 8
  %10 = call ptr @aiml_db_kv_open(ptr @8, i64 0)
  store ptr %10, ptr %cfg, align 8
  %kv_ptr = load ptr, ptr %cfg, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @9, ptr @10, i64 0)
  %v = alloca ptr, align 8
  store ptr null, ptr %v, align 8
  %kv_ptr10 = load ptr, ptr %cfg, align 8
  %11 = call ptr @aiml_db_kv_get(ptr %kv_ptr10, ptr @11)
  store ptr %11, ptr %v, align 8
  %v_val = load ptr, ptr %v, align 8
  %12 = call ptr @__aiml_str_concat(ptr @12, ptr %v_val)
  %print_null_check11 = icmp eq ptr %12, null
  %print_safe_str12 = select i1 %print_null_check11, ptr @print_null_str.3, ptr %12
  %print_call13 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str12)
  %has = alloca i32, align 4
  store i32 0, ptr %has, align 4
  %kv_ptr14 = load ptr, ptr %cfg, align 8
  %13 = call i32 @aiml_db_kv_has(ptr %kv_ptr14, ptr @14)
  store i32 %13, ptr %has, align 4
  %has_val = load i32, ptr %has, align 4
  %14 = call ptr @__aiml_int_to_string(i32 %has_val)
  %15 = call ptr @__aiml_str_concat(ptr @15, ptr %14)
  %print_null_check15 = icmp eq ptr %15, null
  %print_safe_str16 = select i1 %print_null_check15, ptr @print_null_str.4, ptr %15
  %print_call17 = call i32 (ptr, ...) @printf(ptr @16, ptr %print_safe_str16)
  %klen = alloca i32, align 4
  store i32 0, ptr %klen, align 4
  %kv_ptr18 = load ptr, ptr %cfg, align 8
  %16 = call i32 @aiml_db_kv_len(ptr %kv_ptr18)
  store i32 %16, ptr %klen, align 4
  %klen_val = load i32, ptr %klen, align 4
  %17 = call ptr @__aiml_int_to_string(i32 %klen_val)
  %18 = call ptr @__aiml_str_concat(ptr @17, ptr %17)
  %print_null_check19 = icmp eq ptr %18, null
  %print_safe_str20 = select i1 %print_null_check19, ptr @print_null_str.5, ptr %18
  %print_call21 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str20)
  %vs = alloca ptr, align 8
  store ptr null, ptr %vs, align 8
  %19 = call ptr @aiml_db_vec_open(ptr @19, i32 3)
  store ptr %19, ptr %vs, align 8
  %v1 = alloca %list_f32, align 8
  store %list_f32 zeroinitializer, ptr %v1, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %20 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  store ptr %20, ptr %dataPtr, align 8
  %sizePtr22 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %capPtr23 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  %size = load i32, ptr %sizePtr22, align 4
  %capacity = load i32, ptr %capPtr23, align 4
  %21 = icmp eq i32 %size, %capacity
  br i1 %21, label %grow, label %cont

grow:                                             ; preds = %entry
  %22 = mul i32 %capacity, 2
  store i32 %22, ptr %capPtr23, align 4
  %dataPtr24 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %23 = load ptr, ptr %dataPtr24, align 8
  %24 = zext i32 %22 to i64
  %25 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %24
  %26 = call ptr @realloc(ptr %23, i64 %25)
  store ptr %26, ptr %dataPtr24, align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataPtr25 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr25, align 8
  %27 = getelementptr inbounds i32, ptr %dataBase, i32 %size
  store i32 1, ptr %27, align 4
  %28 = add i32 %size, 1
  store i32 %28, ptr %sizePtr22, align 4
  %sizePtr26 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %capPtr27 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  %size28 = load i32, ptr %sizePtr26, align 4
  %capacity29 = load i32, ptr %capPtr27, align 4
  %29 = icmp eq i32 %size28, %capacity29
  br i1 %29, label %grow30, label %cont31

grow30:                                           ; preds = %cont
  %30 = mul i32 %capacity29, 2
  store i32 %30, ptr %capPtr27, align 4
  %dataPtr32 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %31 = load ptr, ptr %dataPtr32, align 8
  %32 = zext i32 %30 to i64
  %33 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %32
  %34 = call ptr @realloc(ptr %31, i64 %33)
  store ptr %34, ptr %dataPtr32, align 8
  br label %cont31

cont31:                                           ; preds = %grow30, %cont
  %dataPtr33 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %dataBase34 = load ptr, ptr %dataPtr33, align 8
  %35 = getelementptr inbounds i32, ptr %dataBase34, i32 %size28
  store i32 0, ptr %35, align 4
  %36 = add i32 %size28, 1
  store i32 %36, ptr %sizePtr26, align 4
  %sizePtr35 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %capPtr36 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  %size37 = load i32, ptr %sizePtr35, align 4
  %capacity38 = load i32, ptr %capPtr36, align 4
  %37 = icmp eq i32 %size37, %capacity38
  br i1 %37, label %grow39, label %cont40

grow39:                                           ; preds = %cont31
  %38 = mul i32 %capacity38, 2
  store i32 %38, ptr %capPtr36, align 4
  %dataPtr41 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %39 = load ptr, ptr %dataPtr41, align 8
  %40 = zext i32 %38 to i64
  %41 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %40
  %42 = call ptr @realloc(ptr %39, i64 %41)
  store ptr %42, ptr %dataPtr41, align 8
  br label %cont40

cont40:                                           ; preds = %grow39, %cont31
  %dataPtr42 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %dataBase43 = load ptr, ptr %dataPtr42, align 8
  %43 = getelementptr inbounds i32, ptr %dataBase43, i32 %size37
  store i32 0, ptr %43, align 4
  %44 = add i32 %size37, 1
  store i32 %44, ptr %sizePtr35, align 4
  %v1_val = load %list_f32, ptr %v1, align 8
  %vs_ptr = load ptr, ptr %vs, align 8
  %vsz_fld = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %list_sz = load i32, ptr %vsz_fld, align 4
  %vdata_fld = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %data_ptr = load ptr, ptr %vdata_fld, align 8
  call void @aiml_db_vec_insert(ptr %vs_ptr, ptr @20, ptr %data_ptr, i32 %list_sz)
  %v2 = alloca %list_f32, align 8
  store %list_f32 zeroinitializer, ptr %v2, align 8
  %sizePtr44 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  store i32 0, ptr %sizePtr44, align 4
  %capPtr45 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  store i32 4, ptr %capPtr45, align 4
  %45 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  %dataPtr46 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  store ptr %45, ptr %dataPtr46, align 8
  %sizePtr47 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %capPtr48 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  %size49 = load i32, ptr %sizePtr47, align 4
  %capacity50 = load i32, ptr %capPtr48, align 4
  %46 = icmp eq i32 %size49, %capacity50
  br i1 %46, label %grow51, label %cont52

grow51:                                           ; preds = %cont40
  %47 = mul i32 %capacity50, 2
  store i32 %47, ptr %capPtr48, align 4
  %dataPtr53 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %48 = load ptr, ptr %dataPtr53, align 8
  %49 = zext i32 %47 to i64
  %50 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %49
  %51 = call ptr @realloc(ptr %48, i64 %50)
  store ptr %51, ptr %dataPtr53, align 8
  br label %cont52

cont52:                                           ; preds = %grow51, %cont40
  %dataPtr54 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %dataBase55 = load ptr, ptr %dataPtr54, align 8
  %52 = getelementptr inbounds i32, ptr %dataBase55, i32 %size49
  store i32 0, ptr %52, align 4
  %53 = add i32 %size49, 1
  store i32 %53, ptr %sizePtr47, align 4
  %sizePtr56 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %capPtr57 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  %size58 = load i32, ptr %sizePtr56, align 4
  %capacity59 = load i32, ptr %capPtr57, align 4
  %54 = icmp eq i32 %size58, %capacity59
  br i1 %54, label %grow60, label %cont61

grow60:                                           ; preds = %cont52
  %55 = mul i32 %capacity59, 2
  store i32 %55, ptr %capPtr57, align 4
  %dataPtr62 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %56 = load ptr, ptr %dataPtr62, align 8
  %57 = zext i32 %55 to i64
  %58 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %57
  %59 = call ptr @realloc(ptr %56, i64 %58)
  store ptr %59, ptr %dataPtr62, align 8
  br label %cont61

cont61:                                           ; preds = %grow60, %cont52
  %dataPtr63 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %dataBase64 = load ptr, ptr %dataPtr63, align 8
  %60 = getelementptr inbounds i32, ptr %dataBase64, i32 %size58
  store i32 1, ptr %60, align 4
  %61 = add i32 %size58, 1
  store i32 %61, ptr %sizePtr56, align 4
  %sizePtr65 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %capPtr66 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  %size67 = load i32, ptr %sizePtr65, align 4
  %capacity68 = load i32, ptr %capPtr66, align 4
  %62 = icmp eq i32 %size67, %capacity68
  br i1 %62, label %grow69, label %cont70

grow69:                                           ; preds = %cont61
  %63 = mul i32 %capacity68, 2
  store i32 %63, ptr %capPtr66, align 4
  %dataPtr71 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %64 = load ptr, ptr %dataPtr71, align 8
  %65 = zext i32 %63 to i64
  %66 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %65
  %67 = call ptr @realloc(ptr %64, i64 %66)
  store ptr %67, ptr %dataPtr71, align 8
  br label %cont70

cont70:                                           ; preds = %grow69, %cont61
  %dataPtr72 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %dataBase73 = load ptr, ptr %dataPtr72, align 8
  %68 = getelementptr inbounds i32, ptr %dataBase73, i32 %size67
  store i32 0, ptr %68, align 4
  %69 = add i32 %size67, 1
  store i32 %69, ptr %sizePtr65, align 4
  %v2_val = load %list_f32, ptr %v2, align 8
  %vs_ptr74 = load ptr, ptr %vs, align 8
  %vsz_fld75 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %list_sz76 = load i32, ptr %vsz_fld75, align 4
  %vdata_fld77 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %data_ptr78 = load ptr, ptr %vdata_fld77, align 8
  call void @aiml_db_vec_insert(ptr %vs_ptr74, ptr @21, ptr %data_ptr78, i32 %list_sz76)
  %q = alloca %list_f32, align 8
  store %list_f32 zeroinitializer, ptr %q, align 8
  %sizePtr79 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  store i32 0, ptr %sizePtr79, align 4
  %capPtr80 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  store i32 4, ptr %capPtr80, align 4
  %70 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  %dataPtr81 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  store ptr %70, ptr %dataPtr81, align 8
  %sizePtr82 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %capPtr83 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  %size84 = load i32, ptr %sizePtr82, align 4
  %capacity85 = load i32, ptr %capPtr83, align 4
  %71 = icmp eq i32 %size84, %capacity85
  br i1 %71, label %grow86, label %cont87

grow86:                                           ; preds = %cont70
  %72 = mul i32 %capacity85, 2
  store i32 %72, ptr %capPtr83, align 4
  %dataPtr88 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %73 = load ptr, ptr %dataPtr88, align 8
  %74 = zext i32 %72 to i64
  %75 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %74
  %76 = call ptr @realloc(ptr %73, i64 %75)
  store ptr %76, ptr %dataPtr88, align 8
  br label %cont87

cont87:                                           ; preds = %grow86, %cont70
  %dataPtr89 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %dataBase90 = load ptr, ptr %dataPtr89, align 8
  %77 = getelementptr inbounds i32, ptr %dataBase90, i32 %size84
  store i32 1, ptr %77, align 4
  %78 = add i32 %size84, 1
  store i32 %78, ptr %sizePtr82, align 4
  %sizePtr91 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %capPtr92 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  %size93 = load i32, ptr %sizePtr91, align 4
  %capacity94 = load i32, ptr %capPtr92, align 4
  %79 = icmp eq i32 %size93, %capacity94
  br i1 %79, label %grow95, label %cont96

grow95:                                           ; preds = %cont87
  %80 = mul i32 %capacity94, 2
  store i32 %80, ptr %capPtr92, align 4
  %dataPtr97 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %81 = load ptr, ptr %dataPtr97, align 8
  %82 = zext i32 %80 to i64
  %83 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %82
  %84 = call ptr @realloc(ptr %81, i64 %83)
  store ptr %84, ptr %dataPtr97, align 8
  br label %cont96

cont96:                                           ; preds = %grow95, %cont87
  %dataPtr98 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %dataBase99 = load ptr, ptr %dataPtr98, align 8
  %85 = getelementptr inbounds i32, ptr %dataBase99, i32 %size93
  store i32 0, ptr %85, align 4
  %86 = add i32 %size93, 1
  store i32 %86, ptr %sizePtr91, align 4
  %sizePtr100 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %capPtr101 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  %size102 = load i32, ptr %sizePtr100, align 4
  %capacity103 = load i32, ptr %capPtr101, align 4
  %87 = icmp eq i32 %size102, %capacity103
  br i1 %87, label %grow104, label %cont105

grow104:                                          ; preds = %cont96
  %88 = mul i32 %capacity103, 2
  store i32 %88, ptr %capPtr101, align 4
  %dataPtr106 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %89 = load ptr, ptr %dataPtr106, align 8
  %90 = zext i32 %88 to i64
  %91 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %90
  %92 = call ptr @realloc(ptr %89, i64 %91)
  store ptr %92, ptr %dataPtr106, align 8
  br label %cont105

cont105:                                          ; preds = %grow104, %cont96
  %dataPtr107 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %dataBase108 = load ptr, ptr %dataPtr107, align 8
  %93 = getelementptr inbounds i32, ptr %dataBase108, i32 %size102
  store i32 0, ptr %93, align 4
  %94 = add i32 %size102, 1
  store i32 %94, ptr %sizePtr100, align 4
  %results = alloca %"list_ptr<VecResult>", align 8
  store %"list_ptr<VecResult>" zeroinitializer, ptr %results, align 8
  %sizePtr109 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 0
  store i32 0, ptr %sizePtr109, align 4
  %capPtr110 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 1
  store i32 4, ptr %capPtr110, align 4
  %95 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr111 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 2
  store ptr %95, ptr %dataPtr111, align 8
  %q_val = load %list_f32, ptr %q, align 8
  %vs_ptr112 = load ptr, ptr %vs, align 8
  %vsz_fld113 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %list_sz114 = load i32, ptr %vsz_fld113, align 4
  %vdata_fld115 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %data_ptr116 = load ptr, ptr %vdata_fld115, align 8
  %vecres_alloca = alloca %GenericList, align 8
  %sizePtr117 = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 0
  store i32 0, ptr %sizePtr117, align 4
  %capPtr118 = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 1
  store i32 8, ptr %capPtr118, align 4
  %96 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr119 = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 2
  store ptr %96, ptr %dataPtr119, align 8
  call void @aiml_db_vec_search(ptr %vs_ptr112, ptr %data_ptr116, i32 2, ptr %vecres_alloca)
  %97 = call ptr @memcpy(ptr %results, ptr %vecres_alloca, i64 16)
  %r = alloca ptr, align 8
  store ptr null, ptr %r, align 8
  %dataPtr120 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 2
  %dataBase121 = load ptr, ptr %dataPtr120, align 8
  %98 = getelementptr inbounds ptr, ptr %dataBase121, i32 0
  %getVal = load ptr, ptr %98, align 8
  store ptr %getVal, ptr %r, align 8
  %prop_ptr_base122 = load ptr, ptr %r, align 8
  %prop_ptr123 = getelementptr inbounds %VecResult, ptr %prop_ptr_base122, i32 0, i32 0
  %id_val = load ptr, ptr %prop_ptr123, align 8
  %99 = call ptr @__aiml_str_concat(ptr @22, ptr %id_val)
  %print_null_check124 = icmp eq ptr %99, null
  %print_safe_str125 = select i1 %print_null_check124, ptr @print_null_str.6, ptr %99
  %print_call126 = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str125)
  %history = alloca ptr, align 8
  store ptr null, ptr %history, align 8
  %100 = call ptr @aiml_db_ts_open(ptr @24, i64 8)
  store ptr %100, ptr %history, align 8
  %gs = alloca %GameState, align 8
  store %GameState zeroinitializer, ptr %gs, align 4
  %set_prop_ptr127 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 0
  store i32 100, ptr %set_prop_ptr127, align 4
  %set_prop_ptr128 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 1
  store i32 75, ptr %set_prop_ptr128, align 4
  %gs_val = load %GameState, ptr %gs, align 4
  %ts_ptr = load ptr, ptr %history, align 8
  call void @aiml_db_ts_append(ptr %ts_ptr, ptr %gs, i64 8, i64 1000)
  %set_prop_ptr129 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 0
  store i32 200, ptr %set_prop_ptr129, align 4
  %set_prop_ptr130 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 1
  store i32 50, ptr %set_prop_ptr130, align 4
  %gs_val131 = load %GameState, ptr %gs, align 4
  %ts_ptr132 = load ptr, ptr %history, align 8
  call void @aiml_db_ts_append(ptr %ts_ptr132, ptr %gs, i64 8, i64 2000)
  %tlen = alloca i32, align 4
  store i32 0, ptr %tlen, align 4
  %ts_ptr133 = load ptr, ptr %history, align 8
  %101 = call i32 @aiml_db_ts_len(ptr %ts_ptr133)
  store i32 %101, ptr %tlen, align 4
  %tlen_val = load i32, ptr %tlen, align 4
  %102 = call ptr @__aiml_int_to_string(i32 %tlen_val)
  %103 = call ptr @__aiml_str_concat(ptr @25, ptr %102)
  %print_null_check134 = icmp eq ptr %103, null
  %print_safe_str135 = select i1 %print_null_check134, ptr @print_null_str.7, ptr %103
  %print_call136 = call i32 (ptr, ...) @printf(ptr @26, ptr %print_safe_str135)
  %tp = alloca ptr, align 8
  store ptr null, ptr %tp, align 8
  %ts_ptr137 = load ptr, ptr %history, align 8
  %104 = call ptr @aiml_db_ts_at(ptr %ts_ptr137, i64 1500)
  store ptr %104, ptr %tp, align 8
  %prop_ptr_base138 = load ptr, ptr %tp, align 8
  %prop_ptr139 = getelementptr inbounds %GameState, ptr %prop_ptr_base138, i32 0, i32 0
  %score_val = load i32, ptr %prop_ptr139, align 4
  %105 = call ptr @__aiml_int_to_string(i32 %score_val)
  %106 = call ptr @__aiml_str_concat(ptr @27, ptr %105)
  %print_null_check140 = icmp eq ptr %106, null
  %print_safe_str141 = select i1 %print_null_check140, ptr @print_null_str.8, ptr %106
  %print_call142 = call i32 (ptr, ...) @printf(ptr @28, ptr %print_safe_str141)
  %hp_store = alloca ptr, align 8
  store ptr null, ptr %hp_store, align 8
  %107 = call ptr @aiml_db_store_open(ptr @29, ptr @30, i64 4)
  store ptr %107, ptr %hp_store, align 8
  %pos_store = alloca ptr, align 8
  store ptr null, ptr %pos_store, align 8
  %108 = call ptr @aiml_db_store_open(ptr @31, ptr @32, i64 4)
  store ptr %108, ptr %pos_store, align 8
  %h1 = alloca %Health, align 8
  store %Health zeroinitializer, ptr %h1, align 4
  %set_prop_ptr143 = getelementptr inbounds %Health, ptr %h1, i32 0, i32 0
  store i32 80, ptr %set_prop_ptr143, align 4
  %h1_val = load %Health, ptr %h1, align 4
  %store_ptr144 = load ptr, ptr %hp_store, align 8
  %109 = call i32 @aiml_db_store_insert(ptr %store_ptr144, ptr %h1, i64 4)
  %p1 = alloca %Pos, align 8
  store %Pos zeroinitializer, ptr %p1, align 4
  %set_prop_ptr145 = getelementptr inbounds %Pos, ptr %p1, i32 0, i32 0
  store i32 10, ptr %set_prop_ptr145, align 4
  %p1_val = load %Pos, ptr %p1, align 4
  %store_ptr146 = load ptr, ptr %pos_store, align 8
  %110 = call i32 @aiml_db_store_insert(ptr %store_ptr146, ptr %p1, i64 4)
  %h2 = alloca %Health, align 8
  store %Health zeroinitializer, ptr %h2, align 4
  %set_prop_ptr147 = getelementptr inbounds %Health, ptr %h2, i32 0, i32 0
  store i32 30, ptr %set_prop_ptr147, align 4
  %h2_val = load %Health, ptr %h2, align 4
  %store_ptr148 = load ptr, ptr %hp_store, align 8
  %111 = call i32 @aiml_db_store_insert(ptr %store_ptr148, ptr %h2, i64 4)
  %hp_store_sptr = load ptr, ptr %hp_store, align 8
  %pos_store_sptr = load ptr, ptr %pos_store, align 8
  %st_total = call i32 @aiml_db_store_total(ptr %hp_store_sptr)
  %is_bigger = icmp sgt i32 %st_total, 0
  %max_total = select i1 %is_bigger, i32 %st_total, i32 0
  %st_total151 = call i32 @aiml_db_store_total(ptr %pos_store_sptr)
  %is_bigger152 = icmp sgt i32 %st_total151, %max_total
  %max_total153 = select i1 %is_bigger152, i32 %st_total151, i32 %max_total
  %rs_eid = alloca i32, align 4
  store i32 1, ptr %rs_eid, align 4
  %rs_max = alloca i32, align 4
  store i32 %max_total153, ptr %rs_max, align 4
  br label %rs_hdr

entry149:                                         ; No predecessors!

entry150:                                         ; No predecessors!

rs_hdr:                                           ; preds = %rs_next, %cont105
  %eid154 = load i32, ptr %rs_eid, align 4
  %mx = load i32, ptr %rs_max, align 4
  %in_range = icmp sle i32 %eid154, %mx
  br i1 %in_range, label %rs_chk, label %rs_exit

rs_chk:                                           ; preds = %rs_hdr
  %eid155 = load i32, ptr %rs_eid, align 4
  %active = call i32 @aiml_db_store_is_active(ptr %hp_store_sptr, i32 %eid155)
  %all_ok = and i32 1, %active
  %active156 = call i32 @aiml_db_store_is_active(ptr %pos_store_sptr, i32 %eid155)
  %all_ok157 = and i32 %all_ok, %active156
  %all_bool = icmp ne i32 %all_ok157, 0
  br i1 %all_bool, label %rs_call, label %rs_next

rs_call:                                          ; preds = %rs_chk
  %eid158 = load i32, ptr %rs_eid, align 4
  %comp_ptr = call ptr @aiml_db_store_get_ptr(ptr %hp_store_sptr, i32 %eid158)
  %comp_ptr159 = call ptr @aiml_db_store_get_ptr(ptr %pos_store_sptr, i32 %eid158)
  call void @tick_hp(ptr %comp_ptr, ptr %comp_ptr159)
  br label %rs_next

rs_next:                                          ; preds = %rs_call, %rs_chk
  %eid160 = load i32, ptr %rs_eid, align 4
  %eid_next = add i32 %eid160, 1
  store i32 %eid_next, ptr %rs_eid, align 4
  br label %rs_hdr

rs_exit:                                          ; preds = %rs_hdr
  %high_hp = alloca %"list_ptr<Health>", align 8
  store %"list_ptr<Health>" zeroinitializer, ptr %high_hp, align 8
  %sizePtr161 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 0
  store i32 0, ptr %sizePtr161, align 4
  %capPtr162 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 1
  store i32 4, ptr %capPtr162, align 4
  %112 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr163 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 2
  store ptr %112, ptr %dataPtr163, align 8
  %hp_store_val = load ptr, ptr %hp_store, align 8
  %query_list = alloca %GenericList, align 8
  %sizePtr164 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 0
  store i32 0, ptr %sizePtr164, align 4
  %capPtr165 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 1
  store i32 8, ptr %capPtr165, align 4
  %113 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr166 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 2
  store ptr %113, ptr %dataPtr166, align 8
  call void @aiml_db_store_query(ptr %hp_store_val, ptr null, i32 -1, i32 0, i32 0, i32 -1, i32 0, ptr %query_list)
  %114 = call ptr @memcpy(ptr %high_hp, ptr %query_list, i64 16)
  %qlen = alloca i32, align 4
  store i32 0, ptr %qlen, align 4
  %sizePtr167 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr167, align 4
  store i32 %sizeVal, ptr %qlen, align 4
  %qlen_val = load i32, ptr %qlen, align 4
  %115 = call ptr @__aiml_int_to_string(i32 %qlen_val)
  %116 = call ptr @__aiml_str_concat(ptr @35, ptr %115)
  %print_null_check168 = icmp eq ptr %116, null
  %print_safe_str169 = select i1 %print_null_check168, ptr @print_null_str.10, ptr %116
  %print_call170 = call i32 (ptr, ...) @printf(ptr @36, ptr %print_safe_str169)
  ret void
}

declare ptr @aiml_db_store_open(ptr, ptr, i64)

declare i32 @aiml_db_store_insert(ptr, ptr, i64)

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

declare ptr @aiml_db_store_get(ptr, i32)

declare i32 @aiml_db_store_len(ptr)

declare ptr @aiml_db_kv_open(ptr, i64)

declare void @aiml_db_kv_set(ptr, ptr, ptr, i64)

declare ptr @aiml_db_kv_get(ptr, ptr)

declare i32 @aiml_db_kv_has(ptr, ptr)

declare i32 @aiml_db_kv_len(ptr)

declare ptr @aiml_db_vec_open(ptr, i32)

declare ptr @realloc(ptr, i64)

declare void @aiml_db_vec_insert(ptr, ptr, ptr, i32)

declare void @aiml_db_vec_search(ptr, ptr, i32, ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare ptr @aiml_db_ts_open(ptr, i64)

declare void @aiml_db_ts_append(ptr, ptr, i64, i64)

declare i32 @aiml_db_ts_len(ptr)

declare ptr @aiml_db_ts_at(ptr, i64)

define void @tick_hp(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %h = alloca ptr, align 8
  store ptr %0, ptr %h, align 8
  %prop_ptr_base = load ptr, ptr %h, align 8
  %prop_ptr = getelementptr inbounds %Health, ptr %prop_ptr_base, i32 0, i32 0
  %hp_val = load i32, ptr %prop_ptr, align 4
  %1 = call ptr @__aiml_int_to_string(i32 %hp_val)
  %2 = call ptr @__aiml_str_concat(ptr @33, ptr %1)
  %print_null_check = icmp eq ptr %2, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.9, ptr %2
  %print_call = call i32 (ptr, ...) @printf(ptr @34, ptr %print_safe_str)
  ret void
}

declare i32 @__gxx_personality_v0(...)

define i32 @is_high_hp(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %h = alloca ptr, align 8
  store ptr %0, ptr %h, align 8
  %prop_ptr_base = load ptr, ptr %h, align 8
  %prop_ptr = getelementptr inbounds %Health, ptr %prop_ptr_base, i32 0, i32 0
  %hp_val = load i32, ptr %prop_ptr, align 4
  %1 = icmp sgt i32 %hp_val, 50
  br i1 %1, label %if_then, label %if_else

if_then:                                          ; preds = %entry
  ret i32 1
  br label %if_merge

if_else:                                          ; preds = %entry
  ret i32 0
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  ret i32 0
}

declare i32 @aiml_db_store_total(ptr)

declare i32 @aiml_db_store_is_active(ptr, i32)

declare ptr @aiml_db_store_get_ptr(ptr, i32)

declare void @aiml_db_store_query(ptr, ptr, i32, i32, i32, i32, i32, ptr)

attributes #0 = { noinline }
