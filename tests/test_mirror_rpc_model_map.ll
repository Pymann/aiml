; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%thread_handle = type { i64, i8, i8 }
%mirror_event = type { ptr, ptr, i32 }
%NetPlayer = type { i32 }
%PaymentReq = type { ptr, i32, %map_string_i32 }
%map_string_i32 = type { i32, i32, ptr, ptr, ptr }
%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%rpc_event = type { ptr, i32, i32, i32, ptr, i32 }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_NetPlayer = private unnamed_addr constant [2 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 1, i32 1, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_PaymentReq = private unnamed_addr constant [6 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 3, i32 1, i32 0, i32 48, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 4, i32 5, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__global_host_th = global %thread_handle zeroinitializer
@__global_client = global ptr null
@__global_ev = global %mirror_event zeroinitializer
@__global_replica = global %NetPlayer zeroinitializer
@__global_req = global %PaymentReq zeroinitializer
@0 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19903\00", align 1
@1 = private unnamed_addr constant [29 x i8] c"host-map: registered net_id=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [7 x i8] c"amorph\00", align 1
@4 = private unnamed_addr constant [6 x i8] c"feuer\00", align 1
@5 = private unnamed_addr constant [7 x i8] c"wasser\00", align 1
@6 = private unnamed_addr constant [5 x i8] c"erde\00", align 1
@7 = private unnamed_addr constant [21 x i8] c"ws://127.0.0.1:19903\00", align 1
@8 = private unnamed_addr constant [7 x i8] c"amorph\00", align 1
@9 = private unnamed_addr constant [6 x i8] c"feuer\00", align 1
@10 = private unnamed_addr constant [7 x i8] c"wasser\00", align 1
@11 = private unnamed_addr constant [5 x i8] c"erde\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"pay\00", align 1
@13 = private unnamed_addr constant [29 x i8] c"client-map: sent pay payload\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [25 x i8] c"client-map: final score=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [20 x i8] c"FAIL map-rpc score=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [26 x i8] c"mirror rpc map model PASS\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @mirror_rpc_map_host, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @mirror_rpc_map_host]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_mirror_rpc_map_host = private constant [20 x i8] c"mirror_rpc_map_host\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_mirror_rpc_map_host]
@__aiml_func_count = constant i32 1
@__aiml_sname_NetPlayer = private constant [10 x i8] c"NetPlayer\00"
@__aiml_sfield_NetPlayer_score = private constant [6 x i8] c"score\00"
@__aiml_sname_PaymentReq = private constant [11 x i8] c"PaymentReq\00"
@__aiml_sfield_PaymentReq_label = private constant [6 x i8] c"label\00"
@__aiml_sfield_PaymentReq_total = private constant [6 x i8] c"total\00"
@__aiml_sfield_PaymentReq_parts = private constant [6 x i8] c"parts\00"
@__aiml_schemas = global [2 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_NetPlayer, i32 1, i32 1, i32 4, i32 4, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_NetPlayer_score }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_PaymentReq, i32 1, i32 3, i32 48, i32 64, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_PaymentReq_label }, %AimlFieldInfo { ptr @__aiml_sfield_PaymentReq_total }, %AimlFieldInfo { ptr @__aiml_sfield_PaymentReq_parts }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 2

define void @mirror_rpc_map_host() personality ptr @__gxx_personality_v0 {
entry:
  %sess = alloca ptr, align 8
  store ptr null, ptr %sess, align 8
  %0 = call ptr @aiml_mirror_listen(ptr @0)
  store ptr %0, ptr %sess, align 8
  %player = alloca %NetPlayer, align 8
  store %NetPlayer zeroinitializer, ptr %player, align 4
  %set_prop_ptr_w = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 0, ptr %set_prop_ptr_w, align 4
  %nid = alloca i32, align 4
  store i32 0, ptr %nid, align 4
  %sess_ptr = load ptr, ptr %sess, align 8
  %1 = call i32 @aiml_mirror_session_register(ptr %sess_ptr, ptr %player, i64 4)
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

while_loop:                                       ; preds = %if_merge, %entry
  %i_val = load i32, ptr %i, align 4
  %4 = icmp slt i32 %i_val, 60
  br i1 %4, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %sess_ptr1 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr1)
  %sess_ptr2 = load ptr, ptr %sess, align 8
  %5 = call i32 @aiml_mirror_session_has_rpc(ptr %sess_ptr2)
  %6 = icmp sgt i32 %5, 0
  br i1 %6, label %if_then, label %if_merge

