; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [32 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Health = type { i32 }
%Entity = type { i32, i32 }
%list_f32 = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%"list_ptr<VecResult>" = type { i32, i32, ptr }
%VecResult = type { ptr, float }
%GameState = type { i32, i32 }
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
@8 = private unnamed_addr constant [18 x i8] c"hp after update: \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"cfg\00", align 1
@11 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@12 = private unnamed_addr constant [5 x i8] c"dark\00", align 1
@13 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@14 = private unnamed_addr constant [8 x i8] c"theme: \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@17 = private unnamed_addr constant [6 x i8] c"has: \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [7 x i8] c"klen: \00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@22 = private unnamed_addr constant [6 x i8] c"theme\00", align 1
@23 = private unnamed_addr constant [16 x i8] c"has after del: \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@26 = private unnamed_addr constant [2 x i8] c"1\00", align 1
@27 = private unnamed_addr constant [2 x i8] c"b\00", align 1
@28 = private unnamed_addr constant [2 x i8] c"2\00", align 1
@29 = private unnamed_addr constant [20 x i8] c"klen before clear: \00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@30 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@31 = private unnamed_addr constant [19 x i8] c"klen after clear: \00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [5 x i8] c"vecs\00", align 1
@34 = private unnamed_addr constant [5 x i8] c"doc1\00", align 1
@35 = private unnamed_addr constant [5 x i8] c"doc2\00", align 1
@36 = private unnamed_addr constant [9 x i8] c"vs len: \00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@38 = private unnamed_addr constant [9 x i8] c"top id: \00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [13 x i8] c"game_history\00", align 1
@41 = private unnamed_addr constant [9 x i8] c"ts len: \00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@42 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@43 = private unnamed_addr constant [16 x i8] c"score at 1500: \00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [12 x i8] c"health_data\00", align 1
@46 = private unnamed_addr constant [7 x i8] c"Health\00", align 1
@47 = private unnamed_addr constant [9 x i8] c"pos_data\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"Pos\00", align 1
@49 = private unnamed_addr constant [9 x i8] c"sys hp: \00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@50 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@51 = private unnamed_addr constant [14 x i8] c"query count: \00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
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

define void @tick_hp(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %h = alloca ptr, align 8
  store ptr %0, ptr %h, align 8
  %prop_ptr_base = load ptr, ptr %h, align 8
  %prop_ptr = getelementptr inbounds %Health, ptr %prop_ptr_base, i32 0, i32 0
  %hp_val = load i32, ptr %prop_ptr, align 4
  %1 = call ptr @__aiml_int_to_string(i32 %hp_val)
  %2 = call ptr @__aiml_str_concat(ptr @49, ptr %1)
  %print_null_check = icmp eq ptr %2, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.14, ptr %2
  %print_call = call i32 (ptr, ...) @printf(ptr @50, ptr %print_safe_str)
  ret void
}

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

if_else:                                          ; preds = %entry
  ret i32 0

if_merge:                                         ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  %entities = alloca ptr, align 8
  store ptr null, ptr %entities, align 8
  %0 = call ptr @aiml_db_store_open(ptr @0, ptr @1, i64 8)
  store ptr %0, ptr %entities, align 8
  %e = alloca %Entity, align 8
  store %Entity zeroinitializer, ptr %e, align 4
  %set_prop_ptr_w = getelementptr inbounds %Entity, ptr %e, i32 0, i32 0
  store i32 42, ptr %set_prop_ptr_w, align 4
  %set_prop_ptr_w1 = getelementptr inbounds %Entity, ptr %e, i32 0, i32 1
  store i32 100, ptr %set_prop_ptr_w1, align 4
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
  %set_prop_ptr_w10 = getelementptr inbounds %Entity, ptr %e, i32 0, i32 1
  store i32 200, ptr %set_prop_ptr_w10, align 4
  %e_val11 = load %Entity, ptr %e, align 4
  %store_ptr12 = load ptr, ptr %entities, align 8
  %10 = call i32 @aiml_db_store_update(ptr %store_ptr12, i32 1, ptr %e, i64 8)
  %store_ptr13 = load ptr, ptr %entities, align 8
  %11 = call ptr @aiml_db_store_get(ptr %store_ptr13, i32 1)
  store ptr %11, ptr %p, align 8
  %prop_ptr_base14 = load ptr, ptr %p, align 8
  %prop_ptr15 = getelementptr inbounds %Entity, ptr %prop_ptr_base14, i32 0, i32 1
  %hp_val16 = load i32, ptr %prop_ptr15, align 4
  %12 = call ptr @__aiml_int_to_string(i32 %hp_val16)
  %13 = call ptr @__aiml_str_concat(ptr @8, ptr %12)
  %print_null_check17 = icmp eq ptr %13, null
  %print_safe_str18 = select i1 %print_null_check17, ptr @print_null_str.3, ptr %13
  %print_call19 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str18)
  %cfg = alloca ptr, align 8
  store ptr null, ptr %cfg, align 8
  %14 = call ptr @aiml_db_kv_open(ptr @10, i64 0)
  store ptr %14, ptr %cfg, align 8
  %kv_ptr = load ptr, ptr %cfg, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @11, ptr @12, i64 0)
  %v = alloca ptr, align 8
  store ptr null, ptr %v, align 8
  %kv_ptr20 = load ptr, ptr %cfg, align 8
  %15 = call ptr @aiml_db_kv_get(ptr %kv_ptr20, ptr @13)
  store ptr %15, ptr %v, align 8
  %v_val = load ptr, ptr %v, align 8
  %16 = call ptr @__aiml_str_concat(ptr @14, ptr %v_val)
  %print_null_check21 = icmp eq ptr %16, null
  %print_safe_str22 = select i1 %print_null_check21, ptr @print_null_str.4, ptr %16
  %print_call23 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str22)
  %has = alloca i32, align 4
  store i32 0, ptr %has, align 4
  %kv_ptr24 = load ptr, ptr %cfg, align 8
  %17 = call i32 @aiml_db_kv_has(ptr %kv_ptr24, ptr @16)
  store i32 %17, ptr %has, align 4
  %has_val = load i32, ptr %has, align 4
  %18 = call ptr @__aiml_int_to_string(i32 %has_val)
  %19 = call ptr @__aiml_str_concat(ptr @17, ptr %18)
  %print_null_check25 = icmp eq ptr %19, null
  %print_safe_str26 = select i1 %print_null_check25, ptr @print_null_str.5, ptr %19
  %print_call27 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str26)
  %klen = alloca i32, align 4
  store i32 0, ptr %klen, align 4
  %kv_ptr28 = load ptr, ptr %cfg, align 8
  %20 = call i32 @aiml_db_kv_len(ptr %kv_ptr28)
  store i32 %20, ptr %klen, align 4
  %klen_val = load i32, ptr %klen, align 4
  %21 = call ptr @__aiml_int_to_string(i32 %klen_val)
  %22 = call ptr @__aiml_str_concat(ptr @19, ptr %21)
  %print_null_check29 = icmp eq ptr %22, null
  %print_safe_str30 = select i1 %print_null_check29, ptr @print_null_str.6, ptr %22
  %print_call31 = call i32 (ptr, ...) @printf(ptr @20, ptr %print_safe_str30)
  %kv_ptr32 = load ptr, ptr %cfg, align 8
  call void @aiml_db_kv_delete(ptr %kv_ptr32, ptr @21)
  %kv_ptr33 = load ptr, ptr %cfg, align 8
  %23 = call i32 @aiml_db_kv_has(ptr %kv_ptr33, ptr @22)
  store i32 %23, ptr %has, align 4
  %has_val34 = load i32, ptr %has, align 4
  %24 = call ptr @__aiml_int_to_string(i32 %has_val34)
  %25 = call ptr @__aiml_str_concat(ptr @23, ptr %24)
  %print_null_check35 = icmp eq ptr %25, null
  %print_safe_str36 = select i1 %print_null_check35, ptr @print_null_str.7, ptr %25
  %print_call37 = call i32 (ptr, ...) @printf(ptr @24, ptr %print_safe_str36)
  %kv_ptr38 = load ptr, ptr %cfg, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr38, ptr @25, ptr @26, i64 0)
  %kv_ptr39 = load ptr, ptr %cfg, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr39, ptr @27, ptr @28, i64 0)
  %kv_ptr40 = load ptr, ptr %cfg, align 8
  %26 = call i32 @aiml_db_kv_len(ptr %kv_ptr40)
  store i32 %26, ptr %klen, align 4
  %klen_val41 = load i32, ptr %klen, align 4
  %27 = call ptr @__aiml_int_to_string(i32 %klen_val41)
  %28 = call ptr @__aiml_str_concat(ptr @29, ptr %27)
  %print_null_check42 = icmp eq ptr %28, null
  %print_safe_str43 = select i1 %print_null_check42, ptr @print_null_str.8, ptr %28
  %print_call44 = call i32 (ptr, ...) @printf(ptr @30, ptr %print_safe_str43)
  %kv_ptr45 = load ptr, ptr %cfg, align 8
  call void @aiml_db_kv_clear(ptr %kv_ptr45)
  %kv_ptr46 = load ptr, ptr %cfg, align 8
  %29 = call i32 @aiml_db_kv_len(ptr %kv_ptr46)
  store i32 %29, ptr %klen, align 4
  %klen_val47 = load i32, ptr %klen, align 4
  %30 = call ptr @__aiml_int_to_string(i32 %klen_val47)
  %31 = call ptr @__aiml_str_concat(ptr @31, ptr %30)
  %print_null_check48 = icmp eq ptr %31, null
  %print_safe_str49 = select i1 %print_null_check48, ptr @print_null_str.9, ptr %31
  %print_call50 = call i32 (ptr, ...) @printf(ptr @32, ptr %print_safe_str49)
  %vs = alloca ptr, align 8
  store ptr null, ptr %vs, align 8
  %32 = call ptr @aiml_db_vec_open(ptr @33, i32 3)
  store ptr %32, ptr %vs, align 8
  %v1 = alloca %list_f32, align 8
  store %list_f32 zeroinitializer, ptr %v1, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %33 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  store ptr %33, ptr %dataPtr, align 8
  %sizePtr51 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %capPtr52 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  %size = load i32, ptr %sizePtr51, align 4
  %capacity = load i32, ptr %capPtr52, align 4
  %34 = icmp eq i32 %size, %capacity
  br i1 %34, label %grow, label %cont

