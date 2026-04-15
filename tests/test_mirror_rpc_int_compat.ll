; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%thread_handle = type { i64, i8, i8 }
%mirror_event = type { ptr, ptr, i32 }
%NetPlayer = type { i32, i32 }
%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%rpc_event = type { ptr, i32, i32, i32, ptr, i32 }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_NetPlayer = private unnamed_addr constant [3 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 2, i32 1, i32 0, i32 8, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 4, i32 0, i32 0 }]
@__global_host_th = global %thread_handle zeroinitializer
@__global_client = global ptr null
@__global_ev = global %mirror_event zeroinitializer
@__global_replica = global %NetPlayer zeroinitializer
@0 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19901\00", align 1
@1 = private unnamed_addr constant [29 x i8] c"host-int: registered net_id=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@4 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19901\00", align 1
@5 = private unnamed_addr constant [28 x i8] c"client-int: initial health=\00", align 1
@6 = private unnamed_addr constant [8 x i8] c" score=\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [12 x i8] c"take_damage\00", align 1
@9 = private unnamed_addr constant [32 x i8] c"client-int: sent take_damage(7)\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [26 x i8] c"client-int: final health=\00", align 1
@12 = private unnamed_addr constant [8 x i8] c" score=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [21 x i8] c"FAIL int-rpc health=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [20 x i8] c"FAIL int-rpc score=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [27 x i8] c"mirror rpc int compat PASS\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @mirror_rpc_int_host, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @mirror_rpc_int_host]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_mirror_rpc_int_host = private constant [20 x i8] c"mirror_rpc_int_host\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_mirror_rpc_int_host]
@__aiml_func_count = constant i32 1
@__aiml_sname_NetPlayer = private constant [10 x i8] c"NetPlayer\00"
@__aiml_sfield_NetPlayer_health = private constant [7 x i8] c"health\00"
@__aiml_sfield_NetPlayer_score = private constant [6 x i8] c"score\00"
@__aiml_schemas = global [1 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_NetPlayer, i32 1, i32 2, i32 8, i32 8, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_NetPlayer_health }, %AimlFieldInfo { ptr @__aiml_sfield_NetPlayer_score }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 1

define void @mirror_rpc_int_host() personality ptr @__gxx_personality_v0 {
entry:
  %sess = alloca ptr, align 8
  store ptr null, ptr %sess, align 8
  %0 = call ptr @aiml_mirror_listen(ptr @0)
  store ptr %0, ptr %sess, align 8
  %player = alloca %NetPlayer, align 8
  store %NetPlayer zeroinitializer, ptr %player, align 4
  %set_prop_ptr_w = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 50, ptr %set_prop_ptr_w, align 4
  %set_prop_ptr_w1 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 0, ptr %set_prop_ptr_w1, align 4
  %nid = alloca i32, align 4
  store i32 0, ptr %nid, align 4
  %sess_ptr = load ptr, ptr %sess, align 8
  %1 = call i32 @aiml_mirror_session_register(ptr %sess_ptr, ptr %player, i64 8)
  store i32 %1, ptr %nid, align 4
  %nid_val = load i32, ptr %nid, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %nid_val)
  %3 = call ptr @__aiml_str_concat(ptr @1, ptr %2)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  store i32 0, ptr %i, align 4
  br label %while_loop

while_loop:                                       ; preds = %while_body, %entry
  %i_val = load i32, ptr %i, align 4
  %4 = icmp slt i32 %i_val, 25
  br i1 %4, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %sess_ptr2 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr2)
  call void @time_sleep_ms(i32 10)
  %i_val3 = load i32, ptr %i, align 4
  %5 = add i32 %i_val3, 1
  store i32 %5, ptr %i, align 4
  br label %while_loop

