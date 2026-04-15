; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Entity = type { i32, i32 }
%list_f32 = type { i32, i32, ptr }
%"list_ptr<VecResult>" = type { i32, i32, ptr }
%GameState = type { i32, i32 }
%Health = type { i32 }
%Pos = type { i32 }
%"list_ptr<Health>" = type { i32, i32, ptr }
%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%GenericList = type { i32, i32, ptr }
%VecResult = type { ptr, float }

@__argc = external global i32
@__argv = external global ptr
@__global_entities = global ptr null
@__global_e = global %Entity zeroinitializer
@__global_eid = global i32 0
@__global_p = global ptr null
@__global_n = global i32 0
@__global_cfg = global ptr null
@__global_v = global ptr null
@__global_has = global i32 0
@__global_klen = global i32 0
@__global_vs = global ptr null
@__global_v1 = global %list_f32 zeroinitializer
@__global_v2 = global %list_f32 zeroinitializer
@__global_vlen = global i32 0
@__global_q = global %list_f32 zeroinitializer
@__global_results = global %"list_ptr<VecResult>" zeroinitializer
@__global_r = global ptr null
@__global_history = global ptr null
@__global_gs = global %GameState zeroinitializer
@__global_tlen = global i32 0
@__global_tp = global ptr null
@__global_hp_store = global ptr null
@__global_pos_store = global ptr null
@__global_h1 = global %Health zeroinitializer
@__global_p1 = global %Pos zeroinitializer
@__global_h2 = global %Health zeroinitializer
@__global_high_hp = global %"list_ptr<Health>" zeroinitializer
@__global_qlen = global i32 0
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
@__aiml_schemas = global [4 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_Entity, i32 1, i32 2, i32 8, i32 8, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Entity_id }, %AimlFieldInfo { ptr @__aiml_sfield_Entity_hp }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_GameState, i32 1, i32 2, i32 8, i32 8, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_GameState_score }, %AimlFieldInfo { ptr @__aiml_sfield_GameState_hp }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Health, i32 1, i32 1, i32 4, i32 4, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Health_hp }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Pos, i32 1, i32 1, i32 4, i32 4, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Pos_x }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
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

if_merge:                                         ; preds = %ret_dead1, %ret_dead
  ret i32 0

ret_dead:                                         ; No predecessors!
  br label %if_merge

ret_dead1:                                        ; No predecessors!
  br label %if_merge
}

