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
@__global_srv_a = global ptr null
@__global_host_th = global %thread_handle zeroinitializer
@__global_client = global ptr null
@__global_conn = global i32 0
@__global_hev = global i32 0
@__global_ev = global %mirror_event zeroinitializer
@__global_ev_nid = global i32 0
@__global_replica = global %NetPlayer zeroinitializer
@__global_pc = global i32 0
@__global_rh = global i32 0
@__global_rs = global i32 0
@__global_rh2 = global i32 0
@0 = private unnamed_addr constant [22 x i8] c"tcp://127.0.0.1:19880\00", align 1
@1 = private unnamed_addr constant [31 x i8] c"Phase A: listen handle created\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [13 x i8] c"Phase A PASS\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [22 x i8] c"tcp://127.0.0.1:19881\00", align 1
@6 = private unnamed_addr constant [25 x i8] c"host: registered net_id=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [23 x i8] c"host: RPC take_damage(\00", align 1
@9 = private unnamed_addr constant [2 x i8] c")\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [11 x i8] c"host: done\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [22 x i8] c"tcp://127.0.0.1:19881\00", align 1
@14 = private unnamed_addr constant [20 x i8] c"Phase B: connect OK\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [23 x i8] c"Phase C: is_connected=\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [20 x i8] c"Phase D: has_event=\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [23 x i8] c"Phase D: event net_id=\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [39 x i8] c"Phase E: replica registered as net_id=\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [28 x i8] c"Phase F: client peer_count=\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [25 x i8] c"Phase E: replica.health=\00", align 1
@27 = private unnamed_addr constant [16 x i8] c" replica.score=\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@29 = private unnamed_addr constant [38 x i8] c"Phase E PASS: health synced correctly\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@30 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@31 = private unnamed_addr constant [31 x i8] c"Phase E FAIL: expected 77 got \00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [12 x i8] c"take_damage\00", align 1
@34 = private unnamed_addr constant [34 x i8] c"Phase H: sent RPC take_damage(10)\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [25 x i8] c"Phase H: replica.health=\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@38 = private unnamed_addr constant [36 x i8] c"Phase H PASS: health after RPC = 67\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [31 x i8] c"Phase H FAIL: expected 67 got \00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [27 x i8] c"Phase G: mirror tests done\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @mirror_host_fn, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @mirror_host_fn]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_mirror_host_fn = private constant [15 x i8] c"mirror_host_fn\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_mirror_host_fn]
@__aiml_func_count = constant i32 1
@__aiml_sname_NetPlayer = private constant [10 x i8] c"NetPlayer\00"
@__aiml_sfield_NetPlayer_health = private constant [7 x i8] c"health\00"
@__aiml_sfield_NetPlayer_score = private constant [6 x i8] c"score\00"
@__aiml_schemas = global [1 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_NetPlayer, i32 1, i32 2, i32 8, i32 8, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_NetPlayer_health }, %AimlFieldInfo { ptr @__aiml_sfield_NetPlayer_score }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 1

define void @mirror_host_fn() personality ptr @__gxx_personality_v0 {
entry:
  %sess = alloca ptr, align 8
  store ptr null, ptr %sess, align 8
  %0 = call ptr @aiml_mirror_listen(ptr @5)
  store ptr %0, ptr %sess, align 8
  %player = alloca %NetPlayer, align 8
  store %NetPlayer zeroinitializer, ptr %player, align 4
  %set_prop_ptr_w = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 77, ptr %set_prop_ptr_w, align 4
  %set_prop_ptr_w1 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 42, ptr %set_prop_ptr_w1, align 4
  %nid = alloca i32, align 4
  store i32 0, ptr %nid, align 4
  %sess_ptr = load ptr, ptr %sess, align 8
  %1 = call i32 @aiml_mirror_session_register(ptr %sess_ptr, ptr %player, i64 8)
  store i32 %1, ptr %nid, align 4
  %nid_val = load i32, ptr %nid, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %nid_val)
  %3 = call ptr @__aiml_str_concat(ptr @6, ptr %2)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.2, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str)
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  store i32 0, ptr %i, align 4
  br label %while_loop

while_loop:                                       ; preds = %while_body, %entry
  %i_val = load i32, ptr %i, align 4
  %4 = icmp slt i32 %i_val, 20
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
  store i32 0, ptr %i, align 4
  br label %while_loop4

while_loop4:                                      ; preds = %if_merge, %while_end
  %i_val7 = load i32, ptr %i, align 4
  %6 = icmp slt i32 %i_val7, 20
  br i1 %6, label %while_body5, label %while_end6