grow:                                             ; preds = %entry
  %35 = mul i32 %capacity, 2
  store i32 %35, ptr %capPtr52, align 4
  %dataPtr53 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %36 = load ptr, ptr %dataPtr53, align 8
  %37 = zext i32 %35 to i64
  %38 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %37
  %39 = call ptr @realloc(ptr %36, i64 %38)
  store ptr %39, ptr %dataPtr53, align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataPtr54 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr54, align 8
  %40 = getelementptr inbounds float, ptr %dataBase, i32 %size
  store float 1.000000e+00, ptr %40, align 4
  %41 = add i32 %size, 1
  store i32 %41, ptr %sizePtr51, align 4
  %sizePtr55 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %capPtr56 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  %size57 = load i32, ptr %sizePtr55, align 4
  %capacity58 = load i32, ptr %capPtr56, align 4
  %42 = icmp eq i32 %size57, %capacity58
  br i1 %42, label %grow59, label %cont60

grow59:                                           ; preds = %cont
  %43 = mul i32 %capacity58, 2
  store i32 %43, ptr %capPtr56, align 4
  %dataPtr61 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %44 = load ptr, ptr %dataPtr61, align 8
  %45 = zext i32 %43 to i64
  %46 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %45
  %47 = call ptr @realloc(ptr %44, i64 %46)
  store ptr %47, ptr %dataPtr61, align 8
  br label %cont60