while_end:                                        ; preds = %while_loop
  %empty_rpc = alloca %rpc_event, align 8
  store %rpc_event zeroinitializer, ptr %empty_rpc, align 8
  %sess_ptr4 = load ptr, ptr %sess, align 8
  %poll_rpc_ptr = call ptr @aiml_mirror_session_poll_rpc(ptr %sess_ptr4)
  %poll_rpc_tmp = alloca %rpc_event, align 8
  %6 = call ptr @memcpy(ptr %poll_rpc_tmp, ptr %poll_rpc_ptr, i64 40)
  %7 = call ptr @memcpy(ptr %empty_rpc, ptr %poll_rpc_tmp, i64 40)
  %prop_ptr = getelementptr inbounds %rpc_event, ptr %empty_rpc, i32 0, i32 0
  %method_val = load ptr, ptr %prop_ptr, align 8
  %8 = icmp eq ptr %method_val, null
  %9 = icmp eq ptr %method_val, @3
  %10 = xor i1 %9, true
  %11 = or i1 %8, false
  br i1 %11, label %cmp_check_null_ne, label %cmp_do_strcmp_ne

cmp_check_null_ne:                                ; preds = %while_end
  br label %cmp_merge_ne

cmp_do_strcmp_ne:                                 ; preds = %while_end
  %12 = call i32 @strcmp(ptr %method_val, ptr @3)
  %13 = icmp ne i32 %12, 0
  br label %cmp_merge_ne

cmp_merge_ne:                                     ; preds = %cmp_do_strcmp_ne, %cmp_check_null_ne
  %strcmp_res_ne = phi i1 [ %10, %cmp_check_null_ne ], [ %13, %cmp_do_strcmp_ne ]
  br i1 %strcmp_res_ne, label %if_then, label %if_merge

if_then:                                          ; preds = %cmp_merge_ne
  %set_prop_ptr_w5 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 700, ptr %set_prop_ptr_w5, align 4
  %sess_ptr6 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_sync_now(ptr %sess_ptr6, ptr %player)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %cmp_merge_ne
  store i32 0, ptr %i, align 4
  br label %while_loop7

while_loop7:                                      ; preds = %if_merge14, %if_merge
  %i_val10 = load i32, ptr %i, align 4
  %14 = icmp slt i32 %i_val10, 40
  br i1 %14, label %while_body8, label %while_end9

while_body8:                                      ; preds = %while_loop7
  %sess_ptr11 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr11)
  %sess_ptr12 = load ptr, ptr %sess, align 8
  %15 = call i32 @aiml_mirror_session_has_rpc(ptr %sess_ptr12)
  %16 = icmp sgt i32 %15, 0
  br i1 %16, label %if_then13, label %if_merge14

while_end9:                                       ; preds = %while_loop7
  %sess_ptr37 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr37)
  ret void

if_then13:                                        ; preds = %while_body8
  %rpc_ev = alloca %rpc_event, align 8
  store %rpc_event zeroinitializer, ptr %rpc_ev, align 8
  %sess_ptr15 = load ptr, ptr %sess, align 8
  %rpc_ptr = call ptr @aiml_mirror_session_next_rpc(ptr %sess_ptr15)
  %rpc_tmp = alloca %rpc_event, align 8
  %17 = call ptr @memcpy(ptr %rpc_tmp, ptr %rpc_ptr, i64 40)
  %18 = call ptr @memcpy(ptr %rpc_ev, ptr %rpc_tmp, i64 40)
  %prop_ptr16 = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 3
  %arg_kind_val = load i32, ptr %prop_ptr16, align 4
  %19 = icmp ne i32 %arg_kind_val, 0
  br i1 %19, label %if_then17, label %if_else

if_merge14:                                       ; preds = %if_merge18, %while_body8
  call void @time_sleep_ms(i32 10)
  %i_val36 = load i32, ptr %i, align 4
  %20 = add i32 %i_val36, 1
  store i32 %20, ptr %i, align 4
  br label %while_loop7

if_then17:                                        ; preds = %if_then13
  %set_prop_ptr_w19 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 701, ptr %set_prop_ptr_w19, align 4
  br label %if_merge18