define void @__aiml_init() {
entry:
  %0 = call ptr @aiml_db_store_open(ptr @0, ptr @1, i64 8)
  store ptr %0, ptr @__global_entities, align 8
  store i32 42, ptr @__global_e, align 4
  store i32 100, ptr getelementptr inbounds (%Entity, ptr @__global_e, i32 0, i32 1), align 4
  %e_val = load %Entity, ptr @__global_e, align 4
  %store_ptr = load ptr, ptr @__global_entities, align 8
  %1 = call i32 @aiml_db_store_insert(ptr %store_ptr, ptr @__global_e, i64 8)
  store i32 %1, ptr @__global_eid, align 4
  %eid_val = load i32, ptr @__global_eid, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %eid_val)
  %3 = call ptr @__aiml_str_concat(ptr @2, ptr %2)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str)
  %store_ptr1 = load ptr, ptr @__global_entities, align 8
  %4 = call ptr @aiml_db_store_get(ptr %store_ptr1, i32 1)
  store ptr %4, ptr @__global_p, align 8
  %prop_ptr_base = load ptr, ptr @__global_p, align 8
  %prop_ptr = getelementptr inbounds %Entity, ptr %prop_ptr_base, i32 0, i32 1
  %hp_val = load i32, ptr %prop_ptr, align 4
  %5 = call ptr @__aiml_int_to_string(i32 %hp_val)
  %6 = call ptr @__aiml_str_concat(ptr @4, ptr %5)
  %print_null_check2 = icmp eq ptr %6, null
  %print_safe_str3 = select i1 %print_null_check2, ptr @print_null_str.1, ptr %6
  %print_call4 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str3)
  %store_ptr5 = load ptr, ptr @__global_entities, align 8
  %7 = call i32 @aiml_db_store_len(ptr %store_ptr5)
  store i32 %7, ptr @__global_n, align 4
  %n_val = load i32, ptr @__global_n, align 4
  %8 = call ptr @__aiml_int_to_string(i32 %n_val)
  %9 = call ptr @__aiml_str_concat(ptr @6, ptr %8)
  %print_null_check6 = icmp eq ptr %9, null
  %print_safe_str7 = select i1 %print_null_check6, ptr @print_null_str.2, ptr %9
  %print_call8 = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str7)
  store i32 200, ptr getelementptr inbounds (%Entity, ptr @__global_e, i32 0, i32 1), align 4
  %e_val9 = load %Entity, ptr @__global_e, align 4
  %store_ptr10 = load ptr, ptr @__global_entities, align 8
  %10 = call i32 @aiml_db_store_update(ptr %store_ptr10, i32 1, ptr @__global_e, i64 8)
  %store_ptr11 = load ptr, ptr @__global_entities, align 8
  %11 = call ptr @aiml_db_store_get(ptr %store_ptr11, i32 1)
  store ptr %11, ptr @__global_p, align 8
  %prop_ptr_base12 = load ptr, ptr @__global_p, align 8
  %prop_ptr13 = getelementptr inbounds %Entity, ptr %prop_ptr_base12, i32 0, i32 1
  %hp_val14 = load i32, ptr %prop_ptr13, align 4
  %12 = call ptr @__aiml_int_to_string(i32 %hp_val14)
  %13 = call ptr @__aiml_str_concat(ptr @8, ptr %12)
  %print_null_check15 = icmp eq ptr %13, null
  %print_safe_str16 = select i1 %print_null_check15, ptr @print_null_str.3, ptr %13
  %print_call17 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str16)
  %14 = call ptr @aiml_db_kv_open(ptr @10, i64 0)
  store ptr %14, ptr @__global_cfg, align 8
  %kv_ptr = load ptr, ptr @__global_cfg, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @11, ptr @12, i64 0)
  %kv_ptr18 = load ptr, ptr @__global_cfg, align 8
  %15 = call ptr @aiml_db_kv_get(ptr %kv_ptr18, ptr @13)
  store ptr %15, ptr @__global_v, align 8
  %v_val = load ptr, ptr @__global_v, align 8
  %16 = call ptr @__aiml_str_concat(ptr @14, ptr %v_val)
  %print_null_check19 = icmp eq ptr %16, null
  %print_safe_str20 = select i1 %print_null_check19, ptr @print_null_str.4, ptr %16
  %print_call21 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str20)
  %kv_ptr22 = load ptr, ptr @__global_cfg, align 8
  %17 = call i32 @aiml_db_kv_has(ptr %kv_ptr22, ptr @16)
  store i32 %17, ptr @__global_has, align 4
  %has_val = load i32, ptr @__global_has, align 4
  %18 = call ptr @__aiml_int_to_string(i32 %has_val)
  %19 = call ptr @__aiml_str_concat(ptr @17, ptr %18)
  %print_null_check23 = icmp eq ptr %19, null
  %print_safe_str24 = select i1 %print_null_check23, ptr @print_null_str.5, ptr %19
  %print_call25 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str24)
  %kv_ptr26 = load ptr, ptr @__global_cfg, align 8
  %20 = call i32 @aiml_db_kv_len(ptr %kv_ptr26)
  store i32 %20, ptr @__global_klen, align 4
  %klen_val = load i32, ptr @__global_klen, align 4
  %21 = call ptr @__aiml_int_to_string(i32 %klen_val)
  %22 = call ptr @__aiml_str_concat(ptr @19, ptr %21)
  %print_null_check27 = icmp eq ptr %22, null
  %print_safe_str28 = select i1 %print_null_check27, ptr @print_null_str.6, ptr %22
  %print_call29 = call i32 (ptr, ...) @printf(ptr @20, ptr %print_safe_str28)
  %kv_ptr30 = load ptr, ptr @__global_cfg, align 8
  call void @aiml_db_kv_delete(ptr %kv_ptr30, ptr @21)
  %kv_ptr31 = load ptr, ptr @__global_cfg, align 8
  %23 = call i32 @aiml_db_kv_has(ptr %kv_ptr31, ptr @22)
  store i32 %23, ptr @__global_has, align 4
  %has_val32 = load i32, ptr @__global_has, align 4
  %24 = call ptr @__aiml_int_to_string(i32 %has_val32)
  %25 = call ptr @__aiml_str_concat(ptr @23, ptr %24)
  %print_null_check33 = icmp eq ptr %25, null
  %print_safe_str34 = select i1 %print_null_check33, ptr @print_null_str.7, ptr %25
  %print_call35 = call i32 (ptr, ...) @printf(ptr @24, ptr %print_safe_str34)
  %kv_ptr36 = load ptr, ptr @__global_cfg, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr36, ptr @25, ptr @26, i64 0)
  %kv_ptr37 = load ptr, ptr @__global_cfg, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr37, ptr @27, ptr @28, i64 0)
  %kv_ptr38 = load ptr, ptr @__global_cfg, align 8
  %26 = call i32 @aiml_db_kv_len(ptr %kv_ptr38)
  store i32 %26, ptr @__global_klen, align 4
  %klen_val39 = load i32, ptr @__global_klen, align 4
  %27 = call ptr @__aiml_int_to_string(i32 %klen_val39)
  %28 = call ptr @__aiml_str_concat(ptr @29, ptr %27)
  %print_null_check40 = icmp eq ptr %28, null
  %print_safe_str41 = select i1 %print_null_check40, ptr @print_null_str.8, ptr %28
  %print_call42 = call i32 (ptr, ...) @printf(ptr @30, ptr %print_safe_str41)
  %kv_ptr43 = load ptr, ptr @__global_cfg, align 8
  call void @aiml_db_kv_clear(ptr %kv_ptr43)
  %kv_ptr44 = load ptr, ptr @__global_cfg, align 8
  %29 = call i32 @aiml_db_kv_len(ptr %kv_ptr44)
  store i32 %29, ptr @__global_klen, align 4
  %klen_val45 = load i32, ptr @__global_klen, align 4
  %30 = call ptr @__aiml_int_to_string(i32 %klen_val45)
  %31 = call ptr @__aiml_str_concat(ptr @31, ptr %30)
  %print_null_check46 = icmp eq ptr %31, null
  %print_safe_str47 = select i1 %print_null_check46, ptr @print_null_str.9, ptr %31
  %print_call48 = call i32 (ptr, ...) @printf(ptr @32, ptr %print_safe_str47)
  %32 = call ptr @aiml_db_vec_open(ptr @33, i32 3)
  store ptr %32, ptr @__global_vs, align 8
  store i32 0, ptr @__global_v1, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 1), align 4
  %33 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  store ptr %33, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  %size = load i32, ptr @__global_v1, align 4
  %capacity = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 1), align 4
  %34 = icmp eq i32 %size, %capacity
  br i1 %34, label %grow, label %cont

