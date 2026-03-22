; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%GameViewWs9 = type { i32, ptr, ptr, i32, i32, i32 }
%ClientInfoWs9 = type { ptr, ptr, ptr }
%AimlClassSchema = type { ptr, i32, i32, i32, i32, [32 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%mirror_event = type { ptr, ptr, i32 }
%thread_handle = type { i64, i8, i8 }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_GameViewWs9 = private unnamed_addr constant [7 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 6, i32 1, i32 0, i32 40, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 16, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 24, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 28, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 32, i32 0, i32 0 }]
@__aiml_sernodes_ClientInfoWs9 = private unnamed_addr constant [4 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 3, i32 1, i32 0, i32 24, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 16, i32 0, i32 0 }]
@__lib_g_ws9_view = external global %GameViewWs9
@__lib_g_ws9_view_nid = external global i32
@__lib_g_ws9_client_info = external global %ClientInfoWs9
@__lib_g_ws9_selected_deckset_id = external global ptr
@__lib_g_ws9_connected = external global i32
@__lib_g_ws9_server_client_info = external global %ClientInfoWs9
@__lib_g_ws9_server_seen_client_info = external global i32
@0 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19897\00", align 1
@1 = private unnamed_addr constant [34 x i8] c"ws9-host: view registered net_id=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@2 = private unnamed_addr constant [6 x i8] c" p1='\00", align 1
@3 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [15 x i8] c"peer_connected\00", align 1
@6 = private unnamed_addr constant [18 x i8] c"object_registered\00", align 1
@7 = private unnamed_addr constant [31 x i8] c"ws9-host: client_info player='\00", align 1
@8 = private unnamed_addr constant [12 x i8] c"' deckset='\00", align 1
@9 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"Bot\00", align 1
@12 = private unnamed_addr constant [27 x i8] c"ws9-host: filled view p1='\00", align 1
@13 = private unnamed_addr constant [9 x i8] c"' round=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [14 x i8] c"deckset-alpha\00", align 1
@16 = private unnamed_addr constant [38 x i8] c"ws9-client: selected before connect='\00", align 1
@17 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19897\00", align 1
@20 = private unnamed_addr constant [22 x i8] c"ws9-client: connected\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [18 x i8] c"object_registered\00", align 1
@23 = private unnamed_addr constant [43 x i8] c"ws9-client: view object_registered net_id=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [40 x i8] c"ws9-client: after register_id deckset='\00", align 1
@26 = private unnamed_addr constant [7 x i8] c"' p1='\00", align 1
@27 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@29 = private unnamed_addr constant [8 x i8] c"Nikolai\00", align 1
@30 = private unnamed_addr constant [10 x i8] c"token-123\00", align 1
@31 = private unnamed_addr constant [45 x i8] c"ws9-client: client_info registered deckset='\00", align 1
@32 = private unnamed_addr constant [11 x i8] c"' player='\00", align 1
@33 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@35 = private unnamed_addr constant [25 x i8] c"ws9-client: drained p1='\00", align 1
@36 = private unnamed_addr constant [9 x i8] c"' round=\00", align 1
@37 = private unnamed_addr constant [12 x i8] c" connected=\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [24 x i8] c"ws9-check: server_seen=\00", align 1
@40 = private unnamed_addr constant [12 x i8] c" selected='\00", align 1
@41 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@42 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@43 = private unnamed_addr constant [24 x i8] c"FAIL ws9: no view event\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [14 x i8] c"deckset-alpha\00", align 1
@46 = private unnamed_addr constant [34 x i8] c"FAIL ws9: selected deckset lost='\00", align 1
@47 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@49 = private unnamed_addr constant [14 x i8] c"deckset-alpha\00", align 1
@50 = private unnamed_addr constant [32 x i8] c"FAIL ws9: client_info deckset='\00", align 1
@51 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@53 = private unnamed_addr constant [14 x i8] c"deckset-alpha\00", align 1
@54 = private unnamed_addr constant [39 x i8] c"FAIL ws9: server client_info deckset='\00", align 1
@55 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@57 = private unnamed_addr constant [8 x i8] c"Nikolai\00", align 1
@58 = private unnamed_addr constant [38 x i8] c"FAIL ws9: server client_info player='\00", align 1
@59 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@60 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@61 = private unnamed_addr constant [8 x i8] c"Nikolai\00", align 1
@62 = private unnamed_addr constant [25 x i8] c"FAIL ws9: view p1_name='\00", align 1
@63 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@64 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@65 = private unnamed_addr constant [22 x i8] c"FAIL ws9: view round=\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@66 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@67 = private unnamed_addr constant [27 x i8] c"FAIL ws9: view hand_count=\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@68 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@69 = private unnamed_addr constant [21 x i8] c"FAIL ws9: connected=\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@70 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@71 = private unnamed_addr constant [23 x i8] c"WS 9rooms imports PASS\00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@72 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [6 x [16 x ptr]] [[16 x ptr] [ptr @ws9client.set_selected_deckset, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @ws9client.get_selected_deckset, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @ws9client.get_connected, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @ws9server.mark_client_info_seen, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @ws9server.client_info_seen, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @ws9_host, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [6 x ptr] [ptr @ws9client.set_selected_deckset, ptr @ws9client.get_selected_deckset, ptr @ws9client.get_connected, ptr @ws9server.mark_client_info_seen, ptr @ws9server.client_info_seen, ptr @ws9_host]
@__aiml_fptable_versions = global [6 x i32] [i32 1, i32 1, i32 1, i32 1, i32 1, i32 1]
@__aiml_fname_ws9client.set_selected_deckset = private constant [31 x i8] c"ws9client.set_selected_deckset\00"
@__aiml_fname_ws9client.get_selected_deckset = private constant [31 x i8] c"ws9client.get_selected_deckset\00"
@__aiml_fname_ws9client.get_connected = private constant [24 x i8] c"ws9client.get_connected\00"
@__aiml_fname_ws9server.mark_client_info_seen = private constant [32 x i8] c"ws9server.mark_client_info_seen\00"
@__aiml_fname_ws9server.client_info_seen = private constant [27 x i8] c"ws9server.client_info_seen\00"
@__aiml_fname_ws9_host = private constant [9 x i8] c"ws9_host\00"
@__aiml_func_names = constant [6 x ptr] [ptr @__aiml_fname_ws9client.set_selected_deckset, ptr @__aiml_fname_ws9client.get_selected_deckset, ptr @__aiml_fname_ws9client.get_connected, ptr @__aiml_fname_ws9server.mark_client_info_seen, ptr @__aiml_fname_ws9server.client_info_seen, ptr @__aiml_fname_ws9_host]
@__aiml_func_count = constant i32 6
@__aiml_sname_ClientInfoWs9 = private constant [14 x i8] c"ClientInfoWs9\00"
@__aiml_sfield_ClientInfoWs9_deckset_id = private constant [11 x i8] c"deckset_id\00"
@__aiml_sfield_ClientInfoWs9_player_name = private constant [12 x i8] c"player_name\00"
@__aiml_sfield_ClientInfoWs9_player_id = private constant [10 x i8] c"player_id\00"
@__aiml_sname_GameViewWs9 = private constant [12 x i8] c"GameViewWs9\00"
@__aiml_sfield_GameViewWs9_round = private constant [6 x i8] c"round\00"
@__aiml_sfield_GameViewWs9_p1_name = private constant [8 x i8] c"p1_name\00"
@__aiml_sfield_GameViewWs9_p2_name = private constant [8 x i8] c"p2_name\00"
@__aiml_sfield_GameViewWs9_hand_count = private constant [11 x i8] c"hand_count\00"
@__aiml_sfield_GameViewWs9_p1_hp = private constant [6 x i8] c"p1_hp\00"
@__aiml_sfield_GameViewWs9_p2_hp = private constant [6 x i8] c"p2_hp\00"
@__aiml_schemas = global [2 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_ClientInfoWs9, i32 1, i32 3, i32 24, i32 32, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_ClientInfoWs9_deckset_id }, %AimlFieldInfo { ptr @__aiml_sfield_ClientInfoWs9_player_name }, %AimlFieldInfo { ptr @__aiml_sfield_ClientInfoWs9_player_id }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_GameViewWs9, i32 1, i32 6, i32 40, i32 64, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_GameViewWs9_round }, %AimlFieldInfo { ptr @__aiml_sfield_GameViewWs9_p1_name }, %AimlFieldInfo { ptr @__aiml_sfield_GameViewWs9_p2_name }, %AimlFieldInfo { ptr @__aiml_sfield_GameViewWs9_hand_count }, %AimlFieldInfo { ptr @__aiml_sfield_GameViewWs9_p1_hp }, %AimlFieldInfo { ptr @__aiml_sfield_GameViewWs9_p2_hp }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 2

declare void @ws9client.set_selected_deckset(ptr)

declare ptr @ws9client.get_selected_deckset()

declare i32 @ws9client.get_connected()

declare void @ws9server.mark_client_info_seen()

declare i32 @ws9server.client_info_seen()

define void @ws9_host() personality ptr @__gxx_personality_v0 {
entry:
  %sess = alloca ptr, align 8
  store ptr null, ptr %sess, align 8
  %0 = call ptr @aiml_mirror_listen(ptr @0)
  store ptr %0, ptr %sess, align 8
  %v = alloca %GameViewWs9, align 8
  store %GameViewWs9 zeroinitializer, ptr %v, align 8
  %nid = alloca i32, align 4
  store i32 0, ptr %nid, align 4
  %sess_ptr = load ptr, ptr %sess, align 8
  %1 = call i32 @aiml_mirror_session_register_typed(ptr %sess_ptr, ptr %v, i64 40, ptr @__aiml_sernodes_GameViewWs9, i32 7)
  store i32 %1, ptr %nid, align 4
  %nid_val = load i32, ptr %nid, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %nid_val)
  %3 = call ptr @__aiml_str_concat(ptr @1, ptr %2)
  %4 = call ptr @__aiml_str_concat(ptr %3, ptr @2)
  %prop_ptr = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 1
  %p1_name_val = load ptr, ptr %prop_ptr, align 8
  %5 = call ptr @__aiml_str_concat(ptr %4, ptr %p1_name_val)
  %6 = call ptr @__aiml_str_concat(ptr %5, ptr @3)
  %print_null_check = icmp eq ptr %6, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %6
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr %print_safe_str)
  %got_peer = alloca i32, align 4
  store i32 0, ptr %got_peer, align 4
  store i32 0, ptr %got_peer, align 4
  %peer_wait = alloca i32, align 4
  store i32 0, ptr %peer_wait, align 4
  store i32 0, ptr %peer_wait, align 4
  br label %while_loop