cont60:                                           ; preds = %grow59, %cont
  %dataPtr62 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %dataBase63 = load ptr, ptr %dataPtr62, align 8
  %48 = getelementptr inbounds float, ptr %dataBase63, i32 %size57
  store float 0.000000e+00, ptr %48, align 4
  %49 = add i32 %size57, 1
  store i32 %49, ptr %sizePtr55, align 4
  %sizePtr64 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %capPtr65 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 1
  %size66 = load i32, ptr %sizePtr64, align 4
  %capacity67 = load i32, ptr %capPtr65, align 4
  %50 = icmp eq i32 %size66, %capacity67
  br i1 %50, label %grow68, label %cont69

grow68:                                           ; preds = %cont60
  %51 = mul i32 %capacity67, 2
  store i32 %51, ptr %capPtr65, align 4
  %dataPtr70 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %52 = load ptr, ptr %dataPtr70, align 8
  %53 = zext i32 %51 to i64
  %54 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %53
  %55 = call ptr @realloc(ptr %52, i64 %54)
  store ptr %55, ptr %dataPtr70, align 8
  br label %cont69

cont69:                                           ; preds = %grow68, %cont60
  %dataPtr71 = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %dataBase72 = load ptr, ptr %dataPtr71, align 8
  %56 = getelementptr inbounds float, ptr %dataBase72, i32 %size66
  store float 0.000000e+00, ptr %56, align 4
  %57 = add i32 %size66, 1
  store i32 %57, ptr %sizePtr64, align 4
  %v1_val = load %list_f32, ptr %v1, align 8
  %vs_ptr = load ptr, ptr %vs, align 8
  %vsz_fld = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 0
  %list_sz = load i32, ptr %vsz_fld, align 4
  %vdata_fld = getelementptr inbounds %GenericList, ptr %v1, i32 0, i32 2
  %data_ptr = load ptr, ptr %vdata_fld, align 8
  call void @aiml_db_vec_insert(ptr %vs_ptr, ptr @34, ptr %data_ptr, i32 %list_sz)
  %v2 = alloca %list_f32, align 8
  store %list_f32 zeroinitializer, ptr %v2, align 8
  %sizePtr73 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  store i32 0, ptr %sizePtr73, align 4
  %capPtr74 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  store i32 4, ptr %capPtr74, align 4
  %58 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  %dataPtr75 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  store ptr %58, ptr %dataPtr75, align 8
  %sizePtr76 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %capPtr77 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  %size78 = load i32, ptr %sizePtr76, align 4
  %capacity79 = load i32, ptr %capPtr77, align 4
  %59 = icmp eq i32 %size78, %capacity79
  br i1 %59, label %grow80, label %cont81