while_end:                                        ; preds = %while_loop
  %sess_ptr52 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr52)
  ret void

if_then:                                          ; preds = %while_body
  %rpc_ev = alloca %rpc_event, align 8
  store %rpc_event zeroinitializer, ptr %rpc_ev, align 8
  %sess_ptr3 = load ptr, ptr %sess, align 8
  %rpc_ptr = call ptr @aiml_mirror_session_next_rpc(ptr %sess_ptr3)
  %rpc_tmp = alloca %rpc_event, align 8
  %7 = call ptr @memcpy(ptr %rpc_tmp, ptr %rpc_ptr, i64 40)
  %8 = call ptr @memcpy(ptr %rpc_ev, ptr %rpc_tmp, i64 40)
  %rc = alloca i32, align 4
  store i32 0, ptr %rc, align 4
  store i32 -1, ptr %rc, align 4
  %prop_ptr = getelementptr inbounds %rpc_event, ptr %rpc_ev, i32 0, i32 3
  %arg_kind_val = load i32, ptr %prop_ptr, align 4
  %9 = icmp ne i32 %arg_kind_val, 1
  br i1 %9, label %if_then4, label %if_else

if_merge:                                         ; preds = %if_merge5, %while_body
  call void @time_sleep_ms(i32 10)
  %i_val51 = load i32, ptr %i, align 4
  %10 = add i32 %i_val51, 1
  store i32 %10, ptr %i, align 4
  br label %while_loop

if_then4:                                         ; preds = %if_then
  %set_prop_ptr_w6 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 901, ptr %set_prop_ptr_w6, align 4
  br label %if_merge5

if_else:                                          ; preds = %if_then
  call void @llvm.memset.p0.i64(ptr align 1 @__global_req, i8 0, i64 48, i1 false)
  %11 = call i32 @aiml_rpc_event_decode_into(ptr %rpc_ev, ptr @__global_req, ptr @__aiml_sernodes_PaymentReq, i32 6)
  store i32 %11, ptr %rc, align 4
  %rc_val = load i32, ptr %rc, align 4
  %12 = icmp ne i32 %rc_val, 0
  br i1 %12, label %if_then7, label %if_else8

if_merge5:                                        ; preds = %if_merge9, %if_then4
  %sess_ptr50 = load ptr, ptr %sess, align 8
  call void @aiml_mirror_session_sync_now(ptr %sess_ptr50, ptr %player)
  br label %if_merge

if_then7:                                         ; preds = %if_else
  %set_prop_ptr_w10 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 902, ptr %set_prop_ptr_w10, align 4
  br label %if_merge9

if_else8:                                         ; preds = %if_else
  %label_val = load ptr, ptr @__global_req, align 8
  %13 = icmp eq ptr %label_val, null
  %14 = icmp eq ptr %label_val, @3
  %15 = xor i1 %14, true
  %16 = or i1 %13, false
  br i1 %16, label %cmp_check_null_ne, label %cmp_do_strcmp_ne

if_merge9:                                        ; preds = %if_merge13, %if_then7
  br label %if_merge5

cmp_check_null_ne:                                ; preds = %if_else8
  br label %cmp_merge_ne

cmp_do_strcmp_ne:                                 ; preds = %if_else8
  %17 = call i32 @strcmp(ptr %label_val, ptr @3)
  %18 = icmp ne i32 %17, 0
  br label %cmp_merge_ne

cmp_merge_ne:                                     ; preds = %cmp_do_strcmp_ne, %cmp_check_null_ne
  %strcmp_res_ne = phi i1 [ %15, %cmp_check_null_ne ], [ %18, %cmp_do_strcmp_ne ]
  br i1 %strcmp_res_ne, label %if_then11, label %if_else12

if_then11:                                        ; preds = %cmp_merge_ne
  %set_prop_ptr_w14 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 903, ptr %set_prop_ptr_w14, align 4
  br label %if_merge13