while_loop:                                       ; preds = %if_merge9, %entry
  %got_peer_val = load i32, ptr %got_peer, align 4
  %7 = icmp eq i32 %got_peer_val, 0
  %peer_wait_val = load i32, ptr %peer_wait, align 4
  %8 = icmp slt i32 %peer_wait_val, 200
  %9 = and i1 %7, %8
  br i1 %9, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %sess_ptr1 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr1)
  %sess_ptr2 = load ptr, ptr %sess, align 8
  %10 = call i32 @aiml_mirror_session_has_event(ptr %sess_ptr2)
  %to_bool = icmp ne i32 %10, 0
  br i1 %to_bool, label %if_then, label %if_merge

while_end:                                        ; preds = %while_loop
  %got_ci = alloca i32, align 4
  store i32 0, ptr %got_ci, align 4
  store i32 0, ptr %got_ci, align 4
  %ci_wait = alloca i32, align 4
  store i32 0, ptr %ci_wait, align 4
  store i32 0, ptr %ci_wait, align 4
  br label %while_loop11

if_then:                                          ; preds = %while_body
  %ev = alloca %mirror_event, align 8
  store %mirror_event zeroinitializer, ptr %ev, align 8
  %sess_ptr3 = load ptr, ptr %sess, align 8
  %ev_ptr = call ptr @aiml_mirror_session_next_event(ptr %sess_ptr3)
  %ev_tmp = alloca %mirror_event, align 8
  %11 = call ptr @memcpy(ptr %ev_tmp, ptr %ev_ptr, i64 24)
  %12 = call ptr @memcpy(ptr %ev, ptr %ev_tmp, i64 24)
  %prop_ptr4 = getelementptr inbounds %mirror_event, ptr %ev, i32 0, i32 0
  %type_val = load ptr, ptr %prop_ptr4, align 8
  %13 = icmp eq ptr %type_val, null
  %14 = icmp eq ptr %type_val, @5
  %15 = or i1 %13, false
  br i1 %15, label %cmp_check_null, label %cmp_do_strcmp

