; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%thread_handle = type { i64, i8, i8 }
%mirror_event = type { ptr, ptr, i32 }
%NetPlayer = type { i32, i32 }
%RpcCast = type { ptr, i32, %list_string }
%list_string = type { i32, i32, ptr }
%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%rpc_event = type { ptr, i32, i32, i32, ptr, i32 }
%GenericList = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_NetPlayer = private unnamed_addr constant [3 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 2, i32 1, i32 0, i32 8, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 4, i32 0, i32 0 }]
@__aiml_sernodes_RpcCast = private unnamed_addr constant [5 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 3, i32 1, i32 0, i32 32, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 4, i32 0, i32 16, i32 8, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__global_host_th = global %thread_handle zeroinitializer
@__global_client = global ptr null
@__global_has_ev = global i32 0
@__global_ev = global %mirror_event zeroinitializer
@__global_replica = global %NetPlayer zeroinitializer
@__global_cast = global %RpcCast zeroinitializer
@0 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19902\00", align 1
@1 = private unnamed_addr constant [31 x i8] c"host-model: registered net_id=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@4 = private unnamed_addr constant [6 x i8] c"ember\00", align 1
@5 = private unnamed_addr constant [6 x i8] c"spell\00", align 1
@6 = private unnamed_addr constant [5 x i8] c"fire\00", align 1
@7 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19902\00", align 1
@8 = private unnamed_addr constant [42 x i8] c"FAIL model-rpc no object_registered event\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [6 x i8] c"ember\00", align 1
@11 = private unnamed_addr constant [6 x i8] c"spell\00", align 1
@12 = private unnamed_addr constant [5 x i8] c"fire\00", align 1
@13 = private unnamed_addr constant [30 x i8] c"client-model: initial health=\00", align 1
@14 = private unnamed_addr constant [8 x i8] c" score=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [5 x i8] c"cast\00", align 1
@17 = private unnamed_addr constant [32 x i8] c"client-model: sent cast payload\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [28 x i8] c"client-model: final health=\00", align 1
@20 = private unnamed_addr constant [8 x i8] c" score=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [23 x i8] c"FAIL model-rpc health=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [22 x i8] c"FAIL model-rpc score=\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [25 x i8] c"mirror rpc model ws PASS\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @mirror_rpc_model_host, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @mirror_rpc_model_host]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_mirror_rpc_model_host = private constant [22 x i8] c"mirror_rpc_model_host\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_mirror_rpc_model_host]
@__aiml_func_count = constant i32 1
@__aiml_sname_NetPlayer = private constant [10 x i8] c"NetPlayer\00"
@__aiml_sfield_NetPlayer_health = private constant [7 x i8] c"health\00"
@__aiml_sfield_NetPlayer_score = private constant [6 x i8] c"score\00"
@__aiml_sname_RpcCast = private constant [8 x i8] c"RpcCast\00"
@__aiml_sfield_RpcCast_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_RpcCast_amount = private constant [7 x i8] c"amount\00"
@__aiml_sfield_RpcCast_tags = private constant [5 x i8] c"tags\00"
@__aiml_schemas = global [2 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_NetPlayer, i32 1, i32 2, i32 8, i32 8, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_NetPlayer_health }, %AimlFieldInfo { ptr @__aiml_sfield_NetPlayer_score }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_RpcCast, i32 1, i32 3, i32 32, i32 32, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_RpcCast_name }, %AimlFieldInfo { ptr @__aiml_sfield_RpcCast_amount }, %AimlFieldInfo { ptr @__aiml_sfield_RpcCast_tags }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 2