grow:                                             ; preds = %entry
  %35 = mul i32 %capacity, 2
  %36 = icmp sgt i32 %35, 0
  %37 = select i1 %36, i32 %35, i32 4
  store i32 %37, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 1), align 4
  %38 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  %39 = zext i32 %37 to i64
  %40 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %39
  %41 = call ptr @realloc(ptr %38, i64 %40)
  store ptr %41, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataBase = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  %42 = getelementptr inbounds float, ptr %dataBase, i32 %size
  store float 1.000000e+00, ptr %42, align 4
  %43 = add i32 %size, 1
  store i32 %43, ptr @__global_v1, align 4
  %size49 = load i32, ptr @__global_v1, align 4
  %capacity50 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 1), align 4
  %44 = icmp eq i32 %size49, %capacity50
  br i1 %44, label %grow51, label %cont52

grow51:                                           ; preds = %cont
  %45 = mul i32 %capacity50, 2
  %46 = icmp sgt i32 %45, 0
  %47 = select i1 %46, i32 %45, i32 4
  store i32 %47, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 1), align 4
  %48 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  %49 = zext i32 %47 to i64
  %50 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %49
  %51 = call ptr @realloc(ptr %48, i64 %50)
  store ptr %51, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  br label %cont52

cont52:                                           ; preds = %grow51, %cont
  %dataBase53 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  %52 = getelementptr inbounds float, ptr %dataBase53, i32 %size49
  store float 0.000000e+00, ptr %52, align 4
  %53 = add i32 %size49, 1
  store i32 %53, ptr @__global_v1, align 4
  %size54 = load i32, ptr @__global_v1, align 4
  %capacity55 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 1), align 4
  %54 = icmp eq i32 %size54, %capacity55
  br i1 %54, label %grow56, label %cont57