if_merge:                                         ; preds = %if_merge6, %while_body
  %got_peer_val7 = load i32, ptr %got_peer, align 4
  %16 = icmp eq i32 %got_peer_val7, 0
  br i1 %16, label %if_then8, label %if_merge9

cmp_check_null:                                   ; preds = %if_then
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %if_then
  %17 = call i32 @strcmp(ptr %type_val, ptr @5)
  %18 = icmp eq i32 %17, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %14, %cmp_check_null ], [ %18, %cmp_do_strcmp ]
  br i1 %strcmp_res, label %if_then5, label %if_merge6

if_then5:                                         ; preds = %cmp_merge
  store i32 1, ptr %got_peer, align 4
  br label %if_merge6

if_merge6:                                        ; preds = %if_then5, %cmp_merge
  br label %if_merge

if_then8:                                         ; preds = %if_merge
  call void @time_sleep_ms(i32 16)
  %peer_wait_val10 = load i32, ptr %peer_wait, align 4
  %19 = add i32 %peer_wait_val10, 1
  store i32 %19, ptr %peer_wait, align 4
  br label %if_merge9

if_merge9:                                        ; preds = %if_then8, %if_merge
  br label %while_loop

while_loop11:                                     ; preds = %if_merge42, %while_end
  %got_ci_val = load i32, ptr %got_ci, align 4
  %20 = icmp eq i32 %got_ci_val, 0
  %ci_wait_val = load i32, ptr %ci_wait, align 4
  %21 = icmp slt i32 %ci_wait_val, 300
  %22 = and i1 %20, %21
  br i1 %22, label %while_body12, label %while_end13

while_body12:                                     ; preds = %while_loop11
  %sess_ptr14 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr14)
  %sess_ptr15 = load ptr, ptr %sess, align 8
  %23 = call i32 @aiml_mirror_session_has_event(ptr %sess_ptr15)
  %to_bool16 = icmp ne i32 %23, 0
  br i1 %to_bool16, label %if_then17, label %if_merge18

while_end13:                                      ; preds = %while_loop11
  %set_prop_ptr = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 0
  store i32 7, ptr %set_prop_ptr, align 4
  %player_name_val44 = load ptr, ptr getelementptr inbounds (%ClientInfoWs9, ptr @__lib_g_ws9_server_client_info, i32 0, i32 1), align 8
  %set_prop_ptr45 = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 1
  store ptr %player_name_val44, ptr %set_prop_ptr45, align 8
  %set_prop_ptr46 = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 2
  store ptr @11, ptr %set_prop_ptr46, align 8
  %set_prop_ptr47 = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 3
  store i32 3, ptr %set_prop_ptr47, align 4
  %set_prop_ptr48 = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 4
  store i32 26, ptr %set_prop_ptr48, align 4
  %set_prop_ptr49 = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 5
  store i32 22, ptr %set_prop_ptr49, align 4
  %prop_ptr50 = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 1
  %p1_name_val51 = load ptr, ptr %prop_ptr50, align 8
  %24 = call ptr @__aiml_str_concat(ptr @12, ptr %p1_name_val51)
  %25 = call ptr @__aiml_str_concat(ptr %24, ptr @13)
  %prop_ptr52 = getelementptr inbounds %GameViewWs9, ptr %v, i32 0, i32 0
  %round_val = load i32, ptr %prop_ptr52, align 4
  %26 = call ptr @__aiml_int_to_string(i32 %round_val)
  %27 = call ptr @__aiml_str_concat(ptr %25, ptr %26)
  %print_null_check53 = icmp eq ptr %27, null
  %print_safe_str54 = select i1 %print_null_check53, ptr @print_null_str.2, ptr %27
  %print_call55 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str54)
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  store i32 0, ptr %i, align 4
  br label %while_loop56

if_then17:                                        ; preds = %while_body12
  %cev = alloca %mirror_event, align 8
  store %mirror_event zeroinitializer, ptr %cev, align 8
  %sess_ptr19 = load ptr, ptr %sess, align 8
  %ev_ptr20 = call ptr @aiml_mirror_session_next_event(ptr %sess_ptr19)
  %ev_tmp21 = alloca %mirror_event, align 8
  %28 = call ptr @memcpy(ptr %ev_tmp21, ptr %ev_ptr20, i64 24)
  %29 = call ptr @memcpy(ptr %cev, ptr %ev_tmp21, i64 24)
  %prop_ptr22 = getelementptr inbounds %mirror_event, ptr %cev, i32 0, i32 0
  %type_val23 = load ptr, ptr %prop_ptr22, align 8
  %30 = icmp eq ptr %type_val23, null
  %31 = icmp eq ptr %type_val23, @6
  %32 = or i1 %30, false
  br i1 %32, label %cmp_check_null24, label %cmp_do_strcmp25

if_merge18:                                       ; preds = %if_merge31, %while_body12
  %got_ci_val40 = load i32, ptr %got_ci, align 4
  %33 = icmp eq i32 %got_ci_val40, 0
  br i1 %33, label %if_then41, label %if_merge42

cmp_check_null24:                                 ; preds = %if_then17
  br label %cmp_merge26

cmp_do_strcmp25:                                  ; preds = %if_then17
  %34 = call i32 @strcmp(ptr %type_val23, ptr @6)
  %35 = icmp eq i32 %34, 0
  br label %cmp_merge26