grow80:                                           ; preds = %cont69
  %60 = mul i32 %capacity79, 2
  store i32 %60, ptr %capPtr77, align 4
  %dataPtr82 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %61 = load ptr, ptr %dataPtr82, align 8
  %62 = zext i32 %60 to i64
  %63 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %62
  %64 = call ptr @realloc(ptr %61, i64 %63)
  store ptr %64, ptr %dataPtr82, align 8
  br label %cont81

cont81:                                           ; preds = %grow80, %cont69
  %dataPtr83 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %dataBase84 = load ptr, ptr %dataPtr83, align 8
  %65 = getelementptr inbounds float, ptr %dataBase84, i32 %size78
  store float 0.000000e+00, ptr %65, align 4
  %66 = add i32 %size78, 1
  store i32 %66, ptr %sizePtr76, align 4
  %sizePtr85 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %capPtr86 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  %size87 = load i32, ptr %sizePtr85, align 4
  %capacity88 = load i32, ptr %capPtr86, align 4
  %67 = icmp eq i32 %size87, %capacity88
  br i1 %67, label %grow89, label %cont90

grow89:                                           ; preds = %cont81
  %68 = mul i32 %capacity88, 2
  store i32 %68, ptr %capPtr86, align 4
  %dataPtr91 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %69 = load ptr, ptr %dataPtr91, align 8
  %70 = zext i32 %68 to i64
  %71 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %70
  %72 = call ptr @realloc(ptr %69, i64 %71)
  store ptr %72, ptr %dataPtr91, align 8
  br label %cont90

cont90:                                           ; preds = %grow89, %cont81
  %dataPtr92 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %dataBase93 = load ptr, ptr %dataPtr92, align 8
  %73 = getelementptr inbounds float, ptr %dataBase93, i32 %size87
  store float 1.000000e+00, ptr %73, align 4
  %74 = add i32 %size87, 1
  store i32 %74, ptr %sizePtr85, align 4
  %sizePtr94 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %capPtr95 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 1
  %size96 = load i32, ptr %sizePtr94, align 4
  %capacity97 = load i32, ptr %capPtr95, align 4
  %75 = icmp eq i32 %size96, %capacity97
  br i1 %75, label %grow98, label %cont99

grow98:                                           ; preds = %cont90
  %76 = mul i32 %capacity97, 2
  store i32 %76, ptr %capPtr95, align 4
  %dataPtr100 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %77 = load ptr, ptr %dataPtr100, align 8
  %78 = zext i32 %76 to i64
  %79 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %78
  %80 = call ptr @realloc(ptr %77, i64 %79)
  store ptr %80, ptr %dataPtr100, align 8
  br label %cont99