grow56:                                           ; preds = %cont52
  %55 = mul i32 %capacity55, 2
  %56 = icmp sgt i32 %55, 0
  %57 = select i1 %56, i32 %55, i32 4
  store i32 %57, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 1), align 4
  %58 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  %59 = zext i32 %57 to i64
  %60 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %59
  %61 = call ptr @realloc(ptr %58, i64 %60)
  store ptr %61, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  br label %cont57

cont57:                                           ; preds = %grow56, %cont52
  %dataBase58 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  %62 = getelementptr inbounds float, ptr %dataBase58, i32 %size54
  store float 0.000000e+00, ptr %62, align 4
  %63 = add i32 %size54, 1
  store i32 %63, ptr @__global_v1, align 4
  %v1_val = load %list_f32, ptr @__global_v1, align 8
  %vs_ptr = load ptr, ptr @__global_vs, align 8
  %list_sz = load i32, ptr @__global_v1, align 4
  %data_ptr = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v1, i32 0, i32 2), align 8
  call void @aiml_db_vec_insert(ptr %vs_ptr, ptr @34, ptr %data_ptr, i32 %list_sz)
  store i32 0, ptr @__global_v2, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 1), align 4
  %64 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  store ptr %64, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  %size59 = load i32, ptr @__global_v2, align 4
  %capacity60 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 1), align 4
  %65 = icmp eq i32 %size59, %capacity60
  br i1 %65, label %grow61, label %cont62

grow61:                                           ; preds = %cont57
  %66 = mul i32 %capacity60, 2
  %67 = icmp sgt i32 %66, 0
  %68 = select i1 %67, i32 %66, i32 4
  store i32 %68, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 1), align 4
  %69 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  %70 = zext i32 %68 to i64
  %71 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %70
  %72 = call ptr @realloc(ptr %69, i64 %71)
  store ptr %72, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  br label %cont62

cont62:                                           ; preds = %grow61, %cont57
  %dataBase63 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  %73 = getelementptr inbounds float, ptr %dataBase63, i32 %size59
  store float 0.000000e+00, ptr %73, align 4
  %74 = add i32 %size59, 1
  store i32 %74, ptr @__global_v2, align 4
  %size64 = load i32, ptr @__global_v2, align 4
  %capacity65 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 1), align 4
  %75 = icmp eq i32 %size64, %capacity65
  br i1 %75, label %grow66, label %cont67