cmp_merge26:                                      ; preds = %cmp_do_strcmp25, %cmp_check_null24
  %strcmp_res27 = phi i1 [ %31, %cmp_check_null24 ], [ %35, %cmp_do_strcmp25 ]
  %prop_ptr28 = getelementptr inbounds %mirror_event, ptr %cev, i32 0, i32 2
  %net_id_val = load i32, ptr %prop_ptr28, align 4
  %nid_val29 = load i32, ptr %nid, align 4
  %36 = icmp ne i32 %net_id_val, %nid_val29
  %37 = and i1 %strcmp_res27, %36
  br i1 %37, label %if_then30, label %if_merge31

if_then30:                                        ; preds = %cmp_merge26
  %sess_ptr32 = load ptr, ptr %sess, align 8
  %prop_ptr33 = getelementptr inbounds %mirror_event, ptr %cev, i32 0, i32 2
  %net_id_val34 = load i32, ptr %prop_ptr33, align 4
  call void @aiml_mirror_session_register_id_typed(ptr %sess_ptr32, ptr @__lib_g_ws9_server_client_info, i64 24, i32 %net_id_val34, ptr @__aiml_sernodes_ClientInfoWs9, i32 4)
  call void @time_sleep_ms(i32 16)
  %sess_ptr35 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr35)
  %sess_ptr36 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr36)
  call void @ws9server.mark_client_info_seen()
  store i32 1, ptr %got_ci, align 4
  %player_name_val = load ptr, ptr getelementptr inbounds (%ClientInfoWs9, ptr @__lib_g_ws9_server_client_info, i32 0, i32 1), align 8
  %38 = call ptr @__aiml_str_concat(ptr @7, ptr %player_name_val)
  %39 = call ptr @__aiml_str_concat(ptr %38, ptr @8)
  %deckset_id_val = load ptr, ptr @__lib_g_ws9_server_client_info, align 8
  %40 = call ptr @__aiml_str_concat(ptr %39, ptr %deckset_id_val)
  %41 = call ptr @__aiml_str_concat(ptr %40, ptr @9)
  %print_null_check37 = icmp eq ptr %41, null
  %print_safe_str38 = select i1 %print_null_check37, ptr @print_null_str.1, ptr %41
  %print_call39 = call i32 (ptr, ...) @printf(ptr @10, ptr %print_safe_str38)
  br label %if_merge31

if_merge31:                                       ; preds = %if_then30, %cmp_merge26
  br label %if_merge18

if_then41:                                        ; preds = %if_merge18
  call void @time_sleep_ms(i32 16)
  %ci_wait_val43 = load i32, ptr %ci_wait, align 4
  %42 = add i32 %ci_wait_val43, 1
  store i32 %42, ptr %ci_wait, align 4
  br label %if_merge42

if_merge42:                                       ; preds = %if_then41, %if_merge18
  br label %while_loop11

while_loop56:                                     ; preds = %while_body57, %while_end13
  %i_val = load i32, ptr %i, align 4
  %43 = icmp slt i32 %i_val, 40
  br i1 %43, label %while_body57, label %while_end58

while_body57:                                     ; preds = %while_loop56
  %sess_ptr59 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr59)
  call void @time_sleep_ms(i32 16)
  %i_val60 = load i32, ptr %i, align 4
  %44 = add i32 %i_val60, 1
  store i32 %44, ptr %i, align 4
  br label %while_loop56

while_end58:                                      ; preds = %while_loop56
  %sess_ptr61 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr61)
  ret void
}

define void @__aiml_init() {
entry:
  call void @__aiml_lib_init_tests_ws9_shared_aiml()
  call void @__aiml_lib_init_tests_ws9_client_globals_aiml()
  call void @__aiml_lib_init_tests_ws9_server_globals_aiml()
  call void @ws9client.set_selected_deckset(ptr @15)
  %selected0 = alloca ptr, align 8
  store ptr null, ptr %selected0, align 8
  %0 = call ptr @ws9client.get_selected_deckset()
  store ptr %0, ptr %selected0, align 8
  %selected0_val = load ptr, ptr %selected0, align 8
  %1 = call ptr @__aiml_str_concat(ptr @16, ptr %selected0_val)
  %2 = call ptr @__aiml_str_concat(ptr %1, ptr @17)
  %print_null_check = icmp eq ptr %2, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.3, ptr %2
  %print_call = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str)
  %host_th = alloca %thread_handle, align 8
  store %thread_handle zeroinitializer, ptr %host_th, align 8
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %3 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_ws9_host, ptr null)
  store ptr %handle_raw, ptr %host_th, align 8
  call void @time_sleep_ms(i32 80)
  %client = alloca ptr, align 8
  store ptr null, ptr %client, align 8
  %4 = call ptr @aiml_mirror_connect_timeout(ptr @19, i32 4000)
  store ptr %4, ptr %client, align 8
  %print_call1 = call i32 (ptr, ...) @printf(ptr @21, ptr @20)
  %got_view = alloca i32, align 4
  store i32 0, ptr %got_view, align 4
  store i32 0, ptr %got_view, align 4
  %wait = alloca i32, align 4
  store i32 0, ptr %wait, align 4
  store i32 0, ptr %wait, align 4
  br label %while_loop

while_loop:                                       ; preds = %if_merge12, %entry
  %got_view_val = load i32, ptr %got_view, align 4
  %5 = icmp eq i32 %got_view_val, 0
  %wait_val = load i32, ptr %wait, align 4
  %6 = icmp slt i32 %wait_val, 300
  %7 = and i1 %5, %6
  br i1 %7, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %sess_ptr = load ptr, ptr %client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr)
  %sess_ptr2 = load ptr, ptr %client, align 8
  %8 = call i32 @aiml_mirror_session_has_event(ptr %sess_ptr2)
  %to_bool = icmp ne i32 %8, 0
  br i1 %to_bool, label %if_then, label %if_merge