if_else12:                                        ; preds = %cmp_merge_ne
  %total_val = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 1), align 4
  %19 = icmp ne i32 %total_val, 5
  br i1 %19, label %if_then15, label %if_else16

if_merge13:                                       ; preds = %if_merge17, %if_then11
  br label %if_merge9

if_then15:                                        ; preds = %if_else12
  %set_prop_ptr_w18 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 904, ptr %set_prop_ptr_w18, align 4
  br label %if_merge17

if_else16:                                        ; preds = %if_else12
  %a = alloca i32, align 4
  store i32 0, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 0, ptr %b, align 4
  %c = alloca i32, align 4
  store i32 0, ptr %c, align 4
  %20 = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  %21 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %22 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca i32, align 4
  store i32 0, ptr %res_get, align 4
  br label %get_loop

if_merge17:                                       ; preds = %if_merge39, %if_then15
  br label %if_merge13

get_loop:                                         ; preds = %get_next, %if_else16
  %23 = load i32, ptr %i_get, align 4
  %24 = icmp slt i32 %23, %20
  br i1 %24, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %25 = getelementptr inbounds ptr, ptr %21, i32 %23
  %26 = load ptr, ptr %25, align 8
  %27 = call i32 @strcmp(ptr @4, ptr %26)
  %str_eq = icmp eq i32 %27, 0
  br i1 %str_eq, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %28 = getelementptr inbounds i32, ptr %22, i32 %23
  %29 = load i32, ptr %28, align 4
  store i32 %29, ptr %res_get, align 4
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load i32, ptr %res_get, align 4
  store i32 %get_res, ptr %a, align 4
  %30 = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  %31 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %32 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %i_get23 = alloca i32, align 4
  store i32 0, ptr %i_get23, align 4
  %res_get24 = alloca i32, align 4
  store i32 0, ptr %res_get24, align 4
  br label %get_loop19

get_next:                                         ; preds = %get_check
  %33 = add i32 %23, 1
  store i32 %33, ptr %i_get, align 4
  br label %get_loop

get_loop19:                                       ; preds = %get_next26, %get_end
  %34 = load i32, ptr %i_get23, align 4
  %35 = icmp slt i32 %34, %30
  br i1 %35, label %get_check20, label %get_end22

get_check20:                                      ; preds = %get_loop19
  %36 = getelementptr inbounds ptr, ptr %31, i32 %34
  %37 = load ptr, ptr %36, align 8
  %38 = call i32 @strcmp(ptr @5, ptr %37)
  %str_eq25 = icmp eq i32 %38, 0
  br i1 %str_eq25, label %get_found21, label %get_next26

get_found21:                                      ; preds = %get_check20
  %39 = getelementptr inbounds i32, ptr %32, i32 %34
  %40 = load i32, ptr %39, align 4
  store i32 %40, ptr %res_get24, align 4
  br label %get_end22

get_end22:                                        ; preds = %get_found21, %get_loop19
  %get_res27 = load i32, ptr %res_get24, align 4
  store i32 %get_res27, ptr %b, align 4
  %41 = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  %42 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %43 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %i_get32 = alloca i32, align 4
  store i32 0, ptr %i_get32, align 4
  %res_get33 = alloca i32, align 4
  store i32 0, ptr %res_get33, align 4
  br label %get_loop28

get_next26:                                       ; preds = %get_check20
  %44 = add i32 %34, 1
  store i32 %44, ptr %i_get23, align 4
  br label %get_loop19

get_loop28:                                       ; preds = %get_next35, %get_end22
  %45 = load i32, ptr %i_get32, align 4
  %46 = icmp slt i32 %45, %41
  br i1 %46, label %get_check29, label %get_end31

get_check29:                                      ; preds = %get_loop28
  %47 = getelementptr inbounds ptr, ptr %42, i32 %45
  %48 = load ptr, ptr %47, align 8
  %49 = call i32 @strcmp(ptr @6, ptr %48)
  %str_eq34 = icmp eq i32 %49, 0
  br i1 %str_eq34, label %get_found30, label %get_next35

get_found30:                                      ; preds = %get_check29
  %50 = getelementptr inbounds i32, ptr %43, i32 %45
  %51 = load i32, ptr %50, align 4
  store i32 %51, ptr %res_get33, align 4
  br label %get_end31