grow66:                                           ; preds = %cont62
  %76 = mul i32 %capacity65, 2
  %77 = icmp sgt i32 %76, 0
  %78 = select i1 %77, i32 %76, i32 4
  store i32 %78, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 1), align 4
  %79 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  %80 = zext i32 %78 to i64
  %81 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %80
  %82 = call ptr @realloc(ptr %79, i64 %81)
  store ptr %82, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  br label %cont67

cont67:                                           ; preds = %grow66, %cont62
  %dataBase68 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  %83 = getelementptr inbounds float, ptr %dataBase68, i32 %size64
  store float 1.000000e+00, ptr %83, align 4
  %84 = add i32 %size64, 1
  store i32 %84, ptr @__global_v2, align 4
  %size69 = load i32, ptr @__global_v2, align 4
  %capacity70 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 1), align 4
  %85 = icmp eq i32 %size69, %capacity70
  br i1 %85, label %grow71, label %cont72

grow71:                                           ; preds = %cont67
  %86 = mul i32 %capacity70, 2
  %87 = icmp sgt i32 %86, 0
  %88 = select i1 %87, i32 %86, i32 4
  store i32 %88, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 1), align 4
  %89 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  %90 = zext i32 %88 to i64
  %91 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %90
  %92 = call ptr @realloc(ptr %89, i64 %91)
  store ptr %92, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  br label %cont72

cont72:                                           ; preds = %grow71, %cont67
  %dataBase73 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  %93 = getelementptr inbounds float, ptr %dataBase73, i32 %size69
  store float 0.000000e+00, ptr %93, align 4
  %94 = add i32 %size69, 1
  store i32 %94, ptr @__global_v2, align 4
  %v2_val = load %list_f32, ptr @__global_v2, align 8
  %vs_ptr74 = load ptr, ptr @__global_vs, align 8
  %list_sz75 = load i32, ptr @__global_v2, align 4
  %data_ptr76 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_v2, i32 0, i32 2), align 8
  call void @aiml_db_vec_insert(ptr %vs_ptr74, ptr @35, ptr %data_ptr76, i32 %list_sz75)
  %vs_ptr77 = load ptr, ptr @__global_vs, align 8
  %95 = call i32 @aiml_db_vec_len(ptr %vs_ptr77)
  store i32 %95, ptr @__global_vlen, align 4
  %vlen_val = load i32, ptr @__global_vlen, align 4
  %96 = call ptr @__aiml_int_to_string(i32 %vlen_val)
  %97 = call ptr @__aiml_str_concat(ptr @36, ptr %96)
  %print_null_check78 = icmp eq ptr %97, null
  %print_safe_str79 = select i1 %print_null_check78, ptr @print_null_str.10, ptr %97
  %print_call80 = call i32 (ptr, ...) @printf(ptr @37, ptr %print_safe_str79)
  store i32 0, ptr @__global_q, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 1), align 4
  %98 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  store ptr %98, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %size81 = load i32, ptr @__global_q, align 4
  %capacity82 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 1), align 4
  %99 = icmp eq i32 %size81, %capacity82
  br i1 %99, label %grow83, label %cont84

grow83:                                           ; preds = %cont72
  %100 = mul i32 %capacity82, 2
  %101 = icmp sgt i32 %100, 0
  %102 = select i1 %101, i32 %100, i32 4
  store i32 %102, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 1), align 4
  %103 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %104 = zext i32 %102 to i64
  %105 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %104
  %106 = call ptr @realloc(ptr %103, i64 %105)
  store ptr %106, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  br label %cont84

cont84:                                           ; preds = %grow83, %cont72
  %dataBase85 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %107 = getelementptr inbounds float, ptr %dataBase85, i32 %size81
  store float 1.000000e+00, ptr %107, align 4
  %108 = add i32 %size81, 1
  store i32 %108, ptr @__global_q, align 4
  %size86 = load i32, ptr @__global_q, align 4
  %capacity87 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 1), align 4
  %109 = icmp eq i32 %size86, %capacity87
  br i1 %109, label %grow88, label %cont89