while_end:                                        ; preds = %while_loop
  %sess_ptr14 = load ptr, ptr %client, align 8
  %g_ws9_view_nid_val15 = load i32, ptr @__lib_g_ws9_view_nid, align 4
  call void @aiml_mirror_session_register_id_typed(ptr %sess_ptr14, ptr @__lib_g_ws9_view, i64 40, i32 %g_ws9_view_nid_val15, ptr @__aiml_sernodes_GameViewWs9, i32 7)
  %g_ws9_selected_deckset_id_val = load ptr, ptr @__lib_g_ws9_selected_deckset_id, align 8
  %9 = call ptr @__aiml_str_concat(ptr @25, ptr %g_ws9_selected_deckset_id_val)
  %10 = call ptr @__aiml_str_concat(ptr %9, ptr @26)
  %p1_name_val = load ptr, ptr getelementptr inbounds (%GameViewWs9, ptr @__lib_g_ws9_view, i32 0, i32 1), align 8
  %11 = call ptr @__aiml_str_concat(ptr %10, ptr %p1_name_val)
  %12 = call ptr @__aiml_str_concat(ptr %11, ptr @27)
  %print_null_check16 = icmp eq ptr %12, null
  %print_safe_str17 = select i1 %print_null_check16, ptr @print_null_str.6, ptr %12
  %print_call18 = call i32 (ptr, ...) @printf(ptr @28, ptr %print_safe_str17)
  %g_ws9_selected_deckset_id_val19 = load ptr, ptr @__lib_g_ws9_selected_deckset_id, align 8
  store ptr %g_ws9_selected_deckset_id_val19, ptr @__lib_g_ws9_client_info, align 8
  store ptr @29, ptr getelementptr inbounds (%ClientInfoWs9, ptr @__lib_g_ws9_client_info, i32 0, i32 1), align 8
  store ptr @30, ptr getelementptr inbounds (%ClientInfoWs9, ptr @__lib_g_ws9_client_info, i32 0, i32 2), align 8
  %sess_ptr20 = load ptr, ptr %client, align 8
  %13 = call i32 @aiml_mirror_session_register_typed(ptr %sess_ptr20, ptr @__lib_g_ws9_client_info, i64 24, ptr @__aiml_sernodes_ClientInfoWs9, i32 4)
  %deckset_id_val = load ptr, ptr @__lib_g_ws9_client_info, align 8
  %14 = call ptr @__aiml_str_concat(ptr @31, ptr %deckset_id_val)
  %15 = call ptr @__aiml_str_concat(ptr %14, ptr @32)
  %player_name_val = load ptr, ptr getelementptr inbounds (%ClientInfoWs9, ptr @__lib_g_ws9_client_info, i32 0, i32 1), align 8
  %16 = call ptr @__aiml_str_concat(ptr %15, ptr %player_name_val)
  %17 = call ptr @__aiml_str_concat(ptr %16, ptr @33)
  %print_null_check21 = icmp eq ptr %17, null
  %print_safe_str22 = select i1 %print_null_check21, ptr @print_null_str.7, ptr %17
  %print_call23 = call i32 (ptr, ...) @printf(ptr @34, ptr %print_safe_str22)
  %drain_wait = alloca i32, align 4
  store i32 0, ptr %drain_wait, align 4
  store i32 0, ptr %drain_wait, align 4
  br label %while_loop24

if_then:                                          ; preds = %while_body
  %ev = alloca %mirror_event, align 8
  store %mirror_event zeroinitializer, ptr %ev, align 8
  %sess_ptr3 = load ptr, ptr %client, align 8
  %ev_ptr = call ptr @aiml_mirror_session_next_event(ptr %sess_ptr3)
  %ev_tmp = alloca %mirror_event, align 8
  %18 = call ptr @memcpy(ptr %ev_tmp, ptr %ev_ptr, i64 24)
  %19 = call ptr @memcpy(ptr %ev, ptr %ev_tmp, i64 24)
  %prop_ptr = getelementptr inbounds %mirror_event, ptr %ev, i32 0, i32 0
  %type_val = load ptr, ptr %prop_ptr, align 8
  %20 = icmp eq ptr %type_val, null
  %21 = icmp eq ptr %type_val, @22
  %22 = or i1 %20, false
  br i1 %22, label %cmp_check_null, label %cmp_do_strcmp

if_merge:                                         ; preds = %if_merge5, %while_body
  %got_view_val10 = load i32, ptr %got_view, align 4
  %23 = icmp eq i32 %got_view_val10, 0
  br i1 %23, label %if_then11, label %if_merge12

cmp_check_null:                                   ; preds = %if_then
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %if_then
  %24 = call i32 @strcmp(ptr %type_val, ptr @22)
  %25 = icmp eq i32 %24, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %21, %cmp_check_null ], [ %25, %cmp_do_strcmp ]
  br i1 %strcmp_res, label %if_then4, label %if_merge5

if_then4:                                         ; preds = %cmp_merge
  %prop_ptr6 = getelementptr inbounds %mirror_event, ptr %ev, i32 0, i32 2
  %net_id_val = load i32, ptr %prop_ptr6, align 4
  store i32 %net_id_val, ptr @__lib_g_ws9_view_nid, align 4
  store i32 1, ptr %got_view, align 4
  %g_ws9_view_nid_val = load i32, ptr @__lib_g_ws9_view_nid, align 4
  %26 = call ptr @__aiml_int_to_string(i32 %g_ws9_view_nid_val)
  %27 = call ptr @__aiml_str_concat(ptr @23, ptr %26)
  %print_null_check7 = icmp eq ptr %27, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.5, ptr %27
  %print_call9 = call i32 (ptr, ...) @printf(ptr @24, ptr %print_safe_str8)
  br label %if_merge5

if_merge5:                                        ; preds = %if_then4, %cmp_merge
  br label %if_merge

if_then11:                                        ; preds = %if_merge
  call void @time_sleep_ms(i32 16)
  %wait_val13 = load i32, ptr %wait, align 4
  %28 = add i32 %wait_val13, 1
  store i32 %28, ptr %wait, align 4
  br label %if_merge12

if_merge12:                                       ; preds = %if_then11, %if_merge
  br label %while_loop