if_else:                                          ; preds = %if_then13
  %prop_ptr20 = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 2
  %arg_val = load i32, ptr %prop_ptr20, align 4
  %21 = icmp ne i32 %arg_val, 7
  br i1 %21, label %if_then21, label %if_else22

if_merge18:                                       ; preds = %if_merge23, %if_then17
  %sess_ptr35 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_sync_now(ptr %sess_ptr35, ptr %player)
  br label %if_merge14

if_then21:                                        ; preds = %if_else
  %set_prop_ptr_w24 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 702, ptr %set_prop_ptr_w24, align 4
  br label %if_merge23

if_else22:                                        ; preds = %if_else
  %prop_ptr25 = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 5
  %arg_blob_len_val = load i32, ptr %prop_ptr25, align 4
  %22 = icmp ne i32 %arg_blob_len_val, 0
  br i1 %22, label %if_then26, label %if_else27

if_merge23:                                       ; preds = %if_merge28, %if_then21
  br label %if_merge18

if_then26:                                        ; preds = %if_else22
  %set_prop_ptr_w29 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 703, ptr %set_prop_ptr_w29, align 4
  br label %if_merge28

if_else27:                                        ; preds = %if_else22
  %prop_ptr30 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  %health_val = load i32, ptr %prop_ptr30, align 4
  %prop_ptr31 = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 2
  %arg_val32 = load i32, ptr %prop_ptr31, align 4
  %23 = sub i32 %health_val, %arg_val32
  %set_prop_ptr_w33 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 %23, ptr %set_prop_ptr_w33, align 4
  %set_prop_ptr_w34 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 777, ptr %set_prop_ptr_w34, align 4
  br label %if_merge28

if_merge28:                                       ; preds = %if_else27, %if_then26
  br label %if_merge23
}

define void @__aiml_init() {
entry:
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %0 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_mirror_rpc_int_host, ptr null)
  store ptr %handle_raw, ptr @__global_host_th, align 8
  call void @time_sleep_ms(i32 80)
  %1 = call ptr @aiml_mirror_connect_timeout(ptr @4, i32 2000)
  store ptr %1, ptr @__global_client, align 8
  call void @time_sleep_ms(i32 120)
  %sess_ptr = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr)
  %sess_ptr1 = load ptr, ptr @__global_client, align 8
  %ev_ptr = call ptr @aiml_mirror_session_next_event(ptr %sess_ptr1)
  %ev_tmp = alloca %mirror_event, align 8
  %2 = call ptr @memcpy(ptr %ev_tmp, ptr %ev_ptr, i64 24)
  %3 = call ptr @memcpy(ptr @__global_ev, ptr %ev_tmp, i64 24)
  %sess_ptr2 = load ptr, ptr @__global_client, align 8
  %net_id_val = load i32, ptr getelementptr inbounds (%mirror_event, ptr @__global_ev, i32 0, i32 2), align 4
  call void @aiml_mirror_session_register_id(ptr %sess_ptr2, ptr @__global_replica, i64 8, i32 %net_id_val)
  call void @time_sleep_ms(i32 100)
  %sess_ptr3 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr3)
  %health_val = load i32, ptr @__global_replica, align 4
  %4 = call ptr @__aiml_int_to_string(i32 %health_val)
  %5 = call ptr @__aiml_str_concat(ptr @5, ptr %4)
  %6 = call ptr @__aiml_str_concat(ptr %5, ptr @6)
  %score_val = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %7 = call ptr @__aiml_int_to_string(i32 %score_val)
  %8 = call ptr @__aiml_str_concat(ptr %6, ptr %7)
  %print_null_check = icmp eq ptr %8, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.1, ptr %8
  %print_call = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str)
  %sess_ptr4 = load ptr, ptr @__global_client, align 8
  %net_id_val5 = load i32, ptr getelementptr inbounds (%mirror_event, ptr @__global_ev, i32 0, i32 2), align 4
  call void @aiml_mirror_session_send_rpc(ptr %sess_ptr4, i32 %net_id_val5, ptr @8, i32 7)
  %print_call6 = call i32 (ptr, ...) @printf(ptr @10, ptr @9)
  call void @time_sleep_ms(i32 240)
  %sess_ptr7 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr7)
  %health_val8 = load i32, ptr @__global_replica, align 4
  %9 = call ptr @__aiml_int_to_string(i32 %health_val8)
  %10 = call ptr @__aiml_str_concat(ptr @11, ptr %9)
  %11 = call ptr @__aiml_str_concat(ptr %10, ptr @12)
  %score_val9 = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %12 = call ptr @__aiml_int_to_string(i32 %score_val9)
  %13 = call ptr @__aiml_str_concat(ptr %11, ptr %12)
  %print_null_check10 = icmp eq ptr %13, null
  %print_safe_str11 = select i1 %print_null_check10, ptr @print_null_str.3, ptr %13
  %print_call12 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str11)
  %health_val13 = load i32, ptr @__global_replica, align 4
  %14 = icmp ne i32 %health_val13, 43
  br i1 %14, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %health_val14 = load i32, ptr @__global_replica, align 4
  %15 = call ptr @__aiml_int_to_string(i32 %health_val14)
  %16 = call ptr @__aiml_str_concat(ptr @14, ptr %15)
  %print_null_check15 = icmp eq ptr %16, null
  %print_safe_str16 = select i1 %print_null_check15, ptr @print_null_str.4, ptr %16
  %print_call17 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str16)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %score_val18 = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %17 = icmp ne i32 %score_val18, 777
  br i1 %17, label %if_then19, label %if_merge20