grow88:                                           ; preds = %cont84
  %110 = mul i32 %capacity87, 2
  %111 = icmp sgt i32 %110, 0
  %112 = select i1 %111, i32 %110, i32 4
  store i32 %112, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 1), align 4
  %113 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %114 = zext i32 %112 to i64
  %115 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %114
  %116 = call ptr @realloc(ptr %113, i64 %115)
  store ptr %116, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  br label %cont89

cont89:                                           ; preds = %grow88, %cont84
  %dataBase90 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %117 = getelementptr inbounds float, ptr %dataBase90, i32 %size86
  store float 0.000000e+00, ptr %117, align 4
  %118 = add i32 %size86, 1
  store i32 %118, ptr @__global_q, align 4
  %size91 = load i32, ptr @__global_q, align 4
  %capacity92 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 1), align 4
  %119 = icmp eq i32 %size91, %capacity92
  br i1 %119, label %grow93, label %cont94

grow93:                                           ; preds = %cont89
  %120 = mul i32 %capacity92, 2
  %121 = icmp sgt i32 %120, 0
  %122 = select i1 %121, i32 %120, i32 4
  store i32 %122, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 1), align 4
  %123 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %124 = zext i32 %122 to i64
  %125 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %124
  %126 = call ptr @realloc(ptr %123, i64 %125)
  store ptr %126, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  br label %cont94