while_body5:                                      ; preds = %while_loop4
  %sess_ptr8 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr8)
  %hr = alloca i32, align 4
  store i32 0, ptr %hr, align 4
  %sess_ptr9 = load ptr, ptr %sess, align 8
  %7 = call i32 @aiml_mirror_session_has_rpc(ptr %sess_ptr9)
  store i32 %7, ptr %hr, align 4
  %hr_val = load i32, ptr %hr, align 4
  %8 = icmp sgt i32 %hr_val, 0
  br i1 %8, label %if_then, label %if_merge

while_end6:                                       ; preds = %while_loop4
  %sess_ptr18 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr18)
  %print_call19 = call i32 (ptr, ...) @printf(ptr @12, ptr @11)
  ret void

if_then:                                          ; preds = %while_body5
  %rpc_ev = alloca %rpc_event, align 8
  store %rpc_event zeroinitializer, ptr %rpc_ev, align 8
  %sess_ptr10 = load ptr, ptr %sess, align 8
  %rpc_ptr = call ptr @aiml_mirror_session_next_rpc(ptr %sess_ptr10)
  %rpc_tmp = alloca %rpc_event, align 8
  %9 = call ptr @memcpy(ptr %rpc_tmp, ptr %rpc_ptr, i64 40)
  %10 = call ptr @memcpy(ptr %rpc_ev, ptr %rpc_tmp, i64 40)
  %rpc_arg = alloca i32, align 4
  store i32 0, ptr %rpc_arg, align 4
  %prop_ptr = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 2
  %arg_val = load i32, ptr %prop_ptr, align 4
  store i32 %arg_val, ptr %rpc_arg, align 4
  %rpc_arg_val = load i32, ptr %rpc_arg, align 4
  %11 = call ptr @__aiml_int_to_string(i32 %rpc_arg_val)
  %12 = call ptr @__aiml_str_concat(ptr @8, ptr %11)
  %13 = call ptr @__aiml_str_concat(ptr %12, ptr @9)
  %print_null_check11 = icmp eq ptr %13, null
  %print_safe_str12 = select i1 %print_null_check11, ptr @print_null_str.3, ptr %13
  %print_call13 = call i32 (ptr, ...) @printf(ptr @10, ptr %print_safe_str12)
  %prop_ptr14 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  %health_val = load i32, ptr %prop_ptr14, align 4
  %rpc_arg_val15 = load i32, ptr %rpc_arg, align 4
  %14 = sub i32 %health_val, %rpc_arg_val15
  %set_prop_ptr_w16 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 %14, ptr %set_prop_ptr_w16, align 4
  br label %if_merge

if_merge:                                         ; preds = %if_then, %while_body5
  call void @time_sleep_ms(i32 10)
  %i_val17 = load i32, ptr %i, align 4
  %15 = add i32 %i_val17, 1
  store i32 %15, ptr %i, align 4
  br label %while_loop4
}