define void @mirror_rpc_model_host() personality ptr @__gxx_personality_v0 {
entry:
  %sess = alloca ptr, align 8
  store ptr null, ptr %sess, align 8
  %0 = call ptr @aiml_mirror_listen(ptr @0)
  store ptr %0, ptr %sess, align 8
  %player = alloca %NetPlayer, align 8
  store %NetPlayer zeroinitializer, ptr %player, align 4
  %set_prop_ptr_w = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 40, ptr %set_prop_ptr_w, align 4
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
  %4 = icmp slt i32 %i_val, 35
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
  store i32 800, ptr %set_prop_ptr_w5, align 4
  %sess_ptr6 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_sync_now(ptr %sess_ptr6, ptr %player)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %cmp_merge_ne
  store i32 0, ptr %i, align 4
  br label %while_loop7

while_loop7:                                      ; preds = %if_merge14, %if_merge
  %i_val10 = load i32, ptr %i, align 4
  %14 = icmp slt i32 %i_val10, 50
  br i1 %14, label %while_body8, label %while_end9

while_body8:                                      ; preds = %while_loop7
  %sess_ptr11 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr11)
  %sess_ptr12 = load ptr, ptr %sess, align 8
  %15 = call i32 @aiml_mirror_session_has_rpc(ptr %sess_ptr12)
  %16 = icmp sgt i32 %15, 0
  br i1 %16, label %if_then13, label %if_merge14

while_end9:                                       ; preds = %while_loop7
  %sess_ptr74 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr74)
  ret void

if_then13:                                        ; preds = %while_body8
  %rpc_ev = alloca %rpc_event, align 8
  store %rpc_event zeroinitializer, ptr %rpc_ev, align 8
  %sess_ptr15 = load ptr, ptr %sess, align 8
  %rpc_ptr = call ptr @aiml_mirror_session_next_rpc(ptr %sess_ptr15)
  %rpc_tmp = alloca %rpc_event, align 8
  %17 = call ptr @memcpy(ptr %rpc_tmp, ptr %rpc_ptr, i64 40)
  %18 = call ptr @memcpy(ptr %rpc_ev, ptr %rpc_tmp, i64 40)
  %rc = alloca i32, align 4
  store i32 0, ptr %rc, align 4
  store i32 -1, ptr %rc, align 4
  %prop_ptr16 = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 3
  %arg_kind_val = load i32, ptr %prop_ptr16, align 4
  %19 = icmp ne i32 %arg_kind_val, 1
  br i1 %19, label %if_then17, label %if_else

if_merge14:                                       ; preds = %if_merge18, %while_body8
  call void @time_sleep_ms(i32 10)
  %i_val73 = load i32, ptr %i, align 4
  %20 = add i32 %i_val73, 1
  store i32 %20, ptr %i, align 4
  br label %while_loop7

if_then17:                                        ; preds = %if_then13
  %set_prop_ptr_w19 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 801, ptr %set_prop_ptr_w19, align 4
  br label %if_merge18

if_else:                                          ; preds = %if_then13
  %prop_ptr20 = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 2
  %arg_val = load i32, ptr %prop_ptr20, align 4
  %21 = icmp ne i32 %arg_val, 99
  br i1 %21, label %if_then21, label %if_else22

if_merge18:                                       ; preds = %if_merge23, %if_then17
  %sess_ptr72 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_sync_now(ptr %sess_ptr72, ptr %player)
  br label %if_merge14

if_then21:                                        ; preds = %if_else
  %set_prop_ptr_w24 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 802, ptr %set_prop_ptr_w24, align 4
  br label %if_merge23

if_else22:                                        ; preds = %if_else
  %prop_ptr25 = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 5
  %arg_blob_len_val = load i32, ptr %prop_ptr25, align 4
  %22 = icmp slt i32 %arg_blob_len_val, 8
  br i1 %22, label %if_then26, label %if_else27

if_merge23:                                       ; preds = %if_merge28, %if_then21
  br label %if_merge18

if_then26:                                        ; preds = %if_else22
  %set_prop_ptr_w29 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 803, ptr %set_prop_ptr_w29, align 4
  br label %if_merge28