cont94:                                           ; preds = %grow93, %cont89
  %dataBase95 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %127 = getelementptr inbounds float, ptr %dataBase95, i32 %size91
  store float 0.000000e+00, ptr %127, align 4
  %128 = add i32 %size91, 1
  store i32 %128, ptr @__global_q, align 4
  store i32 0, ptr @__global_results, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_results, i32 0, i32 1), align 4
  %129 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %129, ptr getelementptr inbounds (%GenericList, ptr @__global_results, i32 0, i32 2), align 8
  %q_val = load %list_f32, ptr @__global_q, align 8
  %vs_ptr96 = load ptr, ptr @__global_vs, align 8
  %list_sz97 = load i32, ptr @__global_q, align 4
  %data_ptr98 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_q, i32 0, i32 2), align 8
  %vecres_alloca = alloca %GenericList, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 1
  store i32 8, ptr %capPtr, align 4
  %130 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr = getelementptr inbounds %GenericList, ptr %vecres_alloca, i32 0, i32 2
  store ptr %130, ptr %dataPtr, align 8
  call void @aiml_db_vec_search(ptr %vs_ptr96, ptr %data_ptr98, i32 2, ptr %vecres_alloca)
  %131 = call ptr @memcpy(ptr @__global_results, ptr %vecres_alloca, i64 16)
  %dataBase99 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_results, i32 0, i32 2), align 8
  %132 = getelementptr inbounds ptr, ptr %dataBase99, i32 0
  %getVal = load ptr, ptr %132, align 8
  store ptr %getVal, ptr @__global_r, align 8
  %prop_ptr_base100 = load ptr, ptr @__global_r, align 8
  %prop_ptr101 = getelementptr inbounds %VecResult, ptr %prop_ptr_base100, i32 0, i32 0
  %id_val = load ptr, ptr %prop_ptr101, align 8
  %133 = call ptr @__aiml_str_concat(ptr @38, ptr %id_val)
  %print_null_check102 = icmp eq ptr %133, null
  %print_safe_str103 = select i1 %print_null_check102, ptr @print_null_str.11, ptr %133
  %print_call104 = call i32 (ptr, ...) @printf(ptr @39, ptr %print_safe_str103)
  %134 = call ptr @aiml_db_ts_open(ptr @40, i64 8)
  store ptr %134, ptr @__global_history, align 8
  store i32 100, ptr @__global_gs, align 4
  store i32 75, ptr getelementptr inbounds (%GameState, ptr @__global_gs, i32 0, i32 1), align 4
  %gs_val = load %GameState, ptr @__global_gs, align 4
  %ts_ptr = load ptr, ptr @__global_history, align 8
  call void @aiml_db_ts_append(ptr %ts_ptr, ptr @__global_gs, i64 8, i64 1000)
  store i32 200, ptr @__global_gs, align 4
  store i32 50, ptr getelementptr inbounds (%GameState, ptr @__global_gs, i32 0, i32 1), align 4
  %gs_val105 = load %GameState, ptr @__global_gs, align 4
  %ts_ptr106 = load ptr, ptr @__global_history, align 8
  call void @aiml_db_ts_append(ptr %ts_ptr106, ptr @__global_gs, i64 8, i64 2000)
  %ts_ptr107 = load ptr, ptr @__global_history, align 8
  %135 = call i32 @aiml_db_ts_len(ptr %ts_ptr107)
  store i32 %135, ptr @__global_tlen, align 4
  %tlen_val = load i32, ptr @__global_tlen, align 4
  %136 = call ptr @__aiml_int_to_string(i32 %tlen_val)
  %137 = call ptr @__aiml_str_concat(ptr @41, ptr %136)
  %print_null_check108 = icmp eq ptr %137, null
  %print_safe_str109 = select i1 %print_null_check108, ptr @print_null_str.12, ptr %137
  %print_call110 = call i32 (ptr, ...) @printf(ptr @42, ptr %print_safe_str109)
  %ts_ptr111 = load ptr, ptr @__global_history, align 8
  %138 = call ptr @aiml_db_ts_at(ptr %ts_ptr111, i64 1500)
  store ptr %138, ptr @__global_tp, align 8
  %prop_ptr_base112 = load ptr, ptr @__global_tp, align 8
  %prop_ptr113 = getelementptr inbounds %GameState, ptr %prop_ptr_base112, i32 0, i32 0
  %score_val = load i32, ptr %prop_ptr113, align 4
  %139 = call ptr @__aiml_int_to_string(i32 %score_val)
  %140 = call ptr @__aiml_str_concat(ptr @43, ptr %139)
  %print_null_check114 = icmp eq ptr %140, null
  %print_safe_str115 = select i1 %print_null_check114, ptr @print_null_str.13, ptr %140
  %print_call116 = call i32 (ptr, ...) @printf(ptr @44, ptr %print_safe_str115)
  %141 = call ptr @aiml_db_store_open(ptr @45, ptr @46, i64 4)
  store ptr %141, ptr @__global_hp_store, align 8
  %142 = call ptr @aiml_db_store_open(ptr @47, ptr @48, i64 4)
  store ptr %142, ptr @__global_pos_store, align 8
  store i32 80, ptr @__global_h1, align 4
  %h1_val = load %Health, ptr @__global_h1, align 4
  %store_ptr117 = load ptr, ptr @__global_hp_store, align 8
  %143 = call i32 @aiml_db_store_insert(ptr %store_ptr117, ptr @__global_h1, i64 4)
  store i32 10, ptr @__global_p1, align 4
  %p1_val = load %Pos, ptr @__global_p1, align 4
  %store_ptr118 = load ptr, ptr @__global_pos_store, align 8
  %144 = call i32 @aiml_db_store_insert(ptr %store_ptr118, ptr @__global_p1, i64 4)
  store i32 30, ptr @__global_h2, align 4
  %h2_val = load %Health, ptr @__global_h2, align 4
  %store_ptr119 = load ptr, ptr @__global_hp_store, align 8
  %145 = call i32 @aiml_db_store_insert(ptr %store_ptr119, ptr @__global_h2, i64 4)
  %hp_store_sptr = load ptr, ptr @__global_hp_store, align 8
  %pos_store_sptr = load ptr, ptr @__global_pos_store, align 8
  %st_total = call i32 @aiml_db_store_total(ptr %hp_store_sptr)
  %is_bigger = icmp sgt i32 %st_total, 0
  %max_total = select i1 %is_bigger, i32 %st_total, i32 0
  %st_total120 = call i32 @aiml_db_store_total(ptr %pos_store_sptr)
  %is_bigger121 = icmp sgt i32 %st_total120, %max_total
  %max_total122 = select i1 %is_bigger121, i32 %st_total120, i32 %max_total
  %rs_eid = alloca i32, align 4
  store i32 1, ptr %rs_eid, align 4
  %rs_max = alloca i32, align 4
  store i32 %max_total122, ptr %rs_max, align 4
  br label %rs_hdr