get_end31:                                        ; preds = %get_found30, %get_loop28
  %get_res36 = load i32, ptr %res_get33, align 4
  store i32 %get_res36, ptr %c, align 4
  %a_val = load i32, ptr %a, align 4
  %52 = icmp ne i32 %a_val, 2
  br i1 %52, label %if_then37, label %if_else38

get_next35:                                       ; preds = %get_check29
  %53 = add i32 %45, 1
  store i32 %53, ptr %i_get32, align 4
  br label %get_loop28

if_then37:                                        ; preds = %get_end31
  %set_prop_ptr_w40 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 905, ptr %set_prop_ptr_w40, align 4
  br label %if_merge39

if_else38:                                        ; preds = %get_end31
  %b_val = load i32, ptr %b, align 4
  %54 = icmp ne i32 %b_val, 1
  br i1 %54, label %if_then41, label %if_else42

if_merge39:                                       ; preds = %if_merge43, %if_then37
  br label %if_merge17

if_then41:                                        ; preds = %if_else38
  %set_prop_ptr_w44 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 906, ptr %set_prop_ptr_w44, align 4
  br label %if_merge43

if_else42:                                        ; preds = %if_else38
  %c_val = load i32, ptr %c, align 4
  %55 = icmp ne i32 %c_val, 2
  br i1 %55, label %if_then45, label %if_else46

if_merge43:                                       ; preds = %if_merge47, %if_then41
  br label %if_merge39

if_then45:                                        ; preds = %if_else42
  %set_prop_ptr_w48 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 907, ptr %set_prop_ptr_w48, align 4
  br label %if_merge47

if_else46:                                        ; preds = %if_else42
  %set_prop_ptr_w49 = getelementptr inbounds %NetPlayer, ptr %player, i32 0, i32 0
  store i32 999, ptr %set_prop_ptr_w49, align 4
  br label %if_merge47

if_merge47:                                       ; preds = %if_else46, %if_then45
  br label %if_merge43
}

define void @__aiml_init() {
entry:
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %0 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_mirror_rpc_map_host, ptr null)
  store ptr %handle_raw, ptr @__global_host_th, align 8
  call void @time_sleep_ms(i32 80)
  %1 = call ptr @aiml_mirror_connect_timeout(ptr @7, i32 4000)
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
  call void @aiml_mirror_session_register_id(ptr %sess_ptr2, ptr @__global_replica, i64 4, i32 %net_id_val)
  store ptr @8, ptr @__global_req, align 8
  store i32 5, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 1), align 4
  %size = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  %capacity = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 1), align 4
  %keysBase = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %valsBase = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %entry
  %4 = load i32, ptr %i_put, align 4
  %5 = icmp slt i32 %4, %size
  br i1 %5, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %6 = getelementptr inbounds ptr, ptr %keysBase, i32 %4
  %7 = load ptr, ptr %6, align 8
  %8 = call i32 @strcmp(ptr @9, ptr %7)
  %str_eq = icmp eq i32 %8, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %9 = getelementptr inbounds i32, ptr %valsBase, i32 %4
  store i32 2, ptr %9, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %10 = add i32 %4, 1
  store i32 %10, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %11 = icmp eq i32 %size, %capacity
  br i1 %11, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %size3 = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  %capacity4 = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 1), align 4
  %keysBase5 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %valsBase6 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %i_put13 = alloca i32, align 4
  store i32 0, ptr %i_put13, align 4
  br label %put_loop7

put_grow:                                         ; preds = %put_not_found
  %12 = mul i32 %capacity, 2
  %13 = icmp sgt i32 %12, 3
  %growCap = select i1 %13, i32 %12, i32 4
  store i32 %growCap, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 1), align 4
  %14 = zext i32 %growCap to i64
  %15 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %14
  %16 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %17 = call ptr @realloc(ptr %16, i64 %15)
  store ptr %17, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %18 = zext i32 %growCap to i64
  %19 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %18
  %20 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %21 = call ptr @realloc(ptr %20, i64 %19)
  store ptr %21, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %22 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %23 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %24 = getelementptr inbounds ptr, ptr %22, i32 %size
  store ptr @9, ptr %24, align 8
  %25 = getelementptr inbounds i32, ptr %23, i32 %size
  store i32 2, ptr %25, align 4
  %26 = add i32 %size, 1
  store i32 %26, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  br label %put_end