if_else27:                                        ; preds = %if_else22
  call void @llvm.memset.p0.i64(ptr align 1 @__global_cast, i8 0, i64 32, i1 false)
  %23 = call i32 @aiml_rpc_event_decode_into(ptr %rpc_ev, ptr @__global_cast, ptr @__aiml_sernodes_RpcCast, i32 5)
  store i32 %23, ptr %rc, align 4
  %rc_val = load i32, ptr %rc, align 4
  %24 = icmp ne i32 %rc_val, 0
  br i1 %24, label %if_then30, label %if_else31

if_merge28:                                       ; preds = %if_merge32, %if_then26
  br label %if_merge23

if_then30:                                        ; preds = %if_else27
  %set_prop_ptr_w33 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 804, ptr %set_prop_ptr_w33, align 4
  br label %if_merge32

if_else31:                                        ; preds = %if_else27
  %name_val = load ptr, ptr @__global_cast, align 8
  %25 = icmp eq ptr %name_val, null
  %26 = icmp eq ptr %name_val, @4
  %27 = xor i1 %26, true
  %28 = or i1 %25, false
  br i1 %28, label %cmp_check_null_ne34, label %cmp_do_strcmp_ne35

if_merge32:                                       ; preds = %if_merge40, %if_then30
  br label %if_merge28

cmp_check_null_ne34:                              ; preds = %if_else31
  br label %cmp_merge_ne36

cmp_do_strcmp_ne35:                               ; preds = %if_else31
  %29 = call i32 @strcmp(ptr %name_val, ptr @4)
  %30 = icmp ne i32 %29, 0
  br label %cmp_merge_ne36

cmp_merge_ne36:                                   ; preds = %cmp_do_strcmp_ne35, %cmp_check_null_ne34
  %strcmp_res_ne37 = phi i1 [ %27, %cmp_check_null_ne34 ], [ %30, %cmp_do_strcmp_ne35 ]
  br i1 %strcmp_res_ne37, label %if_then38, label %if_else39

if_then38:                                        ; preds = %cmp_merge_ne36
  %set_prop_ptr_w41 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 805, ptr %set_prop_ptr_w41, align 4
  br label %if_merge40

if_else39:                                        ; preds = %cmp_merge_ne36
  %amount_val = load i32, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 1), align 4
  %31 = icmp ne i32 %amount_val, 12
  br i1 %31, label %if_then42, label %if_else43

if_merge40:                                       ; preds = %if_merge44, %if_then38
  br label %if_merge32

if_then42:                                        ; preds = %if_else39
  %set_prop_ptr_w45 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 806, ptr %set_prop_ptr_w45, align 4
  br label %if_merge44

if_else43:                                        ; preds = %if_else39
  %sizeVal = load i32, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), align 4
  %32 = icmp ne i32 %sizeVal, 2
  br i1 %32, label %if_then46, label %if_else47

if_merge44:                                       ; preds = %if_merge48, %if_then42
  br label %if_merge40

if_then46:                                        ; preds = %if_else43
  %set_prop_ptr_w49 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 807, ptr %set_prop_ptr_w49, align 4
  br label %if_merge48

if_else47:                                        ; preds = %if_else43
  %dataBase = load ptr, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  %33 = getelementptr inbounds ptr, ptr %dataBase, i32 0
  %chain_elem = load ptr, ptr %33, align 8
  %34 = icmp eq ptr %chain_elem, null
  %35 = icmp eq ptr %chain_elem, @5
  %36 = xor i1 %35, true
  %37 = or i1 %34, false
  br i1 %37, label %cmp_check_null_ne50, label %cmp_do_strcmp_ne51

if_merge48:                                       ; preds = %if_merge56, %if_then46
  br label %if_merge44

cmp_check_null_ne50:                              ; preds = %if_else47
  br label %cmp_merge_ne52