cont99:                                           ; preds = %grow98, %cont90
  %dataPtr101 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %dataBase102 = load ptr, ptr %dataPtr101, align 8
  %81 = getelementptr inbounds float, ptr %dataBase102, i32 %size96
  store float 0.000000e+00, ptr %81, align 4
  %82 = add i32 %size96, 1
  store i32 %82, ptr %sizePtr94, align 4
  %v2_val = load %list_f32, ptr %v2, align 8
  %vs_ptr103 = load ptr, ptr %vs, align 8
  %vsz_fld104 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 0
  %list_sz105 = load i32, ptr %vsz_fld104, align 4
  %vdata_fld106 = getelementptr inbounds %GenericList, ptr %v2, i32 0, i32 2
  %data_ptr107 = load ptr, ptr %vdata_fld106, align 8
  call void @aiml_db_vec_insert(ptr %vs_ptr103, ptr @35, ptr %data_ptr107, i32 %list_sz105)
  %vlen = alloca i32, align 4
  store i32 0, ptr %vlen, align 4
  %vs_ptr108 = load ptr, ptr %vs, align 8
  %83 = call i32 @aiml_db_vec_len(ptr %vs_ptr108)
  store i32 %83, ptr %vlen, align 4
  %vlen_val = load i32, ptr %vlen, align 4
  %84 = call ptr @__aiml_int_to_string(i32 %vlen_val)
  %85 = call ptr @__aiml_str_concat(ptr @36, ptr %84)
  %print_null_check109 = icmp eq ptr %85, null
  %print_safe_str110 = select i1 %print_null_check109, ptr @print_null_str.10, ptr %85
  %print_call111 = call i32 (ptr, ...) @printf(ptr @37, ptr %print_safe_str110)
  %q = alloca %list_f32, align 8
  store %list_f32 zeroinitializer, ptr %q, align 8
  %sizePtr112 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  store i32 0, ptr %sizePtr112, align 4
  %capPtr113 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  store i32 4, ptr %capPtr113, align 4
  %86 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  %dataPtr114 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  store ptr %86, ptr %dataPtr114, align 8
  %sizePtr115 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %capPtr116 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  %size117 = load i32, ptr %sizePtr115, align 4
  %capacity118 = load i32, ptr %capPtr116, align 4
  %87 = icmp eq i32 %size117, %capacity118
  br i1 %87, label %grow119, label %cont120

grow119:                                          ; preds = %cont99
  %88 = mul i32 %capacity118, 2
  store i32 %88, ptr %capPtr116, align 4
  %dataPtr121 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %89 = load ptr, ptr %dataPtr121, align 8
  %90 = zext i32 %88 to i64
  %91 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %90
  %92 = call ptr @realloc(ptr %89, i64 %91)
  store ptr %92, ptr %dataPtr121, align 8
  br label %cont120

cont120:                                          ; preds = %grow119, %cont99
  %dataPtr122 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %dataBase123 = load ptr, ptr %dataPtr122, align 8
  %93 = getelementptr inbounds float, ptr %dataBase123, i32 %size117
  store float 1.000000e+00, ptr %93, align 4
  %94 = add i32 %size117, 1
  store i32 %94, ptr %sizePtr115, align 4
  %sizePtr124 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %capPtr125 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  %size126 = load i32, ptr %sizePtr124, align 4
  %capacity127 = load i32, ptr %capPtr125, align 4
  %95 = icmp eq i32 %size126, %capacity127
  br i1 %95, label %grow128, label %cont129

grow128:                                          ; preds = %cont120
  %96 = mul i32 %capacity127, 2
  store i32 %96, ptr %capPtr125, align 4
  %dataPtr130 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %97 = load ptr, ptr %dataPtr130, align 8
  %98 = zext i32 %96 to i64
  %99 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %98
  %100 = call ptr @realloc(ptr %97, i64 %99)
  store ptr %100, ptr %dataPtr130, align 8
  br label %cont129

cont129:                                          ; preds = %grow128, %cont120
  %dataPtr131 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %dataBase132 = load ptr, ptr %dataPtr131, align 8
  %101 = getelementptr inbounds float, ptr %dataBase132, i32 %size126
  store float 0.000000e+00, ptr %101, align 4
  %102 = add i32 %size126, 1
  store i32 %102, ptr %sizePtr124, align 4
  %sizePtr133 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %capPtr134 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 1
  %size135 = load i32, ptr %sizePtr133, align 4
  %capacity136 = load i32, ptr %capPtr134, align 4
  %103 = icmp eq i32 %size135, %capacity136
  br i1 %103, label %grow137, label %cont138

grow137:                                          ; preds = %cont129
  %104 = mul i32 %capacity136, 2
  store i32 %104, ptr %capPtr134, align 4
  %dataPtr139 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %105 = load ptr, ptr %dataPtr139, align 8
  %106 = zext i32 %104 to i64
  %107 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %106
  %108 = call ptr @realloc(ptr %105, i64 %107)
  store ptr %108, ptr %dataPtr139, align 8
  br label %cont138