while_loop24:                                     ; preds = %while_body25, %while_end
  %29 = load ptr, ptr getelementptr inbounds (%GameViewWs9, ptr @__lib_g_ws9_view, i32 0, i32 1), align 8
  %str_is_null = icmp eq ptr %29, null
  br i1 %str_is_null, label %str_len_null, label %str_len_ok

while_body25:                                     ; preds = %str_len_merge
  %sess_ptr27 = load ptr, ptr %client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr27)
  call void @time_sleep_ms(i32 16)
  %drain_wait_val28 = load i32, ptr %drain_wait, align 4
  %30 = add i32 %drain_wait_val28, 1
  store i32 %30, ptr %drain_wait, align 4
  br label %while_loop24

while_end26:                                      ; preds = %str_len_merge
  store i32 1, ptr @__lib_g_ws9_connected, align 4
  %connected1 = alloca i32, align 4
  store i32 0, ptr %connected1, align 4
  %31 = call i32 @ws9client.get_connected()
  store i32 %31, ptr %connected1, align 4
  %server_seen = alloca i32, align 4
  store i32 0, ptr %server_seen, align 4
  %32 = call i32 @ws9server.client_info_seen()
  store i32 %32, ptr %server_seen, align 4
  %p1_name_val29 = load ptr, ptr getelementptr inbounds (%GameViewWs9, ptr @__lib_g_ws9_view, i32 0, i32 1), align 8
  %33 = call ptr @__aiml_str_concat(ptr @35, ptr %p1_name_val29)
  %34 = call ptr @__aiml_str_concat(ptr %33, ptr @36)
  %round_val = load i32, ptr @__lib_g_ws9_view, align 4
  %35 = call ptr @__aiml_int_to_string(i32 %round_val)
  %36 = call ptr @__aiml_str_concat(ptr %34, ptr %35)
  %37 = call ptr @__aiml_str_concat(ptr %36, ptr @37)
  %connected1_val = load i32, ptr %connected1, align 4
  %38 = call ptr @__aiml_int_to_string(i32 %connected1_val)
  %39 = call ptr @__aiml_str_concat(ptr %37, ptr %38)
  %print_null_check30 = icmp eq ptr %39, null
  %print_safe_str31 = select i1 %print_null_check30, ptr @print_null_str.8, ptr %39
  %print_call32 = call i32 (ptr, ...) @printf(ptr @38, ptr %print_safe_str31)
  %server_seen_val = load i32, ptr %server_seen, align 4
  %40 = call ptr @__aiml_int_to_string(i32 %server_seen_val)
  %41 = call ptr @__aiml_str_concat(ptr @39, ptr %40)
  %42 = call ptr @__aiml_str_concat(ptr %41, ptr @40)
  %g_ws9_selected_deckset_id_val33 = load ptr, ptr @__lib_g_ws9_selected_deckset_id, align 8
  %43 = call ptr @__aiml_str_concat(ptr %42, ptr %g_ws9_selected_deckset_id_val33)
  %44 = call ptr @__aiml_str_concat(ptr %43, ptr @41)
  %print_null_check34 = icmp eq ptr %44, null
  %print_safe_str35 = select i1 %print_null_check34, ptr @print_null_str.9, ptr %44
  %print_call36 = call i32 (ptr, ...) @printf(ptr @42, ptr %print_safe_str35)
  %got_view_val37 = load i32, ptr %got_view, align 4
  %45 = icmp eq i32 %got_view_val37, 0
  br i1 %45, label %if_then38, label %if_merge39

str_len_null:                                     ; preds = %while_loop24
  br label %str_len_merge

str_len_ok:                                       ; preds = %while_loop24
  %46 = call i64 @strlen(ptr %29)
  %47 = trunc i64 %46 to i32
  br label %str_len_merge

str_len_merge:                                    ; preds = %str_len_ok, %str_len_null
  %str_len = phi i32 [ 0, %str_len_null ], [ %47, %str_len_ok ]
  %48 = icmp eq i32 %str_len, 0
  %drain_wait_val = load i32, ptr %drain_wait, align 4
  %49 = icmp slt i32 %drain_wait_val, 300
  %50 = and i1 %48, %49
  br i1 %50, label %while_body25, label %while_end26

if_then38:                                        ; preds = %while_end26
  %print_call40 = call i32 (ptr, ...) @printf(ptr @44, ptr @43)
  call void @exit(i32 1)
  br label %if_merge39

if_merge39:                                       ; preds = %if_then38, %while_end26
  %g_ws9_selected_deckset_id_val41 = load ptr, ptr @__lib_g_ws9_selected_deckset_id, align 8
  %51 = icmp eq ptr %g_ws9_selected_deckset_id_val41, null
  %52 = icmp eq ptr %g_ws9_selected_deckset_id_val41, @45
  %53 = or i1 %51, false
  br i1 %53, label %cmp_check_null42, label %cmp_do_strcmp43

cmp_check_null42:                                 ; preds = %if_merge39
  br label %cmp_merge44

cmp_do_strcmp43:                                  ; preds = %if_merge39
  %54 = call i32 @strcmp(ptr %g_ws9_selected_deckset_id_val41, ptr @45)
  %55 = icmp eq i32 %54, 0
  br label %cmp_merge44

cmp_merge44:                                      ; preds = %cmp_do_strcmp43, %cmp_check_null42
  %strcmp_res45 = phi i1 [ %52, %cmp_check_null42 ], [ %55, %cmp_do_strcmp43 ]
  %56 = sext i1 %strcmp_res45 to i32
  %57 = icmp eq i32 %56, 0
  br i1 %57, label %if_then46, label %if_merge47

if_then46:                                        ; preds = %cmp_merge44
  %g_ws9_selected_deckset_id_val48 = load ptr, ptr @__lib_g_ws9_selected_deckset_id, align 8
  %58 = call ptr @__aiml_str_concat(ptr @46, ptr %g_ws9_selected_deckset_id_val48)
  %59 = call ptr @__aiml_str_concat(ptr %58, ptr @47)
  %print_null_check49 = icmp eq ptr %59, null
  %print_safe_str50 = select i1 %print_null_check49, ptr @print_null_str.11, ptr %59
  %print_call51 = call i32 (ptr, ...) @printf(ptr @48, ptr %print_safe_str50)
  call void @exit(i32 1)
  br label %if_merge47