cmp_do_strcmp_ne51:                               ; preds = %if_else47
  %38 = call i32 @strcmp(ptr %chain_elem, ptr @5)
  %39 = icmp ne i32 %38, 0
  br label %cmp_merge_ne52

cmp_merge_ne52:                                   ; preds = %cmp_do_strcmp_ne51, %cmp_check_null_ne50
  %strcmp_res_ne53 = phi i1 [ %36, %cmp_check_null_ne50 ], [ %39, %cmp_do_strcmp_ne51 ]
  br i1 %strcmp_res_ne53, label %if_then54, label %if_else55

if_then54:                                        ; preds = %cmp_merge_ne52
  %set_prop_ptr_w57 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 808, ptr %set_prop_ptr_w57, align 4
  br label %if_merge56

if_else55:                                        ; preds = %cmp_merge_ne52
  %dataBase58 = load ptr, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  %40 = getelementptr inbounds ptr, ptr %dataBase58, i32 1
  %chain_elem59 = load ptr, ptr %40, align 8
  %41 = icmp eq ptr %chain_elem59, null
  %42 = icmp eq ptr %chain_elem59, @6
  %43 = xor i1 %42, true
  %44 = or i1 %41, false
  br i1 %44, label %cmp_check_null_ne60, label %cmp_do_strcmp_ne61

if_merge56:                                       ; preds = %if_merge66, %if_then54
  br label %if_merge48

cmp_check_null_ne60:                              ; preds = %if_else55
  br label %cmp_merge_ne62

cmp_do_strcmp_ne61:                               ; preds = %if_else55
  %45 = call i32 @strcmp(ptr %chain_elem59, ptr @6)
  %46 = icmp ne i32 %45, 0
  br label %cmp_merge_ne62

cmp_merge_ne62:                                   ; preds = %cmp_do_strcmp_ne61, %cmp_check_null_ne60
  %strcmp_res_ne63 = phi i1 [ %43, %cmp_check_null_ne60 ], [ %46, %cmp_do_strcmp_ne61 ]
  br i1 %strcmp_res_ne63, label %if_then64, label %if_else65

if_then64:                                        ; preds = %cmp_merge_ne62
  %set_prop_ptr_w67 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 809, ptr %set_prop_ptr_w67, align 4
  br label %if_merge66

if_else65:                                        ; preds = %cmp_merge_ne62
  %prop_ptr68 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  %health_val = load i32, ptr %prop_ptr68, align 4
  %amount_val69 = load i32, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 1), align 4
  %47 = add i32 %health_val, %amount_val69
  %set_prop_ptr_w70 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 %47, ptr %set_prop_ptr_w70, align 4
  %set_prop_ptr_w71 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 1
  store i32 888, ptr %set_prop_ptr_w71, align 4
  br label %if_merge66

if_merge66:                                       ; preds = %if_else65, %if_then64
  br label %if_merge56
}

define void @__aiml_init() {
entry:
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %0 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_mirror_rpc_model_host, ptr null)
  store ptr %handle_raw, ptr @__global_host_th, align 8
  call void @time_sleep_ms(i32 80)
  %1 = call ptr @aiml_mirror_connect_timeout(ptr @7, i32 4000)
  store ptr %1, ptr @__global_client, align 8
  call void @time_sleep_ms(i32 120)
  %sess_ptr = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr)
  %sess_ptr1 = load ptr, ptr @__global_client, align 8
  %2 = call i32 @aiml_mirror_session_has_event(ptr %sess_ptr1)
  store i32 %2, ptr @__global_has_ev, align 4
  %has_ev_val = load i32, ptr @__global_has_ev, align 4
  %3 = icmp eq i32 %has_ev_val, 0
  br i1 %3, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %print_call = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %sess_ptr2 = load ptr, ptr @__global_client, align 8
  %ev_ptr = call ptr @aiml_mirror_session_next_event(ptr %sess_ptr2)
  %ev_tmp = alloca %mirror_event, align 8
  %4 = call ptr @memcpy(ptr %ev_tmp, ptr %ev_ptr, i64 24)
  %5 = call ptr @memcpy(ptr @__global_ev, ptr %ev_tmp, i64 24)
  %sess_ptr3 = load ptr, ptr @__global_client, align 8
  %net_id_val = load i32, ptr getelementptr inbounds (%mirror_event, ptr @__global_ev, i32 0, i32 2), align 4
  call void @aiml_mirror_session_register_id(ptr %sess_ptr3, ptr @__global_replica, i64 8, i32 %net_id_val)
  store ptr @10, ptr @__global_cast, align 8
  store i32 12, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 1), align 4
  %size = load i32, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), align 4
  %capacity = load i32, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 1), align 4
  %6 = icmp eq i32 %size, %capacity
  br i1 %6, label %grow, label %cont