put_loop7:                                        ; preds = %put_next10, %put_end
  %27 = load i32, ptr %i_put13, align 4
  %28 = icmp slt i32 %27, %size3
  br i1 %28, label %put_check8, label %put_not_found11

put_check8:                                       ; preds = %put_loop7
  %29 = getelementptr inbounds ptr, ptr %keysBase5, i32 %27
  %30 = load ptr, ptr %29, align 8
  %31 = call i32 @strcmp(ptr @10, ptr %30)
  %str_eq14 = icmp eq i32 %31, 0
  br i1 %str_eq14, label %put_found9, label %put_next10

put_found9:                                       ; preds = %put_check8
  %32 = getelementptr inbounds i32, ptr %valsBase6, i32 %27
  store i32 1, ptr %32, align 4
  br label %put_end12

put_next10:                                       ; preds = %put_check8
  %33 = add i32 %27, 1
  store i32 %33, ptr %i_put13, align 4
  br label %put_loop7

put_not_found11:                                  ; preds = %put_loop7
  %34 = icmp eq i32 %size3, %capacity4
  br i1 %34, label %put_grow15, label %put_append16

put_end12:                                        ; preds = %put_append16, %put_found9
  %size18 = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  %capacity19 = load i32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 1), align 4
  %keysBase20 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %valsBase21 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %i_put28 = alloca i32, align 4
  store i32 0, ptr %i_put28, align 4
  br label %put_loop22

put_grow15:                                       ; preds = %put_not_found11
  %35 = mul i32 %capacity4, 2
  %36 = icmp sgt i32 %35, 3
  %growCap17 = select i1 %36, i32 %35, i32 4
  store i32 %growCap17, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 1), align 4
  %37 = zext i32 %growCap17 to i64
  %38 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %37
  %39 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %40 = call ptr @realloc(ptr %39, i64 %38)
  store ptr %40, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %41 = zext i32 %growCap17 to i64
  %42 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %41
  %43 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %44 = call ptr @realloc(ptr %43, i64 %42)
  store ptr %44, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  br label %put_append16

put_append16:                                     ; preds = %put_grow15, %put_not_found11
  %45 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %46 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %47 = getelementptr inbounds ptr, ptr %45, i32 %size3
  store ptr @10, ptr %47, align 8
  %48 = getelementptr inbounds i32, ptr %46, i32 %size3
  store i32 1, ptr %48, align 4
  %49 = add i32 %size3, 1
  store i32 %49, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  br label %put_end12

put_loop22:                                       ; preds = %put_next25, %put_end12
  %50 = load i32, ptr %i_put28, align 4
  %51 = icmp slt i32 %50, %size18
  br i1 %51, label %put_check23, label %put_not_found26

put_check23:                                      ; preds = %put_loop22
  %52 = getelementptr inbounds ptr, ptr %keysBase20, i32 %50
  %53 = load ptr, ptr %52, align 8
  %54 = call i32 @strcmp(ptr @11, ptr %53)
  %str_eq29 = icmp eq i32 %54, 0
  br i1 %str_eq29, label %put_found24, label %put_next25

put_found24:                                      ; preds = %put_check23
  %55 = getelementptr inbounds i32, ptr %valsBase21, i32 %50
  store i32 2, ptr %55, align 4
  br label %put_end27

put_next25:                                       ; preds = %put_check23
  %56 = add i32 %50, 1
  store i32 %56, ptr %i_put28, align 4
  br label %put_loop22

put_not_found26:                                  ; preds = %put_loop22
  %57 = icmp eq i32 %size18, %capacity19
  br i1 %57, label %put_grow30, label %put_append31