cont138:                                          ; preds = %grow137, %cont129
  %dataPtr140 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %dataBase141 = load ptr, ptr %dataPtr140, align 8
  %109 = getelementptr inbounds float, ptr %dataBase141, i32 %size135
  store float 0.000000e+00, ptr %109, align 4
  %110 = add i32 %size135, 1
  store i32 %110, ptr %sizePtr133, align 4
  %results = alloca %"list_ptr<VecResult>", align 8
  store %"list_ptr<VecResult>" zeroinitializer, ptr %results, align 8
  %sizePtr142 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 0
  store i32 0, ptr %sizePtr142, align 4
  %capPtr143 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 1
  store i32 4, ptr %capPtr143, align 4
  %111 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr144 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 2
  store ptr %111, ptr %dataPtr144, align 8
  %q_val = load %list_f32, ptr %q, align 8
  %vs_ptr145 = load ptr, ptr %vs, align 8
  %vsz_fld146 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 0
  %list_sz147 = load i32, ptr %vsz_fld146, align 4
  %vdata_fld148 = getelementptr inbounds %GenericList, ptr %q, i32 0, i32 2
  %data_ptr149 = load ptr, ptr %vdata_fld148, align 8
  %vecres_alloca = alloca %GenericList, align 8
  %sizePtr150 = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 0
  store i32 0, ptr %sizePtr150, align 4
  %capPtr151 = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 1
  store i32 8, ptr %capPtr151, align 4
  %112 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr152 = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 2
  store ptr %112, ptr %dataPtr152, align 8
  call void @aiml_db_vec_search(ptr %vs_ptr145, ptr %data_ptr149, i32 2, ptr %vecres_alloca)
  %113 = call ptr @memcpy(ptr %results, ptr %vecres_alloca, i64 16)
  %r = alloca ptr, align 8
  store ptr null, ptr %r, align 8
  %dataPtr153 = getelementptr inbounds %GenericList, ptr %results, i32 0, i32 2
  %dataBase154 = load ptr, ptr %dataPtr153, align 8
  %114 = getelementptr inbounds ptr, ptr %dataBase154, i32 0
  %getVal = load ptr, ptr %114, align 8
  store ptr %getVal, ptr %r, align 8
  %prop_ptr_base155 = load ptr, ptr %r, align 8
  %prop_ptr156 = getelementptr inbounds %VecResult, ptr %prop_ptr_base155, i32 0, i32 0
  %id_val = load ptr, ptr %prop_ptr156, align 8
  %115 = call ptr @__aiml_str_concat(ptr @38, ptr %id_val)
  %print_null_check157 = icmp eq ptr %115, null
  %print_safe_str158 = select i1 %print_null_check157, ptr @print_null_str.11, ptr %115
  %print_call159 = call i32 (ptr, ...) @printf(ptr @39, ptr %print_safe_str158)
  %history = alloca ptr, align 8
  store ptr null, ptr %history, align 8
  %116 = call ptr @aiml_db_ts_open(ptr @40, i64 8)
  store ptr %116, ptr %history, align 8
  %gs = alloca %GameState, align 8
  store %GameState zeroinitializer, ptr %gs, align 4
  %set_prop_ptr_w160 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 0
  store i32 100, ptr %set_prop_ptr_w160, align 4
  %set_prop_ptr_w161 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 1
  store i32 75, ptr %set_prop_ptr_w161, align 4
  %gs_val = load %GameState, ptr %gs, align 4
  %ts_ptr = load ptr, ptr %history, align 8
  call void @aiml_db_ts_append(ptr %ts_ptr, ptr %gs, i64 8, i64 1000)
  %set_prop_ptr_w162 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 0
  store i32 200, ptr %set_prop_ptr_w162, align 4
  %set_prop_ptr_w163 = getelementptr inbounds %GameState, ptr %gs, i32 0, i32 1
  store i32 50, ptr %set_prop_ptr_w163, align 4
  %gs_val164 = load %GameState, ptr %gs, align 4
  %ts_ptr165 = load ptr, ptr %history, align 8
  call void @aiml_db_ts_append(ptr %ts_ptr165, ptr %gs, i64 8, i64 2000)
  %tlen = alloca i32, align 4
  store i32 0, ptr %tlen, align 4
  %ts_ptr166 = load ptr, ptr %history, align 8
  %117 = call i32 @aiml_db_ts_len(ptr %ts_ptr166)
  store i32 %117, ptr %tlen, align 4
  %tlen_val = load i32, ptr %tlen, align 4
  %118 = call ptr @__aiml_int_to_string(i32 %tlen_val)
  %119 = call ptr @__aiml_str_concat(ptr @41, ptr %118)
  %print_null_check167 = icmp eq ptr %119, null
  %print_safe_str168 = select i1 %print_null_check167, ptr @print_null_str.12, ptr %119
  %print_call169 = call i32 (ptr, ...) @printf(ptr @42, ptr %print_safe_str168)
  %tp = alloca ptr, align 8
  store ptr null, ptr %tp, align 8
  %ts_ptr170 = load ptr, ptr %history, align 8
  %120 = call ptr @aiml_db_ts_at(ptr %ts_ptr170, i64 1500)
  store ptr %120, ptr %tp, align 8
  %prop_ptr_base171 = load ptr, ptr %tp, align 8
  %prop_ptr172 = getelementptr inbounds %GameState, ptr %prop_ptr_base171, i32 0, i32 0
  %score_val = load i32, ptr %prop_ptr172, align 4
  %121 = call ptr @__aiml_int_to_string(i32 %score_val)
  %122 = call ptr @__aiml_str_concat(ptr @43, ptr %121)
  %print_null_check173 = icmp eq ptr %122, null
  %print_safe_str174 = select i1 %print_null_check173, ptr @print_null_str.13, ptr %122
  %print_call175 = call i32 (ptr, ...) @printf(ptr @44, ptr %print_safe_str174)
  %hp_store = alloca ptr, align 8
  store ptr null, ptr %hp_store, align 8
  %123 = call ptr @aiml_db_store_open(ptr @45, ptr @46, i64 4)
  store ptr %123, ptr %hp_store, align 8
  %pos_store = alloca ptr, align 8
  store ptr null, ptr %pos_store, align 8
  %124 = call ptr @aiml_db_store_open(ptr @47, ptr @48, i64 4)
  store ptr %124, ptr %pos_store, align 8
  %h1 = alloca %Health, align 8
  store %Health zeroinitializer, ptr %h1, align 4
  %set_prop_ptr_w176 = getelementptr inbounds %Health, ptr %h1, i32 0, i32 0
  store i32 80, ptr %set_prop_ptr_w176, align 4
  %h1_val = load %Health, ptr %h1, align 4
  %store_ptr177 = load ptr, ptr %hp_store, align 8
  %125 = call i32 @aiml_db_store_insert(ptr %store_ptr177, ptr %h1, i64 4)
  %p1 = alloca %Pos, align 8
  store %Pos zeroinitializer, ptr %p1, align 4
  %set_prop_ptr_w178 = getelementptr inbounds %Pos, ptr %p1, i32 0, i32 0
  store i32 10, ptr %set_prop_ptr_w178, align 4
  %p1_val = load %Pos, ptr %p1, align 4
  %store_ptr179 = load ptr, ptr %pos_store, align 8
  %126 = call i32 @aiml_db_store_insert(ptr %store_ptr179, ptr %p1, i64 4)
  %h2 = alloca %Health, align 8
  store %Health zeroinitializer, ptr %h2, align 4
  %set_prop_ptr_w180 = getelementptr inbounds %Health, ptr %h2, i32 0, i32 0
  store i32 30, ptr %set_prop_ptr_w180, align 4
  %h2_val = load %Health, ptr %h2, align 4
  %store_ptr181 = load ptr, ptr %hp_store, align 8
  %127 = call i32 @aiml_db_store_insert(ptr %store_ptr181, ptr %h2, i64 4)
  %hp_store_sptr = load ptr, ptr %hp_store, align 8
  %pos_store_sptr = load ptr, ptr %pos_store, align 8
  %st_total = call i32 @aiml_db_store_total(ptr %hp_store_sptr)
  %is_bigger = icmp sgt i32 %st_total, 0
  %max_total = select i1 %is_bigger, i32 %st_total, i32 0
  %st_total182 = call i32 @aiml_db_store_total(ptr %pos_store_sptr)
  %is_bigger183 = icmp sgt i32 %st_total182, %max_total
  %max_total184 = select i1 %is_bigger183, i32 %st_total182, i32 %max_total
  %rs_eid = alloca i32, align 4
  store i32 1, ptr %rs_eid, align 4
  %rs_max = alloca i32, align 4
  store i32 %max_total184, ptr %rs_max, align 4
  br label %rs_hdr