rs_hdr:                                           ; preds = %rs_next, %cont94
  %eid = load i32, ptr %rs_eid, align 4
  %mx = load i32, ptr %rs_max, align 4
  %in_range = icmp sle i32 %eid, %mx
  br i1 %in_range, label %rs_chk, label %rs_exit

rs_chk:                                           ; preds = %rs_hdr
  %eid123 = load i32, ptr %rs_eid, align 4
  %active = call i32 @aiml_db_store_is_active(ptr %hp_store_sptr, i32 %eid123)
  %all_ok = and i32 1, %active
  %active124 = call i32 @aiml_db_store_is_active(ptr %pos_store_sptr, i32 %eid123)
  %all_ok125 = and i32 %all_ok, %active124
  %all_bool = icmp ne i32 %all_ok125, 0
  br i1 %all_bool, label %rs_call, label %rs_next

rs_call:                                          ; preds = %rs_chk
  %eid126 = load i32, ptr %rs_eid, align 4
  %comp_ptr = call ptr @aiml_db_store_get_ptr(ptr %hp_store_sptr, i32 %eid126)
  call void @tick_hp(ptr %comp_ptr)
  br label %rs_next

rs_next:                                          ; preds = %rs_call, %rs_chk
  %eid127 = load i32, ptr %rs_eid, align 4
  %eid_next = add i32 %eid127, 1
  store i32 %eid_next, ptr %rs_eid, align 4
  br label %rs_hdr

rs_exit:                                          ; preds = %rs_hdr
  store i32 0, ptr @__global_high_hp, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_high_hp, i32 0, i32 1), align 4
  %146 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %146, ptr getelementptr inbounds (%GenericList, ptr @__global_high_hp, i32 0, i32 2), align 8
  %hp_store_val = load ptr, ptr @__global_hp_store, align 8
  %query_list = alloca %GenericList, align 8
  %sizePtr128 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 0
  store i32 0, ptr %sizePtr128, align 4
  %capPtr129 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 1
  store i32 8, ptr %capPtr129, align 4
  %147 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr130 = getelementptr inbounds %GenericList, ptr %query_list, i32 0, i32 2
  store ptr %147, ptr %dataPtr130, align 8
  call void @aiml_db_store_query(ptr %hp_store_val, ptr @is_high_hp, i32 -1, i32 0, i32 0, i32 -1, i32 0, ptr %query_list)
  %148 = call ptr @memcpy(ptr @__global_high_hp, ptr %query_list, i64 16)
  %sizeVal = load i32, ptr @__global_high_hp, align 4
  store i32 %sizeVal, ptr @__global_qlen, align 4
  %qlen_val = load i32, ptr @__global_qlen, align 4
  %149 = call ptr @__aiml_int_to_string(i32 %qlen_val)
  %150 = call ptr @__aiml_str_concat(ptr @51, ptr %149)
  %print_null_check131 = icmp eq ptr %150, null
  %print_safe_str132 = select i1 %print_null_check131, ptr @print_null_str.15, ptr %150
  %print_call133 = call i32 (ptr, ...) @printf(ptr @52, ptr %print_safe_str132)
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