put_end27:                                        ; preds = %put_append31, %put_found24
  %sess_ptr33 = load ptr, ptr @__global_client, align 8
  %net_id_val34 = load i32, ptr getelementptr inbounds (%mirror_event, ptr @__global_ev, i32 0, i32 2), align 4
  call void @aiml_mirror_session_send_rpc_model(ptr %sess_ptr33, i32 %net_id_val34, ptr @12, i32 0, ptr @__global_req, ptr @__aiml_sernodes_PaymentReq, i32 6)
  %print_call = call i32 (ptr, ...) @printf(ptr @14, ptr @13)
  call void @time_sleep_ms(i32 300)
  %sess_ptr35 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_update(ptr %sess_ptr35)
  %score_val = load i32, ptr @__global_replica, align 4
  %58 = call ptr @__aiml_int_to_string(i32 %score_val)
  %59 = call ptr @__aiml_str_concat(ptr @15, ptr %58)
  %print_null_check = icmp eq ptr %59, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.2, ptr %59
  %print_call36 = call i32 (ptr, ...) @printf(ptr @16, ptr %print_safe_str)
  %score_val37 = load i32, ptr @__global_replica, align 4
  %60 = icmp ne i32 %score_val37, 999
  br i1 %60, label %if_then, label %if_merge

put_grow30:                                       ; preds = %put_not_found26
  %61 = mul i32 %capacity19, 2
  %62 = icmp sgt i32 %61, 3
  %growCap32 = select i1 %62, i32 %61, i32 4
  store i32 %growCap32, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 1), align 4
  %63 = zext i32 %growCap32 to i64
  %64 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %63
  %65 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %66 = call ptr @realloc(ptr %65, i64 %64)
  store ptr %66, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %67 = zext i32 %growCap32 to i64
  %68 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %67
  %69 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %70 = call ptr @realloc(ptr %69, i64 %68)
  store ptr %70, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  br label %put_append31

put_append31:                                     ; preds = %put_grow30, %put_not_found26
  %71 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 3), align 8
  %72 = load ptr, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2, i32 4), align 8
  %73 = getelementptr inbounds ptr, ptr %71, i32 %size18
  store ptr @11, ptr %73, align 8
  %74 = getelementptr inbounds i32, ptr %72, i32 %size18
  store i32 2, ptr %74, align 4
  %75 = add i32 %size18, 1
  store i32 %75, ptr getelementptr inbounds (%PaymentReq, ptr @__global_req, i32 0, i32 2), align 4
  br label %put_end27

if_then:                                          ; preds = %put_end27
  %score_val38 = load i32, ptr @__global_replica, align 4
  %76 = call ptr @__aiml_int_to_string(i32 %score_val38)
  %77 = call ptr @__aiml_str_concat(ptr @17, ptr %76)
  %print_null_check39 = icmp eq ptr %77, null
  %print_safe_str40 = select i1 %print_null_check39, ptr @print_null_str.3, ptr %77
  %print_call41 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str40)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %put_end27
  %sess_ptr42 = load ptr, ptr @__global_client, align 8
  call void @aiml_mirror_session_close(ptr %sess_ptr42)
  %handle_ptr = load ptr, ptr @__global_host_th, align 8
  %detached_ptr43 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr43, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

join_detached_throw:                              ; preds = %if_merge
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %if_merge
  %joined_ptr44 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr44, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc45 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc45, align 8
  call void @__cxa_throw(ptr %exc_alloc45, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %78 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr44, align 1
  %print_call46 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
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

declare i32 @aiml_mirror_session_has_rpc(ptr)

declare ptr @aiml_mirror_session_next_rpc(ptr)

declare ptr @memcpy(ptr, ptr, i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #1

declare i32 @aiml_rpc_event_decode_into(ptr, ptr, ptr, i32)

declare i32 @strcmp(ptr, ptr)

declare void @aiml_mirror_session_sync_now(ptr, ptr)

declare void @time_sleep_ms(i32)

declare void @aiml_mirror_session_close(ptr)

define internal ptr @__aiml_spawn_mirror_rpc_map_host(ptr %0) {
entry:
  call void @mirror_rpc_map_host()
  ret ptr null
}

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare ptr @aiml_mirror_connect_timeout(ptr, i32)

declare ptr @aiml_mirror_session_next_event(ptr)

declare void @aiml_mirror_session_register_id(ptr, ptr, i64, i32)

declare ptr @realloc(ptr, i64)

declare void @aiml_mirror_session_send_rpc_model(ptr, i32, ptr, i32, ptr, ptr, i32)

declare void @exit(i32)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

attributes #0 = { noinline }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: write) }