rs_hdr:                                           ; preds = %rs_next, %cont138
  %eid185 = load i32, ptr %rs_eid, align 4
  %mx = load i32, ptr %rs_max, align 4
  %in_range = icmp sle i32 %eid185, %mx
  br i1 %in_range, label %rs_chk, label %rs_exit

rs_chk:                                           ; preds = %rs_hdr
  %eid186 = load i32, ptr %rs_eid, align 4
  %active = call i32 @aiml_db_store_is_active(ptr %hp_store_sptr, i32 %eid186)
  %all_ok = and i32 1, %active
  %active187 = call i32 @aiml_db_store_is_active(ptr %pos_store_sptr, i32 %eid186)
  %all_ok188 = and i32 %all_ok, %active187
  %all_bool = icmp ne i32 %all_ok188, 0
  br i1 %all_bool, label %rs_call, label %rs_next

rs_call:                                          ; preds = %rs_chk
  %eid189 = load i32, ptr %rs_eid, align 4
  %comp_ptr = call ptr @aiml_db_store_get_ptr(ptr %hp_store_sptr, i32 %eid189)
  %comp_ptr190 = call ptr @aiml_db_store_get_ptr(ptr %pos_store_sptr, i32 %eid189)
  call void @tick_hp(ptr %comp_ptr, ptr %comp_ptr190)
  br label %rs_next

