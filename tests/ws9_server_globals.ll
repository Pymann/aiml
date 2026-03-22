; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [32 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%ClientInfoWs9 = type { ptr, ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_GameViewWs9 = private unnamed_addr constant [7 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 6, i32 1, i32 0, i32 40, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 16, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 24, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 28, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 32, i32 0, i32 0 }]
@__aiml_sernodes_ClientInfoWs9 = private unnamed_addr constant [4 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 3, i32 1, i32 0, i32 24, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 16, i32 0, i32 0 }]
@__aiml_fptable = global [2 x [16 x ptr]] [[16 x ptr] [ptr @ws9server.mark_client_info_seen, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @ws9server.client_info_seen, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [2 x ptr] [ptr @ws9server.mark_client_info_seen, ptr @ws9server.client_info_seen]
@__aiml_fptable_versions = global [2 x i32] [i32 1, i32 1]
@__aiml_fname_ws9server.mark_client_info_seen = private constant [32 x i8] c"ws9server.mark_client_info_seen\00"
@__aiml_fname_ws9server.client_info_seen = private constant [27 x i8] c"ws9server.client_info_seen\00"
@__aiml_func_names = constant [2 x ptr] [ptr @__aiml_fname_ws9server.mark_client_info_seen, ptr @__aiml_fname_ws9server.client_info_seen]
@__aiml_func_count = constant i32 2
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

define void @ws9server.mark_client_info_seen() personality ptr @__gxx_personality_v0 {
entry:
  store i32 1, ptr %g_ws9_server_seen_client_info, align 4
  ret void
}

define i32 @ws9server.client_info_seen() personality ptr @__gxx_personality_v0 {
entry:
  %g_ws9_server_seen_client_info_val = load i32, ptr %g_ws9_server_seen_client_info, align 4
  ret i32 %g_ws9_server_seen_client_info_val
}

define void @__aiml_init() {
entry:
  call void @__aiml_lib_init_tests_ws9_shared_aiml()
  %g_ws9_server_client_info = alloca %ClientInfoWs9, align 8
  store %ClientInfoWs9 zeroinitializer, ptr %g_ws9_server_client_info, align 8
  %g_ws9_server_seen_client_info = alloca i32, align 4
  store i32 0, ptr %g_ws9_server_seen_client_info, align 4
  store i32 0, ptr %g_ws9_server_seen_client_info, align 4
  ret void
}

declare void @__aiml_lib_init_tests_ws9_shared_aiml()

declare i32 @__gxx_personality_v0(...)