if_merge47:                                       ; preds = %if_then46, %cmp_merge44
  %deckset_id_val52 = load ptr, ptr @__lib_g_ws9_client_info, align 8
  %60 = icmp eq ptr %deckset_id_val52, null
  %61 = icmp eq ptr %deckset_id_val52, @49
  %62 = or i1 %60, false
  br i1 %62, label %cmp_check_null53, label %cmp_do_strcmp54

cmp_check_null53:                                 ; preds = %if_merge47
  br label %cmp_merge55

cmp_do_strcmp54:                                  ; preds = %if_merge47
  %63 = call i32 @strcmp(ptr %deckset_id_val52, ptr @49)
  %64 = icmp eq i32 %63, 0
  br label %cmp_merge55

cmp_merge55:                                      ; preds = %cmp_do_strcmp54, %cmp_check_null53
  %strcmp_res56 = phi i1 [ %61, %cmp_check_null53 ], [ %64, %cmp_do_strcmp54 ]
  %65 = sext i1 %strcmp_res56 to i32
  %66 = icmp eq i32 %65, 0
  br i1 %66, label %if_then57, label %if_merge58

if_then57:                                        ; preds = %cmp_merge55
  %deckset_id_val59 = load ptr, ptr @__lib_g_ws9_client_info, align 8
  %67 = call ptr @__aiml_str_concat(ptr @50, ptr %deckset_id_val59)
  %68 = call ptr @__aiml_str_concat(ptr %67, ptr @51)
  %print_null_check60 = icmp eq ptr %68, null
  %print_safe_str61 = select i1 %print_null_check60, ptr @print_null_str.12, ptr %68
  %print_call62 = call i32 (ptr, ...) @printf(ptr @52, ptr %print_safe_str61)
  call void @exit(i32 1)
  br label %if_merge58

if_merge58:                                       ; preds = %if_then57, %cmp_merge55
  %deckset_id_val63 = load ptr, ptr @__lib_g_ws9_server_client_info, align 8
  %69 = icmp eq ptr %deckset_id_val63, null
  %70 = icmp eq ptr %deckset_id_val63, @53
  %71 = or i1 %69, false
  br i1 %71, label %cmp_check_null64, label %cmp_do_strcmp65

cmp_check_null64:                                 ; preds = %if_merge58
  br label %cmp_merge66

cmp_do_strcmp65:                                  ; preds = %if_merge58
  %72 = call i32 @strcmp(ptr %deckset_id_val63, ptr @53)
  %73 = icmp eq i32 %72, 0
  br label %cmp_merge66

cmp_merge66:                                      ; preds = %cmp_do_strcmp65, %cmp_check_null64
  %strcmp_res67 = phi i1 [ %70, %cmp_check_null64 ], [ %73, %cmp_do_strcmp65 ]
  %74 = sext i1 %strcmp_res67 to i32
  %75 = icmp eq i32 %74, 0
  br i1 %75, label %if_then68, label %if_merge69

if_then68:                                        ; preds = %cmp_merge66
  %deckset_id_val70 = load ptr, ptr @__lib_g_ws9_server_client_info, align 8
  %76 = call ptr @__aiml_str_concat(ptr @54, ptr %deckset_id_val70)
  %77 = call ptr @__aiml_str_concat(ptr %76, ptr @55)
  %print_null_check71 = icmp eq ptr %77, null
  %print_safe_str72 = select i1 %print_null_check71, ptr @print_null_str.13, ptr %77
  %print_call73 = call i32 (ptr, ...) @printf(ptr @56, ptr %print_safe_str72)
  call void @exit(i32 1)
  br label %if_merge69

if_merge69:                                       ; preds = %if_then68, %cmp_merge66
  %player_name_val74 = load ptr, ptr getelementptr inbounds (%ClientInfoWs9, ptr @__lib_g_ws9_server_client_info, i32 0, i32 1), align 8
  %78 = icmp eq ptr %player_name_val74, null
  %79 = icmp eq ptr %player_name_val74, @57
  %80 = or i1 %78, false
  br i1 %80, label %cmp_check_null75, label %cmp_do_strcmp76

cmp_check_null75:                                 ; preds = %if_merge69
  br label %cmp_merge77

cmp_do_strcmp76:                                  ; preds = %if_merge69
  %81 = call i32 @strcmp(ptr %player_name_val74, ptr @57)
  %82 = icmp eq i32 %81, 0
  br label %cmp_merge77

cmp_merge77:                                      ; preds = %cmp_do_strcmp76, %cmp_check_null75
  %strcmp_res78 = phi i1 [ %79, %cmp_check_null75 ], [ %82, %cmp_do_strcmp76 ]
  %83 = sext i1 %strcmp_res78 to i32
  %84 = icmp eq i32 %83, 0
  br i1 %84, label %if_then79, label %if_merge80

if_then79:                                        ; preds = %cmp_merge77
  %player_name_val81 = load ptr, ptr getelementptr inbounds (%ClientInfoWs9, ptr @__lib_g_ws9_server_client_info, i32 0, i32 1), align 8
  %85 = call ptr @__aiml_str_concat(ptr @58, ptr %player_name_val81)
  %86 = call ptr @__aiml_str_concat(ptr %85, ptr @59)
  %print_null_check82 = icmp eq ptr %86, null
  %print_safe_str83 = select i1 %print_null_check82, ptr @print_null_str.14, ptr %86
  %print_call84 = call i32 (ptr, ...) @printf(ptr @60, ptr %print_safe_str83)
  call void @exit(i32 1)
  br label %if_merge80

if_merge80:                                       ; preds = %if_then79, %cmp_merge77
  %p1_name_val85 = load ptr, ptr getelementptr inbounds (%GameViewWs9, ptr @__lib_g_ws9_view, i32 0, i32 1), align 8
  %87 = icmp eq ptr %p1_name_val85, null
  %88 = icmp eq ptr %p1_name_val85, @61
  %89 = or i1 %87, false
  br i1 %89, label %cmp_check_null86, label %cmp_do_strcmp87