define void @__aiml_init() {
entry:
  %0 = call ptr @aiml_mirror_listen(ptr @0)
  store ptr %0, ptr @__global_srv_a, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr @1)
  %sess_ptr = load ptr, ptr @__global_srv_a, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @4, ptr @3)
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %1 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_mirror_host_fn, ptr null)
  store ptr %handle_raw, ptr @__global_host_th, align 8
  call void @time_sleep_ms(i32 50)
  %2 = call ptr @aiml_mirror_connect_timeout(ptr @13, i32 2000)
  store ptr %2, ptr @__global_client, align 8
  %print_call2 = call i32 (ptr, ...) @printf(ptr @15, ptr @14)
  %sess_ptr3 = load ptr, ptr @__global_client, align 8
  %3 = call i32 @aiml_mirror_session_is_connected(ptr %sess_ptr3)
  store i32 %3, ptr @__global_conn, align 4
  %conn_val = load i32, ptr @__global_conn, align 4
  %4 = call ptr @__aiml_int_to_string(i32 %conn_val)
  %5 = call ptr @__aiml_str_concat(ptr @16, ptr %4)
  %print_null_check = icmp eq ptr %5, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.6, ptr %5
  %print_call4 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str)
  call void @time_sleep_ms(i32 60)
  %sess_ptr5 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr5)
  %sess_ptr6 = load ptr, ptr @__global_client, align 8
  %6 = call i32 @aiml_mirror_session_has_event(ptr %sess_ptr6)
  store i32 %6, ptr @__global_hev, align 4
  %hev_val = load i32, ptr @__global_hev, align 4
  %7 = call ptr @__aiml_int_to_string(i32 %hev_val)
  %8 = call ptr @__aiml_str_concat(ptr @18, ptr %7)
  %print_null_check7 = icmp eq ptr %8, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.7, ptr %8
  %print_call9 = call i32 (ptr, ...) @printf(ptr @19, ptr %print_safe_str8)
  store i32 0, ptr @__global_ev_nid, align 4
  %hev_val10 = load i32, ptr @__global_hev, align 4
  %9 = icmp sgt i32 %hev_val10, 0
  br i1 %9, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %sess_ptr11 = load ptr, ptr @__global_client, align 8
  %ev_ptr = call ptr @aiml_mirror_session_next_event(ptr %sess_ptr11)
  %ev_tmp = alloca %mirror_event, align 8
  %10 = call ptr @memcpy(ptr %ev_tmp, ptr %ev_ptr, i64 24)
  %11 = call ptr @memcpy(ptr @__global_ev, ptr %ev_tmp, i64 24)
  %net_id_val = load i32, ptr getelementptr inbounds (%mirror_event, ptr @__global_ev, i32 0, i32 2), align 4
  store i32 %net_id_val, ptr @__global_ev_nid, align 4
  %ev_nid_val = load i32, ptr @__global_ev_nid, align 4
  %12 = call ptr @__aiml_int_to_string(i32 %ev_nid_val)
  %13 = call ptr @__aiml_str_concat(ptr @20, ptr %12)
  %print_null_check12 = icmp eq ptr %13, null
  %print_safe_str13 = select i1 %print_null_check12, ptr @print_null_str.8, ptr %13
  %print_call14 = call i32 (ptr, ...) @printf(ptr @21, ptr %print_safe_str13)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  store i32 0, ptr @__global_replica, align 4
  store i32 0, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %sess_ptr15 = load ptr, ptr @__global_client, align 8
  %ev_nid_val16 = load i32, ptr @__global_ev_nid, align 4
  call void @aiml_mirror_session_register_id(ptr %sess_ptr15, ptr @__global_replica, i64 8, i32 %ev_nid_val16)
  %ev_nid_val17 = load i32, ptr @__global_ev_nid, align 4
  %14 = call ptr @__aiml_int_to_string(i32 %ev_nid_val17)
  %15 = call ptr @__aiml_str_concat(ptr @22, ptr %14)
  %print_null_check18 = icmp eq ptr %15, null
  %print_safe_str19 = select i1 %print_null_check18, ptr @print_null_str.9, ptr %15
  %print_call20 = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str19)
  %sess_ptr21 = load ptr, ptr @__global_client, align 8
  %16 = call i32 @aiml_mirror_session_peer_count(ptr %sess_ptr21)
  store i32 %16, ptr @__global_pc, align 4
  %pc_val = load i32, ptr @__global_pc, align 4
  %17 = call ptr @__aiml_int_to_string(i32 %pc_val)
  %18 = call ptr @__aiml_str_concat(ptr @24, ptr %17)
  %print_null_check22 = icmp eq ptr %18, null
  %print_safe_str23 = select i1 %print_null_check22, ptr @print_null_str.10, ptr %18
  %print_call24 = call i32 (ptr, ...) @printf(ptr @25, ptr %print_safe_str23)
  call void @time_sleep_ms(i32 100)
  %sess_ptr25 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr25)
  %health_val = load i32, ptr @__global_replica, align 4
  store i32 %health_val, ptr @__global_rh, align 4
  %score_val = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  store i32 %score_val, ptr @__global_rs, align 4
  %rh_val = load i32, ptr @__global_rh, align 4
  %19 = call ptr @__aiml_int_to_string(i32 %rh_val)
  %20 = call ptr @__aiml_str_concat(ptr @26, ptr %19)
  %21 = call ptr @__aiml_str_concat(ptr %20, ptr @27)
  %rs_val = load i32, ptr @__global_rs, align 4
  %22 = call ptr @__aiml_int_to_string(i32 %rs_val)
  %23 = call ptr @__aiml_str_concat(ptr %21, ptr %22)
  %print_null_check26 = icmp eq ptr %23, null
  %print_safe_str27 = select i1 %print_null_check26, ptr @print_null_str.11, ptr %23
  %print_call28 = call i32 (ptr, ...) @printf(ptr @28, ptr %print_safe_str27)
  %rh_val29 = load i32, ptr @__global_rh, align 4
  %24 = icmp eq i32 %rh_val29, 77
  br i1 %24, label %if_then30, label %if_else