if_then19:                                        ; preds = %if_merge
  %score_val21 = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %18 = call ptr @__aiml_int_to_string(i32 %score_val21)
  %19 = call ptr @__aiml_str_concat(ptr @16, ptr %18)
  %print_null_check22 = icmp eq ptr %19, null
  %print_safe_str23 = select i1 %print_null_check22, ptr @print_null_str.5, ptr %19
  %print_call24 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str23)
  call void @exit(i32 1)
  br label %if_merge20

if_merge20:                                       ; preds = %if_then19, %if_merge
  %sess_ptr25 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr25)
  %handle_ptr = load ptr, ptr @__global_host_th, align 8
  %detached_ptr26 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr26, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

join_detached_throw:                              ; preds = %if_merge20
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %if_merge20
  %joined_ptr27 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr27, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc28 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc28, align 8
  call void @__cxa_throw(ptr %exc_alloc28, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %20 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr27, align 1
  %print_call29 = call i32 (ptr, ...) @printf(ptr @19, ptr @18)
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @aiml_mirror_listen(ptr)

declare i32 @aiml_mirror_session_register(ptr, ptr, i64)

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

declare void @aiml_mirror_session_update(ptr)

declare void @time_sleep_ms(i32)

declare ptr @aiml_mirror_session_poll_rpc(ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare i32 @strcmp(ptr, ptr)

declare void @aiml_mirror_session_sync_now(ptr, ptr)

declare i32 @aiml_mirror_session_has_rpc(ptr)

declare ptr @aiml_mirror_session_next_rpc(ptr)

declare void @aiml_mirror_session_close(ptr)

define internal ptr @__aiml_spawn_mirror_rpc_int_host(ptr %0) {
entry:
  call void @mirror_rpc_int_host()
  ret ptr null
}

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare ptr @aiml_mirror_connect_timeout(ptr, i32)

declare ptr @aiml_mirror_session_next_event(ptr)

declare void @aiml_mirror_session_register_id(ptr, ptr, i64, i32)

declare void @aiml_mirror_session_send_rpc(ptr, i32, ptr, i32)

declare void @exit(i32)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

attributes #0 = { noinline }