grow:                                             ; preds = %if_merge
  %7 = mul i32 %capacity, 2
  %8 = icmp sgt i32 %7, 0
  %9 = select i1 %8, i32 %7, i32 4
  store i32 %9, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 1), align 4
  %10 = load ptr, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  %11 = zext i32 %9 to i64
  %12 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %11
  %13 = call ptr @realloc(ptr %10, i64 %12)
  store ptr %13, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  br label %cont

cont:                                             ; preds = %grow, %if_merge
  %dataBase = load ptr, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  %14 = getelementptr inbounds ptr, ptr %dataBase, i32 %size
  store ptr @11, ptr %14, align 8
  %15 = add i32 %size, 1
  store i32 %15, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), align 4
  %size4 = load i32, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), align 4
  %capacity5 = load i32, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 1), align 4
  %16 = icmp eq i32 %size4, %capacity5
  br i1 %16, label %grow6, label %cont7

grow6:                                            ; preds = %cont
  %17 = mul i32 %capacity5, 2
  %18 = icmp sgt i32 %17, 0
  %19 = select i1 %18, i32 %17, i32 4
  store i32 %19, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 1), align 4
  %20 = load ptr, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  %21 = zext i32 %19 to i64
  %22 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %21
  %23 = call ptr @realloc(ptr %20, i64 %22)
  store ptr %23, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  br label %cont7

cont7:                                            ; preds = %grow6, %cont
  %dataBase8 = load ptr, ptr getelementptr inbounds (%GenericList, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), i32 0, i32 2), align 8
  %24 = getelementptr inbounds ptr, ptr %dataBase8, i32 %size4
  store ptr @12, ptr %24, align 8
  %25 = add i32 %size4, 1
  store i32 %25, ptr getelementptr inbounds (%RpcCast, ptr @__global_cast, i32 0, i32 2), align 4
  call void @time_sleep_ms(i32 150)
  %sess_ptr9 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr9)
  %health_val = load i32, ptr @__global_replica, align 4
  %26 = call ptr @__aiml_int_to_string(i32 %health_val)
  %27 = call ptr @__aiml_str_concat(ptr @13, ptr %26)
  %28 = call ptr @__aiml_str_concat(ptr %27, ptr @14)
  %score_val = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %29 = call ptr @__aiml_int_to_string(i32 %score_val)
  %30 = call ptr @__aiml_str_concat(ptr %28, ptr %29)
  %print_null_check = icmp eq ptr %30, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.2, ptr %30
  %print_call10 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str)
  %sess_ptr11 = load ptr, ptr @__global_client, align 8
  %net_id_val12 = load i32, ptr getelementptr inbounds (%mirror_event, ptr @__global_ev, i32 0, i32 2), align 4
  call void @aiml_mirror_session_send_rpc_model(ptr %sess_ptr11, i32 %net_id_val12, ptr @16, i32 99, ptr @__global_cast, ptr @__aiml_sernodes_RpcCast, i32 5)
  %print_call13 = call i32 (ptr, ...) @printf(ptr @18, ptr @17)
  call void @time_sleep_ms(i32 240)
  %sess_ptr14 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr14)
  %health_val15 = load i32, ptr @__global_replica, align 4
  %31 = call ptr @__aiml_int_to_string(i32 %health_val15)
  %32 = call ptr @__aiml_str_concat(ptr @19, ptr %31)
  %33 = call ptr @__aiml_str_concat(ptr %32, ptr @20)
  %score_val16 = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %34 = call ptr @__aiml_int_to_string(i32 %score_val16)
  %35 = call ptr @__aiml_str_concat(ptr %33, ptr %34)
  %print_null_check17 = icmp eq ptr %35, null
  %print_safe_str18 = select i1 %print_null_check17, ptr @print_null_str.4, ptr %35
  %print_call19 = call i32 (ptr, ...) @printf(ptr @21, ptr %print_safe_str18)
  %health_val20 = load i32, ptr @__global_replica, align 4
  %36 = icmp ne i32 %health_val20, 52
  br i1 %36, label %if_then21, label %if_merge22