if_then30:                                        ; preds = %if_merge
  %print_call32 = call i32 (ptr, ...) @printf(ptr @30, ptr @29)
  br label %if_merge31

if_else:                                          ; preds = %if_merge
  %rh_val33 = load i32, ptr @__global_rh, align 4
  %25 = call ptr @__aiml_int_to_string(i32 %rh_val33)
  %26 = call ptr @__aiml_str_concat(ptr @31, ptr %25)
  %print_null_check34 = icmp eq ptr %26, null
  %print_safe_str35 = select i1 %print_null_check34, ptr @print_null_str.13, ptr %26
  %print_call36 = call i32 (ptr, ...) @printf(ptr @32, ptr %print_safe_str35)
  br label %if_merge31

if_merge31:                                       ; preds = %if_else, %if_then30
  %sess_ptr37 = load ptr, ptr @__global_client, align 8
  %ev_nid_val38 = load i32, ptr @__global_ev_nid, align 4
  call void @aiml_mirror_session_send_rpc(ptr %sess_ptr37, i32 %ev_nid_val38, ptr @33, i32 10)
  %print_call39 = call i32 (ptr, ...) @printf(ptr @35, ptr @34)
  call void @time_sleep_ms(i32 150)
  %sess_ptr40 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr40)
  %health_val41 = load i32, ptr @__global_replica, align 4
  store i32 %health_val41, ptr @__global_rh2, align 4
  %rh2_val = load i32, ptr @__global_rh2, align 4
  %27 = call ptr @__aiml_int_to_string(i32 %rh2_val)
  %28 = call ptr @__aiml_str_concat(ptr @36, ptr %27)
  %print_null_check42 = icmp eq ptr %28, null
  %print_safe_str43 = select i1 %print_null_check42, ptr @print_null_str.15, ptr %28
  %print_call44 = call i32 (ptr, ...) @printf(ptr @37, ptr %print_safe_str43)
  %rh2_val45 = load i32, ptr @__global_rh2, align 4
  %29 = icmp eq i32 %rh2_val45, 67
  br i1 %29, label %if_then46, label %if_else47

if_then46:                                        ; preds = %if_merge31
  %print_call49 = call i32 (ptr, ...) @printf(ptr @39, ptr @38)
  br label %if_merge48

if_else47:                                        ; preds = %if_merge31
  %rh2_val50 = load i32, ptr @__global_rh2, align 4
  %30 = call ptr @__aiml_int_to_string(i32 %rh2_val50)
  %31 = call ptr @__aiml_str_concat(ptr @40, ptr %30)
  %print_null_check51 = icmp eq ptr %31, null
  %print_safe_str52 = select i1 %print_null_check51, ptr @print_null_str.17, ptr %31
  %print_call53 = call i32 (ptr, ...) @printf(ptr @41, ptr %print_safe_str52)
  br label %if_merge48

if_merge48:                                       ; preds = %if_else47, %if_then46
  %sess_ptr54 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr54)
  %handle_ptr = load ptr, ptr @__global_host_th, align 8
  %detached_ptr55 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr55, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

join_detached_throw:                              ; preds = %if_merge48
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %if_merge48
  %joined_ptr56 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr56, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc57 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc57, align 8
  call void @__cxa_throw(ptr %exc_alloc57, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %32 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr56, align 1
  %print_call58 = call i32 (ptr, ...) @printf(ptr @43, ptr @42)
  ret void
}

declare ptr @aiml_mirror_listen(ptr)

declare i32 @printf(ptr, ...)

declare void @aiml_mirror_session_close(ptr)

declare i32 @__gxx_personality_v0(...)

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

declare void @aiml_mirror_session_update(ptr)

declare void @time_sleep_ms(i32)

declare i32 @aiml_mirror_session_has_rpc(ptr)

declare ptr @aiml_mirror_session_next_rpc(ptr)

declare ptr @memcpy(ptr, ptr, i64)

define internal ptr @__aiml_spawn_mirror_host_fn(ptr %0) {
entry:
  call void @mirror_host_fn()
  ret ptr null
}

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare ptr @aiml_mirror_connect_timeout(ptr, i32)

declare i32 @aiml_mirror_session_is_connected(ptr)

declare i32 @aiml_mirror_session_has_event(ptr)

declare ptr @aiml_mirror_session_next_event(ptr)

declare void @aiml_mirror_session_register_id(ptr, ptr, i64, i32)

declare i32 @aiml_mirror_session_peer_count(ptr)

declare void @aiml_mirror_session_send_rpc(ptr, i32, ptr, i32)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

attributes #0 = { noinline }