cmp_check_null86:                                 ; preds = %if_merge80
  br label %cmp_merge88

cmp_do_strcmp87:                                  ; preds = %if_merge80
  %90 = call i32 @strcmp(ptr %p1_name_val85, ptr @61)
  %91 = icmp eq i32 %90, 0
  br label %cmp_merge88

cmp_merge88:                                      ; preds = %cmp_do_strcmp87, %cmp_check_null86
  %strcmp_res89 = phi i1 [ %88, %cmp_check_null86 ], [ %91, %cmp_do_strcmp87 ]
  %92 = sext i1 %strcmp_res89 to i32
  %93 = icmp eq i32 %92, 0
  br i1 %93, label %if_then90, label %if_merge91

if_then90:                                        ; preds = %cmp_merge88
  %p1_name_val92 = load ptr, ptr getelementptr inbounds (%GameViewWs9, ptr @__lib_g_ws9_view, i32 0, i32 1), align 8
  %94 = call ptr @__aiml_str_concat(ptr @62, ptr %p1_name_val92)
  %95 = call ptr @__aiml_str_concat(ptr %94, ptr @63)
  %print_null_check93 = icmp eq ptr %95, null
  %print_safe_str94 = select i1 %print_null_check93, ptr @print_null_str.15, ptr %95
  %print_call95 = call i32 (ptr, ...) @printf(ptr @64, ptr %print_safe_str94)
  call void @exit(i32 1)
  br label %if_merge91

if_merge91:                                       ; preds = %if_then90, %cmp_merge88
  %round_val96 = load i32, ptr @__lib_g_ws9_view, align 4
  %96 = icmp ne i32 %round_val96, 7
  br i1 %96, label %if_then97, label %if_merge98

if_then97:                                        ; preds = %if_merge91
  %round_val99 = load i32, ptr @__lib_g_ws9_view, align 4
  %97 = call ptr @__aiml_int_to_string(i32 %round_val99)
  %98 = call ptr @__aiml_str_concat(ptr @65, ptr %97)
  %print_null_check100 = icmp eq ptr %98, null
  %print_safe_str101 = select i1 %print_null_check100, ptr @print_null_str.16, ptr %98
  %print_call102 = call i32 (ptr, ...) @printf(ptr @66, ptr %print_safe_str101)
  call void @exit(i32 1)
  br label %if_merge98

if_merge98:                                       ; preds = %if_then97, %if_merge91
  %hand_count_val = load i32, ptr getelementptr inbounds (%GameViewWs9, ptr @__lib_g_ws9_view, i32 0, i32 3), align 4
  %99 = icmp ne i32 %hand_count_val, 3
  br i1 %99, label %if_then103, label %if_merge104

if_then103:                                       ; preds = %if_merge98
  %hand_count_val105 = load i32, ptr getelementptr inbounds (%GameViewWs9, ptr @__lib_g_ws9_view, i32 0, i32 3), align 4
  %100 = call ptr @__aiml_int_to_string(i32 %hand_count_val105)
  %101 = call ptr @__aiml_str_concat(ptr @67, ptr %100)
  %print_null_check106 = icmp eq ptr %101, null
  %print_safe_str107 = select i1 %print_null_check106, ptr @print_null_str.17, ptr %101
  %print_call108 = call i32 (ptr, ...) @printf(ptr @68, ptr %print_safe_str107)
  call void @exit(i32 1)
  br label %if_merge104

if_merge104:                                      ; preds = %if_then103, %if_merge98
  %connected1_val109 = load i32, ptr %connected1, align 4
  %102 = icmp ne i32 %connected1_val109, 1
  br i1 %102, label %if_then110, label %if_merge111

if_then110:                                       ; preds = %if_merge104
  %connected1_val112 = load i32, ptr %connected1, align 4
  %103 = call ptr @__aiml_int_to_string(i32 %connected1_val112)
  %104 = call ptr @__aiml_str_concat(ptr @69, ptr %103)
  %print_null_check113 = icmp eq ptr %104, null
  %print_safe_str114 = select i1 %print_null_check113, ptr @print_null_str.18, ptr %104
  %print_call115 = call i32 (ptr, ...) @printf(ptr @70, ptr %print_safe_str114)
  call void @exit(i32 1)
  br label %if_merge111

if_merge111:                                      ; preds = %if_then110, %if_merge104
  %sess_ptr116 = load ptr, ptr %client, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr116)
  %handle_ptr = load ptr, ptr %host_th, align 8
  %detached_ptr117 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr117, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

join_detached_throw:                              ; preds = %if_merge111
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %if_merge111
  %joined_ptr118 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr118, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc119 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc119, align 8
  call void @__cxa_throw(ptr %exc_alloc119, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %105 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr118, align 1
  %print_call120 = call i32 (ptr, ...) @printf(ptr @72, ptr @71)
  ret void
}

declare void @__aiml_lib_init_tests_ws9_shared_aiml()

declare void @__aiml_lib_init_tests_ws9_client_globals_aiml()

declare void @__aiml_lib_init_tests_ws9_server_globals_aiml()

declare i32 @__gxx_personality_v0(...)

declare ptr @aiml_mirror_listen(ptr)

declare i32 @aiml_mirror_session_register_typed(ptr, ptr, i64, ptr, i32)

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

declare i32 @aiml_mirror_session_has_event(ptr)

declare ptr @aiml_mirror_session_next_event(ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare i32 @strcmp(ptr, ptr)

declare void @time_sleep_ms(i32)

declare void @aiml_mirror_session_register_id_typed(ptr, ptr, i64, i32, ptr, i32)

declare void @aiml_mirror_session_close(ptr)

define internal ptr @__aiml_spawn_ws9_host(ptr %0) {
entry:
  call void @ws9_host()
  ret ptr null
}

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare ptr @aiml_mirror_connect_timeout(ptr, i32)

declare void @exit(i32)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

attributes #0 = { noinline }