rs_next:                                          ; preds = %rs_call, %rs_chk
  %eid191 = load i32, ptr %rs_eid, align 4
  %eid_next = add i32 %eid191, 1
  store i32 %eid_next, ptr %rs_eid, align 4
  br label %rs_hdr

rs_exit:                                          ; preds = %rs_hdr
  %high_hp = alloca %"list_ptr<Health>", align 8
  store %"list_ptr<Health>" zeroinitializer, ptr %high_hp, align 8
  %sizePtr192 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 0
  store i32 0, ptr %sizePtr192, align 4
  %capPtr193 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 1
  store i32 4, ptr %capPtr193, align 4
  %128 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr194 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 2
  store ptr %128, ptr %dataPtr194, align 8
  %hp_store_val = load ptr, ptr %hp_store, align 8
  %query_list = alloca %GenericList, align 8
  %sizePtr195 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 0
  store i32 0, ptr %sizePtr195, align 4
  %capPtr196 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 1
  store i32 8, ptr %capPtr196, align 4
  %129 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr197 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 2
  store ptr %129, ptr %dataPtr197, align 8
  call void @aiml_db_store_query(ptr %hp_store_val, ptr @is_high_hp, i32 -1, i32 0, i32 0, i32 -1, i32 0, ptr %query_list)
  %130 = call ptr @memcpy(ptr %high_hp, ptr %query_list, i64 16)
  %qlen = alloca i32, align 4
  store i32 0, ptr %qlen, align 4
  %sizePtr198 = getelementptr inbounds %GenericList, ptr %high_hp, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr198, align 4
  store i32 %sizeVal, ptr %qlen, align 4
  %qlen_val = load i32, ptr %qlen, align 4
  %131 = call ptr @__aiml_int_to_string(i32 %qlen_val)
  %132 = call ptr @__aiml_str_concat(ptr @51, ptr %131)
  %print_null_check199 = icmp eq ptr %132, null
  %print_safe_str200 = select i1 %print_null_check199, ptr @print_null_str.15, ptr %132
  %print_call201 = call i32 (ptr, ...) @printf(ptr @52, ptr %print_safe_str200)
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

declare i32 @aiml_db_store_update(ptr, i32, ptr, i64)

declare ptr @aiml_db_kv_open(ptr, i64)

declare void @aiml_db_kv_set(ptr, ptr, ptr, i64)

declare ptr @aiml_db_kv_get(ptr, ptr)

declare i32 @aiml_db_kv_has(ptr, ptr)

declare i32 @aiml_db_kv_len(ptr)

declare void @aiml_db_kv_delete(ptr, ptr)

declare void @aiml_db_kv_clear(ptr)

declare ptr @aiml_db_vec_open(ptr, i32)

declare ptr @realloc(ptr, i64)

declare void @aiml_db_vec_insert(ptr, ptr, ptr, i32)

declare i32 @aiml_db_vec_len(ptr)

declare void @aiml_db_vec_search(ptr, ptr, i32, ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare ptr @aiml_db_ts_open(ptr, i64)

declare void @aiml_db_ts_append(ptr, ptr, i64, i64)

declare i32 @aiml_db_ts_len(ptr)

declare ptr @aiml_db_ts_at(ptr, i64)

declare i32 @__gxx_personality_v0(...)

declare i32 @aiml_db_store_total(ptr)

declare i32 @aiml_db_store_is_active(ptr, i32)

declare ptr @aiml_db_store_get_ptr(ptr, i32)

declare void @aiml_db_store_query(ptr, ptr, i32, i32, i32, i32, i32, ptr)

attributes #0 = { noinline }