if_then21:                                        ; preds = %cont7
  %health_val23 = load i32, ptr @__global_replica, align 4
  %37 = call ptr @__aiml_int_to_string(i32 %health_val23)
  %38 = call ptr @__aiml_str_concat(ptr @22, ptr %37)
  %print_null_check24 = icmp eq ptr %38, null
  %print_safe_str25 = select i1 %print_null_check24, ptr @print_null_str.5, ptr %38
  %print_call26 = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str25)
  call void @exit(i32 1)
  br label %if_merge22

if_merge22:                                       ; preds = %if_then21, %cont7
  %score_val27 = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %39 = icmp ne i32 %score_val27, 888
  br i1 %39, label %if_then28, label %if_merge29

if_then28:                                        ; preds = %if_merge22
  %score_val30 = load i32, ptr getelementptr inbounds (%NetPlayer, ptr @__global_replica, i32 0, i32 1), align 4
  %40 = call ptr @__aiml_int_to_string(i32 %score_val30)
  %41 = call ptr @__aiml_str_concat(ptr @24, ptr %40)
  %print_null_check31 = icmp eq ptr %41, null
  %print_safe_str32 = select i1 %print_null_check31, ptr @print_null_str.6, ptr %41
  %print_call33 = call i32 (ptr, ...) @printf(ptr @25, ptr %print_safe_str32)
  call void @exit(i32 1)
  br label %if_merge29

if_merge29:                                       ; preds = %if_then28, %if_merge22
  %sess_ptr34 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr34)
  %handle_ptr = load ptr, ptr @__global_host_th, align 8
  %detached_ptr35 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr35, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

join_detached_throw:                              ; preds = %if_merge29
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %if_merge29
  %joined_ptr36 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr36, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc37 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc37, align 8
  call void @__cxa_throw(ptr %exc_alloc37, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %42 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr36, align 1
  %print_call38 = call i32 (ptr, ...) @printf(ptr @27, ptr @26)
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

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #1

declare i32 @aiml_rpc_event_decode_into(ptr, ptr, ptr, i32)

declare void @aiml_mirror_session_close(ptr)

define internal ptr @__aiml_spawn_mirror_rpc_model_host(ptr %0) {
entry:
  call void @mirror_rpc_model_host()
  ret ptr null
}

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare ptr @aiml_mirror_connect_timeout(ptr, i32)

declare i32 @aiml_mirror_session_has_event(ptr)

declare void @exit(i32)

declare ptr @aiml_mirror_session_next_event(ptr)

declare void @aiml_mirror_session_register_id(ptr, ptr, i64, i32)

declare ptr @realloc(ptr, i64)

declare void @aiml_mirror_session_send_rpc_model(ptr, i32, ptr, i32, ptr, ptr, i32)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

attributes #0 = { noinline }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: write) }
