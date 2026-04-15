; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Ability = type { ptr, %"list_list<i32>", %"list_list<i32>", %"list_list<i32>" }
%"list_list<i32>" = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%list_i32 = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_Ability = private unnamed_addr constant [11 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 1, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 5, i32 0, i32 8, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 7, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 9, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 6, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 8, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 10, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@0 = private unnamed_addr constant [95 x i8] c"{\22__type\22:\22Ability\22,\22cost_text\22:\22Tap\22,\22activation_cost\22:[[6]],\22trigger\22:[],\22effects\22:[[10,3]]}\00", align 1
@fk_cost_text = private unnamed_addr constant [10 x i8] c"cost_text\00", align 1
@fk_activation_cost = private unnamed_addr constant [16 x i8] c"activation_cost\00", align 1
@fk_trigger = private unnamed_addr constant [8 x i8] c"trigger\00", align 1
@fk_effects = private unnamed_addr constant [8 x i8] c"effects\00", align 1
@1 = private unnamed_addr constant [30 x i8] c"FAIL A1: activation_cost.len=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@2 = private unnamed_addr constant [12 x i8] c" expected 1\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [17 x i8] c"FAIL A2: cb.len=\00", align 1
@5 = private unnamed_addr constant [12 x i8] c" expected 1\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [16 x i8] c"FAIL A3: cb[0]=\00", align 1
@8 = private unnamed_addr constant [12 x i8] c" expected 6\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [22 x i8] c"FAIL A4: effects.len=\00", align 1
@11 = private unnamed_addr constant [12 x i8] c" expected 1\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [19 x i8] c"FAIL A5: eff0.len=\00", align 1
@14 = private unnamed_addr constant [12 x i8] c" expected 2\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [18 x i8] c"FAIL A6: eff0[0]=\00", align 1
@17 = private unnamed_addr constant [13 x i8] c" expected 10\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [13 x i8] c"Phase A PASS\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [39 x i8] c"/tmp/aiml_test_list_list_inner_values/\00", align 1
@22 = private unnamed_addr constant [13 x i8] c"inner_values\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"ab1\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"ab1\00", align 1
@25 = private unnamed_addr constant [18 x i8] c"FAIL B1: got=null\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [34 x i8] c"FAIL B2: got.activation_cost.len=\00", align 1
@28 = private unnamed_addr constant [12 x i8] c" expected 1\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [18 x i8] c"FAIL B3: gcb.len=\00", align 1
@31 = private unnamed_addr constant [51 x i8] c" expected 1 (inner list empty after WAL roundtrip)\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [17 x i8] c"FAIL B4: gcb[0]=\00", align 1
@34 = private unnamed_addr constant [12 x i8] c" expected 6\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [26 x i8] c"FAIL B5: got.effects.len=\00", align 1
@37 = private unnamed_addr constant [12 x i8] c" expected 1\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [20 x i8] c"FAIL B6: geff0.len=\00", align 1
@40 = private unnamed_addr constant [12 x i8] c" expected 2\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [19 x i8] c"FAIL B7: geff0[0]=\00", align 1
@43 = private unnamed_addr constant [13 x i8] c" expected 10\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [13 x i8] c"Phase B PASS\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@46 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@47 = private unnamed_addr constant [9 x i8] c"ALL PASS\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @main]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 1
@__aiml_sname_Ability = private constant [8 x i8] c"Ability\00"
@__aiml_sfield_Ability_cost_text = private constant [10 x i8] c"cost_text\00"
@__aiml_sfield_Ability_activation_cost = private constant [16 x i8] c"activation_cost\00"
@__aiml_sfield_Ability_trigger = private constant [8 x i8] c"trigger\00"
@__aiml_sfield_Ability_effects = private constant [8 x i8] c"effects\00"
@__aiml_schemas = global [1 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_Ability, i32 1, i32 4, i32 56, i32 64, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Ability_cost_text }, %AimlFieldInfo { ptr @__aiml_sfield_Ability_activation_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Ability_trigger }, %AimlFieldInfo { ptr @__aiml_sfield_Ability_effects }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 1

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %j1 = alloca ptr, align 8
  store ptr null, ptr %j1, align 8
  store ptr @0, ptr %j1, align 8
  %ab = alloca %Ability, align 8
  store %Ability zeroinitializer, ptr %ab, align 8
  %j1_val = load ptr, ptr %j1, align 8
  %deser_r = call ptr @__aiml_deser_Ability(ptr %j1_val)
  %0 = call ptr @memcpy(ptr %ab, ptr %deser_r, i64 56)
  %chain_gep = getelementptr inbounds %Ability, ptr %ab, i32 0, i32 1
  %sizePtr = getelementptr inbounds %GenericList, ptr %chain_gep, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr, align 4
  %1 = icmp ne i32 %sizeVal, 1
  br i1 %1, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %chain_gep1 = getelementptr inbounds %Ability, ptr %ab, i32 0, i32 1
  %sizePtr2 = getelementptr inbounds %GenericList, ptr %chain_gep1, i32 0, i32 0
  %sizeVal3 = load i32, ptr %sizePtr2, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %sizeVal3)
  %3 = call ptr @__aiml_str_concat(ptr @1, ptr %2)
  %4 = call ptr @__aiml_str_concat(ptr %3, ptr @2)
  %print_null_check = icmp eq ptr %4, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %4
  %print_call = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %cb = alloca %list_i32, align 8
  store %list_i32 zeroinitializer, ptr %cb, align 8
  %sizePtr4 = getelementptr inbounds %GenericList, ptr %cb, i32 0, i32 0
  store i32 0, ptr %sizePtr4, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %cb, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %5 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %cb, i32 0, i32 2
  store ptr %5, ptr %dataPtr, align 8
  %set_prop_ptr_w = getelementptr inbounds %Ability, ptr %ab, i32 0, i32 1
  %dataPtr5 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr5, align 8
  %6 = getelementptr inbounds %list_i32, ptr %dataBase, i32 0
  %7 = call ptr @memcpy(ptr %cb, ptr %6, i64 16)
  %sizePtr6 = getelementptr inbounds %GenericList, ptr %cb, i32 0, i32 0
  %sizeVal7 = load i32, ptr %sizePtr6, align 4
  %8 = icmp ne i32 %sizeVal7, 1
  br i1 %8, label %if_then8, label %if_merge9

if_then8:                                         ; preds = %if_merge
  %sizePtr10 = getelementptr inbounds %GenericList, ptr %cb, i32 0, i32 0
  %sizeVal11 = load i32, ptr %sizePtr10, align 4
  %9 = call ptr @__aiml_int_to_string(i32 %sizeVal11)
  %10 = call ptr @__aiml_str_concat(ptr @4, ptr %9)
  %11 = call ptr @__aiml_str_concat(ptr %10, ptr @5)
  %print_null_check12 = icmp eq ptr %11, null
  %print_safe_str13 = select i1 %print_null_check12, ptr @print_null_str.1, ptr %11
  %print_call14 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str13)
  call void @exit(i32 1)
  br label %if_merge9

if_merge9:                                        ; preds = %if_then8, %if_merge
  %v0 = alloca i32, align 4
  store i32 0, ptr %v0, align 4
  %dataPtr15 = getelementptr inbounds %GenericList, ptr %cb, i32 0, i32 2
  %dataBase16 = load ptr, ptr %dataPtr15, align 8
  %12 = getelementptr inbounds i32, ptr %dataBase16, i32 0
  %getVal = load i32, ptr %12, align 4
  store i32 %getVal, ptr %v0, align 4
  %v0_val = load i32, ptr %v0, align 4
  %13 = icmp ne i32 %v0_val, 6
  br i1 %13, label %if_then17, label %if_merge18

if_then17:                                        ; preds = %if_merge9
  %v0_val19 = load i32, ptr %v0, align 4
  %14 = call ptr @__aiml_int_to_string(i32 %v0_val19)
  %15 = call ptr @__aiml_str_concat(ptr @7, ptr %14)
  %16 = call ptr @__aiml_str_concat(ptr %15, ptr @8)
  %print_null_check20 = icmp eq ptr %16, null
  %print_safe_str21 = select i1 %print_null_check20, ptr @print_null_str.2, ptr %16
  %print_call22 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str21)
  call void @exit(i32 1)
  br label %if_merge18

if_merge18:                                       ; preds = %if_then17, %if_merge9
  %chain_gep23 = getelementptr inbounds %Ability, ptr %ab, i32 0, i32 3
  %sizePtr24 = getelementptr inbounds %GenericList, ptr %chain_gep23, i32 0, i32 0
  %sizeVal25 = load i32, ptr %sizePtr24, align 4
  %17 = icmp ne i32 %sizeVal25, 1
  br i1 %17, label %if_then26, label %if_merge27

if_then26:                                        ; preds = %if_merge18
  %chain_gep28 = getelementptr inbounds %Ability, ptr %ab, i32 0, i32 3
  %sizePtr29 = getelementptr inbounds %GenericList, ptr %chain_gep28, i32 0, i32 0
  %sizeVal30 = load i32, ptr %sizePtr29, align 4
  %18 = call ptr @__aiml_int_to_string(i32 %sizeVal30)
  %19 = call ptr @__aiml_str_concat(ptr @10, ptr %18)
  %20 = call ptr @__aiml_str_concat(ptr %19, ptr @11)
  %print_null_check31 = icmp eq ptr %20, null
  %print_safe_str32 = select i1 %print_null_check31, ptr @print_null_str.3, ptr %20
  %print_call33 = call i32 (ptr, ...) @printf(ptr @12, ptr %print_safe_str32)
  call void @exit(i32 1)
  br label %if_merge27

if_merge27:                                       ; preds = %if_then26, %if_merge18
  %eff0 = alloca %list_i32, align 8
  store %list_i32 zeroinitializer, ptr %eff0, align 8
  %sizePtr34 = getelementptr inbounds %GenericList, ptr %eff0, i32 0, i32 0
  store i32 0, ptr %sizePtr34, align 4
  %capPtr35 = getelementptr inbounds %GenericList, ptr %eff0, i32 0, i32 1
  store i32 4, ptr %capPtr35, align 4
  %21 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr36 = getelementptr inbounds %GenericList, ptr %eff0, i32 0, i32 2
  store ptr %21, ptr %dataPtr36, align 8
  %set_prop_ptr_w37 = getelementptr inbounds %Ability, ptr %ab, i32 0, i32 3
  %dataPtr38 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w37, i32 0, i32 2
  %dataBase39 = load ptr, ptr %dataPtr38, align 8
  %22 = getelementptr inbounds %list_i32, ptr %dataBase39, i32 0
  %23 = call ptr @memcpy(ptr %eff0, ptr %22, i64 16)
  %sizePtr40 = getelementptr inbounds %GenericList, ptr %eff0, i32 0, i32 0
  %sizeVal41 = load i32, ptr %sizePtr40, align 4
  %24 = icmp ne i32 %sizeVal41, 2
  br i1 %24, label %if_then42, label %if_merge43

if_then42:                                        ; preds = %if_merge27
  %sizePtr44 = getelementptr inbounds %GenericList, ptr %eff0, i32 0, i32 0
  %sizeVal45 = load i32, ptr %sizePtr44, align 4
  %25 = call ptr @__aiml_int_to_string(i32 %sizeVal45)
  %26 = call ptr @__aiml_str_concat(ptr @13, ptr %25)
  %27 = call ptr @__aiml_str_concat(ptr %26, ptr @14)
  %print_null_check46 = icmp eq ptr %27, null
  %print_safe_str47 = select i1 %print_null_check46, ptr @print_null_str.4, ptr %27
  %print_call48 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str47)
  call void @exit(i32 1)
  br label %if_merge43

if_merge43:                                       ; preds = %if_then42, %if_merge27
  %e0 = alloca i32, align 4
  store i32 0, ptr %e0, align 4
  %dataPtr49 = getelementptr inbounds %GenericList, ptr %eff0, i32 0, i32 2
  %dataBase50 = load ptr, ptr %dataPtr49, align 8
  %28 = getelementptr inbounds i32, ptr %dataBase50, i32 0
  %getVal51 = load i32, ptr %28, align 4
  store i32 %getVal51, ptr %e0, align 4
  %e0_val = load i32, ptr %e0, align 4
  %29 = icmp ne i32 %e0_val, 10
  br i1 %29, label %if_then52, label %if_merge53

if_then52:                                        ; preds = %if_merge43
  %e0_val54 = load i32, ptr %e0, align 4
  %30 = call ptr @__aiml_int_to_string(i32 %e0_val54)
  %31 = call ptr @__aiml_str_concat(ptr @16, ptr %30)
  %32 = call ptr @__aiml_str_concat(ptr %31, ptr @17)
  %print_null_check55 = icmp eq ptr %32, null
  %print_safe_str56 = select i1 %print_null_check55, ptr @print_null_str.5, ptr %32
  %print_call57 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str56)
  call void @exit(i32 1)
  br label %if_merge53

if_merge53:                                       ; preds = %if_then52, %if_merge43
  %print_call58 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
  %db_dir = alloca ptr, align 8
  store ptr null, ptr %db_dir, align 8
  %path_init = call ptr @aiml_path_intern(ptr @21)
  store ptr %path_init, ptr %db_dir, align 8
  %db_dir_val = load ptr, ptr %db_dir, align 8
  %33 = call i32 @aiml_io_remove_all(ptr %db_dir_val)
  %kv = alloca ptr, align 8
  store ptr null, ptr %kv, align 8
  %db_dir_val59 = load ptr, ptr %db_dir, align 8
  %34 = call ptr @aiml_db_kv_open_typed_persist(ptr @22, i64 56, ptr %db_dir_val59, ptr @__aiml_sernodes_Ability, i32 11)
  store ptr %34, ptr %kv, align 8
  %ab_val = load %Ability, ptr %ab, align 8
  %kv_ptr = load ptr, ptr %kv, align 8
  %kv_val_tmp = alloca %Ability, align 8
  store %Ability %ab_val, ptr %kv_val_tmp, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @23, ptr %kv_val_tmp, i64 56)
  %got = alloca ptr, align 8
  store ptr null, ptr %got, align 8
  %kv_ptr60 = load ptr, ptr %kv, align 8
  %35 = call ptr @aiml_db_kv_get(ptr %kv_ptr60, ptr @24)
  store ptr %35, ptr %got, align 8
  %got_val = load ptr, ptr %got, align 8
  %36 = icmp eq ptr %got_val, null
  %37 = icmp eq ptr %got_val, null
  %38 = or i1 %36, true
  br i1 %38, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %if_merge53
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %if_merge53
  %39 = call i32 @strcmp(ptr %got_val, ptr null)
  %40 = icmp eq i32 %39, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %37, %cmp_check_null ], [ %40, %cmp_do_strcmp ]
  br i1 %strcmp_res, label %if_then61, label %if_merge62

if_then61:                                        ; preds = %cmp_merge
  %print_call63 = call i32 (ptr, ...) @printf(ptr @26, ptr @25)
  call void @exit(i32 1)
  br label %if_merge62

if_merge62:                                       ; preds = %if_then61, %cmp_merge
  %chain_base = load ptr, ptr %got, align 8
  %chain_gep64 = getelementptr inbounds %Ability, ptr %chain_base, i32 0, i32 1
  %sizePtr65 = getelementptr inbounds %GenericList, ptr %chain_gep64, i32 0, i32 0
  %sizeVal66 = load i32, ptr %sizePtr65, align 4
  %41 = icmp ne i32 %sizeVal66, 1
  br i1 %41, label %if_then67, label %if_merge68

if_then67:                                        ; preds = %if_merge62
  %chain_base69 = load ptr, ptr %got, align 8
  %chain_gep70 = getelementptr inbounds %Ability, ptr %chain_base69, i32 0, i32 1
  %sizePtr71 = getelementptr inbounds %GenericList, ptr %chain_gep70, i32 0, i32 0
  %sizeVal72 = load i32, ptr %sizePtr71, align 4
  %42 = call ptr @__aiml_int_to_string(i32 %sizeVal72)
  %43 = call ptr @__aiml_str_concat(ptr @27, ptr %42)
  %44 = call ptr @__aiml_str_concat(ptr %43, ptr @28)
  %print_null_check73 = icmp eq ptr %44, null
  %print_safe_str74 = select i1 %print_null_check73, ptr @print_null_str.8, ptr %44
  %print_call75 = call i32 (ptr, ...) @printf(ptr @29, ptr %print_safe_str74)
  call void @exit(i32 1)
  br label %if_merge68

if_merge68:                                       ; preds = %if_then67, %if_merge62
  %gcb = alloca %list_i32, align 8
  store %list_i32 zeroinitializer, ptr %gcb, align 8
  %sizePtr76 = getelementptr inbounds %GenericList, ptr %gcb, i32 0, i32 0
  store i32 0, ptr %sizePtr76, align 4
  %capPtr77 = getelementptr inbounds %GenericList, ptr %gcb, i32 0, i32 1
  store i32 4, ptr %capPtr77, align 4
  %45 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr78 = getelementptr inbounds %GenericList, ptr %gcb, i32 0, i32 2
  store ptr %45, ptr %dataPtr78, align 8
  %prop_ptr_base_w = load ptr, ptr %got, align 8
  %set_prop_ptr_w79 = getelementptr inbounds %Ability, ptr %prop_ptr_base_w, i32 0, i32 1
  %dataPtr80 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w79, i32 0, i32 2
  %dataBase81 = load ptr, ptr %dataPtr80, align 8
  %46 = getelementptr inbounds %list_i32, ptr %dataBase81, i32 0
  %47 = call ptr @memcpy(ptr %gcb, ptr %46, i64 16)
  %sizePtr82 = getelementptr inbounds %GenericList, ptr %gcb, i32 0, i32 0
  %sizeVal83 = load i32, ptr %sizePtr82, align 4
  %48 = icmp ne i32 %sizeVal83, 1
  br i1 %48, label %if_then84, label %if_merge85

if_then84:                                        ; preds = %if_merge68
  %sizePtr86 = getelementptr inbounds %GenericList, ptr %gcb, i32 0, i32 0
  %sizeVal87 = load i32, ptr %sizePtr86, align 4
  %49 = call ptr @__aiml_int_to_string(i32 %sizeVal87)
  %50 = call ptr @__aiml_str_concat(ptr @30, ptr %49)
  %51 = call ptr @__aiml_str_concat(ptr %50, ptr @31)
  %print_null_check88 = icmp eq ptr %51, null
  %print_safe_str89 = select i1 %print_null_check88, ptr @print_null_str.9, ptr %51
  %print_call90 = call i32 (ptr, ...) @printf(ptr @32, ptr %print_safe_str89)
  call void @exit(i32 1)
  br label %if_merge85

if_merge85:                                       ; preds = %if_then84, %if_merge68
  %gv0 = alloca i32, align 4
  store i32 0, ptr %gv0, align 4
  %dataPtr91 = getelementptr inbounds %GenericList, ptr %gcb, i32 0, i32 2
  %dataBase92 = load ptr, ptr %dataPtr91, align 8
  %52 = getelementptr inbounds i32, ptr %dataBase92, i32 0
  %getVal93 = load i32, ptr %52, align 4
  store i32 %getVal93, ptr %gv0, align 4
  %gv0_val = load i32, ptr %gv0, align 4
  %53 = icmp ne i32 %gv0_val, 6
  br i1 %53, label %if_then94, label %if_merge95

if_then94:                                        ; preds = %if_merge85
  %gv0_val96 = load i32, ptr %gv0, align 4
  %54 = call ptr @__aiml_int_to_string(i32 %gv0_val96)
  %55 = call ptr @__aiml_str_concat(ptr @33, ptr %54)
  %56 = call ptr @__aiml_str_concat(ptr %55, ptr @34)
  %print_null_check97 = icmp eq ptr %56, null
  %print_safe_str98 = select i1 %print_null_check97, ptr @print_null_str.10, ptr %56
  %print_call99 = call i32 (ptr, ...) @printf(ptr @35, ptr %print_safe_str98)
  call void @exit(i32 1)
  br label %if_merge95

if_merge95:                                       ; preds = %if_then94, %if_merge85
  %chain_base100 = load ptr, ptr %got, align 8
  %chain_gep101 = getelementptr inbounds %Ability, ptr %chain_base100, i32 0, i32 3
  %sizePtr102 = getelementptr inbounds %GenericList, ptr %chain_gep101, i32 0, i32 0
  %sizeVal103 = load i32, ptr %sizePtr102, align 4
  %57 = icmp ne i32 %sizeVal103, 1
  br i1 %57, label %if_then104, label %if_merge105

if_then104:                                       ; preds = %if_merge95
  %chain_base106 = load ptr, ptr %got, align 8
  %chain_gep107 = getelementptr inbounds %Ability, ptr %chain_base106, i32 0, i32 3
  %sizePtr108 = getelementptr inbounds %GenericList, ptr %chain_gep107, i32 0, i32 0
  %sizeVal109 = load i32, ptr %sizePtr108, align 4
  %58 = call ptr @__aiml_int_to_string(i32 %sizeVal109)
  %59 = call ptr @__aiml_str_concat(ptr @36, ptr %58)
  %60 = call ptr @__aiml_str_concat(ptr %59, ptr @37)
  %print_null_check110 = icmp eq ptr %60, null
  %print_safe_str111 = select i1 %print_null_check110, ptr @print_null_str.11, ptr %60
  %print_call112 = call i32 (ptr, ...) @printf(ptr @38, ptr %print_safe_str111)
  call void @exit(i32 1)
  br label %if_merge105

if_merge105:                                      ; preds = %if_then104, %if_merge95
  %geff0 = alloca %list_i32, align 8
  store %list_i32 zeroinitializer, ptr %geff0, align 8
  %sizePtr113 = getelementptr inbounds %GenericList, ptr %geff0, i32 0, i32 0
  store i32 0, ptr %sizePtr113, align 4
  %capPtr114 = getelementptr inbounds %GenericList, ptr %geff0, i32 0, i32 1
  store i32 4, ptr %capPtr114, align 4
  %61 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr115 = getelementptr inbounds %GenericList, ptr %geff0, i32 0, i32 2
  store ptr %61, ptr %dataPtr115, align 8
  %prop_ptr_base_w116 = load ptr, ptr %got, align 8
  %set_prop_ptr_w117 = getelementptr inbounds %Ability, ptr %prop_ptr_base_w116, i32 0, i32 3
  %dataPtr118 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w117, i32 0, i32 2
  %dataBase119 = load ptr, ptr %dataPtr118, align 8
  %62 = getelementptr inbounds %list_i32, ptr %dataBase119, i32 0
  %63 = call ptr @memcpy(ptr %geff0, ptr %62, i64 16)
  %sizePtr120 = getelementptr inbounds %GenericList, ptr %geff0, i32 0, i32 0
  %sizeVal121 = load i32, ptr %sizePtr120, align 4
  %64 = icmp ne i32 %sizeVal121, 2
  br i1 %64, label %if_then122, label %if_merge123

if_then122:                                       ; preds = %if_merge105
  %sizePtr124 = getelementptr inbounds %GenericList, ptr %geff0, i32 0, i32 0
  %sizeVal125 = load i32, ptr %sizePtr124, align 4
  %65 = call ptr @__aiml_int_to_string(i32 %sizeVal125)
  %66 = call ptr @__aiml_str_concat(ptr @39, ptr %65)
  %67 = call ptr @__aiml_str_concat(ptr %66, ptr @40)
  %print_null_check126 = icmp eq ptr %67, null
  %print_safe_str127 = select i1 %print_null_check126, ptr @print_null_str.12, ptr %67
  %print_call128 = call i32 (ptr, ...) @printf(ptr @41, ptr %print_safe_str127)
  call void @exit(i32 1)
  br label %if_merge123

if_merge123:                                      ; preds = %if_then122, %if_merge105
  %ge0 = alloca i32, align 4
  store i32 0, ptr %ge0, align 4
  %dataPtr129 = getelementptr inbounds %GenericList, ptr %geff0, i32 0, i32 2
  %dataBase130 = load ptr, ptr %dataPtr129, align 8
  %68 = getelementptr inbounds i32, ptr %dataBase130, i32 0
  %getVal131 = load i32, ptr %68, align 4
  store i32 %getVal131, ptr %ge0, align 4
  %ge0_val = load i32, ptr %ge0, align 4
  %69 = icmp ne i32 %ge0_val, 10
  br i1 %69, label %if_then132, label %if_merge133

if_then132:                                       ; preds = %if_merge123
  %ge0_val134 = load i32, ptr %ge0, align 4
  %70 = call ptr @__aiml_int_to_string(i32 %ge0_val134)
  %71 = call ptr @__aiml_str_concat(ptr @42, ptr %70)
  %72 = call ptr @__aiml_str_concat(ptr %71, ptr @43)
  %print_null_check135 = icmp eq ptr %72, null
  %print_safe_str136 = select i1 %print_null_check135, ptr @print_null_str.13, ptr %72
  %print_call137 = call i32 (ptr, ...) @printf(ptr @44, ptr %print_safe_str136)
  call void @exit(i32 1)
  br label %if_merge133

if_merge133:                                      ; preds = %if_then132, %if_merge123
  %print_call138 = call i32 (ptr, ...) @printf(ptr @46, ptr @45)
  %73 = call i32 @aiml_db_checkpoint_all()
  %print_call139 = call i32 (ptr, ...) @printf(ptr @48, ptr @47)
  ret i32 0

ret_dead:                                         ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

; Function Attrs: noinline
define internal ptr @__aiml_deser_Ability(ptr %json) #0 {
entry:
  %r = call ptr @aiml_jr_parse(ptr %json)
  %hp = call ptr @malloc(i64 56)
  call void @llvm.memset.p0.i64(ptr align 8 %hp, i8 0, i64 56, i1 false)
  %fp_cost_text = getelementptr inbounds %Ability, ptr %hp, i32 0, i32 0
  %sv = call ptr @aiml_jr_str(ptr %r, ptr @fk_cost_text)
  store ptr %sv, ptr %fp_cost_text, align 8
  %fp_activation_cost = getelementptr inbounds %Ability, ptr %hp, i32 0, i32 1
  %sizePtr = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  store ptr %0, ptr %dataPtr, align 8
  %cnt = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_activation_cost)
  br label %activation_cost_dlc

activation_cost_dlc:                              ; preds = %cont13, %entry
  %dli = phi i32 [ 0, %entry ], [ %ni, %cont13 ]
  %1 = icmp slt i32 %dli, %cnt
  br i1 %1, label %activation_cost_dlb, label %activation_cost_dle

activation_cost_dlb:                              ; preds = %activation_cost_dlc
  %ila = alloca %list_i32, align 8
  %sizePtr1 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  store i32 0, ptr %sizePtr1, align 4
  %capPtr2 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  store i32 4, ptr %capPtr2, align 4
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr3 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  store ptr %2, ptr %dataPtr3, align 8
  %isr = call ptr @aiml_jr_arr_sub(ptr %r, ptr @fk_activation_cost, i32 %dli)
  %3 = icmp ne ptr %isr, null
  br i1 %3, label %activation_cost_isrok, label %activation_cost_isrdone

activation_cost_dle:                              ; preds = %activation_cost_dlc
  %fp_trigger = getelementptr inbounds %Ability, ptr %hp, i32 0, i32 2
  %sizePtr17 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 0
  store i32 0, ptr %sizePtr17, align 4
  %capPtr18 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 1
  store i32 4, ptr %capPtr18, align 4
  %4 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr19 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  store ptr %4, ptr %dataPtr19, align 8
  %cnt20 = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_trigger)
  br label %trigger_dlc

activation_cost_isrok:                            ; preds = %activation_cost_dlb
  %icnt = call i32 @aiml_jr_val_len(ptr %isr)
  br label %activation_cost_ilc

activation_cost_isrdone:                          ; preds = %activation_cost_ile, %activation_cost_dlb
  %ilval = load %list_i32, ptr %ila, align 8
  %sizePtr8 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 0
  %capPtr9 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 1
  %size10 = load i32, ptr %sizePtr8, align 4
  %capacity11 = load i32, ptr %capPtr9, align 4
  %5 = icmp eq i32 %size10, %capacity11
  br i1 %5, label %grow12, label %cont13

activation_cost_ilc:                              ; preds = %cont, %activation_cost_isrok
  %iidx = phi i32 [ 0, %activation_cost_isrok ], [ %ini, %cont ]
  %6 = icmp slt i32 %iidx, %icnt
  br i1 %6, label %activation_cost_ilb, label %activation_cost_ile

activation_cost_ilb:                              ; preds = %activation_cost_ilc
  %ve = call i32 @aiml_jr_val_i32(ptr %isr, i32 %iidx)
  %sizePtr4 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  %capPtr5 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  %size = load i32, ptr %sizePtr4, align 4
  %capacity = load i32, ptr %capPtr5, align 4
  %7 = icmp eq i32 %size, %capacity
  br i1 %7, label %grow, label %cont

activation_cost_ile:                              ; preds = %activation_cost_ilc
  call void @aiml_jr_free(ptr %isr)
  br label %activation_cost_isrdone

grow:                                             ; preds = %activation_cost_ilb
  %8 = mul i32 %capacity, 2
  %9 = icmp sgt i32 %8, 0
  %10 = select i1 %9, i32 %8, i32 4
  store i32 %10, ptr %capPtr5, align 4
  %dataPtr6 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %11 = load ptr, ptr %dataPtr6, align 8
  %12 = zext i32 %10 to i64
  %13 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %12
  %14 = call ptr @realloc(ptr %11, i64 %13)
  store ptr %14, ptr %dataPtr6, align 8
  br label %cont

cont:                                             ; preds = %grow, %activation_cost_ilb
  %dataPtr7 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr7, align 8
  %15 = getelementptr inbounds i32, ptr %dataBase, i32 %size
  store i32 %ve, ptr %15, align 4
  %16 = add i32 %size, 1
  store i32 %16, ptr %sizePtr4, align 4
  %ini = add i32 %iidx, 1
  br label %activation_cost_ilc

grow12:                                           ; preds = %activation_cost_isrdone
  %17 = mul i32 %capacity11, 2
  %18 = icmp sgt i32 %17, 0
  %19 = select i1 %18, i32 %17, i32 4
  store i32 %19, ptr %capPtr9, align 4
  %dataPtr14 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  %20 = load ptr, ptr %dataPtr14, align 8
  %21 = zext i32 %19 to i64
  %22 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), %21
  %23 = call ptr @realloc(ptr %20, i64 %22)
  store ptr %23, ptr %dataPtr14, align 8
  br label %cont13

cont13:                                           ; preds = %grow12, %activation_cost_isrdone
  %dataPtr15 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  %dataBase16 = load ptr, ptr %dataPtr15, align 8
  %24 = getelementptr inbounds %list_i32, ptr %dataBase16, i32 %size10
  store %list_i32 %ilval, ptr %24, align 8
  %25 = add i32 %size10, 1
  store i32 %25, ptr %sizePtr8, align 4
  %ni = add i32 %dli, 1
  br label %activation_cost_dlc

trigger_dlc:                                      ; preds = %cont46, %activation_cost_dle
  %dli21 = phi i32 [ 0, %activation_cost_dle ], [ %ni50, %cont46 ]
  %26 = icmp slt i32 %dli21, %cnt20
  br i1 %26, label %trigger_dlb, label %trigger_dle

trigger_dlb:                                      ; preds = %trigger_dlc
  %ila22 = alloca %list_i32, align 8
  %sizePtr23 = getelementptr inbounds %GenericList, ptr %ila22, i32 0, i32 0
  store i32 0, ptr %sizePtr23, align 4
  %capPtr24 = getelementptr inbounds %GenericList, ptr %ila22, i32 0, i32 1
  store i32 4, ptr %capPtr24, align 4
  %27 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr25 = getelementptr inbounds %GenericList, ptr %ila22, i32 0, i32 2
  store ptr %27, ptr %dataPtr25, align 8
  %isr26 = call ptr @aiml_jr_arr_sub(ptr %r, ptr @fk_trigger, i32 %dli21)
  %28 = icmp ne ptr %isr26, null
  br i1 %28, label %trigger_isrok, label %trigger_isrdone

trigger_dle:                                      ; preds = %trigger_dlc
  %fp_effects = getelementptr inbounds %Ability, ptr %hp, i32 0, i32 3
  %sizePtr51 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 0
  store i32 0, ptr %sizePtr51, align 4
  %capPtr52 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 1
  store i32 4, ptr %capPtr52, align 4
  %29 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr53 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  store ptr %29, ptr %dataPtr53, align 8
  %cnt54 = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_effects)
  br label %effects_dlc

trigger_isrok:                                    ; preds = %trigger_dlb
  %icnt27 = call i32 @aiml_jr_val_len(ptr %isr26)
  br label %trigger_ilc

trigger_isrdone:                                  ; preds = %trigger_ile, %trigger_dlb
  %ilval40 = load %list_i32, ptr %ila22, align 8
  %sizePtr41 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 0
  %capPtr42 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 1
  %size43 = load i32, ptr %sizePtr41, align 4
  %capacity44 = load i32, ptr %capPtr42, align 4
  %30 = icmp eq i32 %size43, %capacity44
  br i1 %30, label %grow45, label %cont46

trigger_ilc:                                      ; preds = %cont35, %trigger_isrok
  %iidx28 = phi i32 [ 0, %trigger_isrok ], [ %ini39, %cont35 ]
  %31 = icmp slt i32 %iidx28, %icnt27
  br i1 %31, label %trigger_ilb, label %trigger_ile

trigger_ilb:                                      ; preds = %trigger_ilc
  %ve29 = call i32 @aiml_jr_val_i32(ptr %isr26, i32 %iidx28)
  %sizePtr30 = getelementptr inbounds %GenericList, ptr %ila22, i32 0, i32 0
  %capPtr31 = getelementptr inbounds %GenericList, ptr %ila22, i32 0, i32 1
  %size32 = load i32, ptr %sizePtr30, align 4
  %capacity33 = load i32, ptr %capPtr31, align 4
  %32 = icmp eq i32 %size32, %capacity33
  br i1 %32, label %grow34, label %cont35

trigger_ile:                                      ; preds = %trigger_ilc
  call void @aiml_jr_free(ptr %isr26)
  br label %trigger_isrdone

grow34:                                           ; preds = %trigger_ilb
  %33 = mul i32 %capacity33, 2
  %34 = icmp sgt i32 %33, 0
  %35 = select i1 %34, i32 %33, i32 4
  store i32 %35, ptr %capPtr31, align 4
  %dataPtr36 = getelementptr inbounds %GenericList, ptr %ila22, i32 0, i32 2
  %36 = load ptr, ptr %dataPtr36, align 8
  %37 = zext i32 %35 to i64
  %38 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %37
  %39 = call ptr @realloc(ptr %36, i64 %38)
  store ptr %39, ptr %dataPtr36, align 8
  br label %cont35

cont35:                                           ; preds = %grow34, %trigger_ilb
  %dataPtr37 = getelementptr inbounds %GenericList, ptr %ila22, i32 0, i32 2
  %dataBase38 = load ptr, ptr %dataPtr37, align 8
  %40 = getelementptr inbounds i32, ptr %dataBase38, i32 %size32
  store i32 %ve29, ptr %40, align 4
  %41 = add i32 %size32, 1
  store i32 %41, ptr %sizePtr30, align 4
  %ini39 = add i32 %iidx28, 1
  br label %trigger_ilc

grow45:                                           ; preds = %trigger_isrdone
  %42 = mul i32 %capacity44, 2
  %43 = icmp sgt i32 %42, 0
  %44 = select i1 %43, i32 %42, i32 4
  store i32 %44, ptr %capPtr42, align 4
  %dataPtr47 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  %45 = load ptr, ptr %dataPtr47, align 8
  %46 = zext i32 %44 to i64
  %47 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), %46
  %48 = call ptr @realloc(ptr %45, i64 %47)
  store ptr %48, ptr %dataPtr47, align 8
  br label %cont46

cont46:                                           ; preds = %grow45, %trigger_isrdone
  %dataPtr48 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  %dataBase49 = load ptr, ptr %dataPtr48, align 8
  %49 = getelementptr inbounds %list_i32, ptr %dataBase49, i32 %size43
  store %list_i32 %ilval40, ptr %49, align 8
  %50 = add i32 %size43, 1
  store i32 %50, ptr %sizePtr41, align 4
  %ni50 = add i32 %dli21, 1
  br label %trigger_dlc

effects_dlc:                                      ; preds = %cont80, %trigger_dle
  %dli55 = phi i32 [ 0, %trigger_dle ], [ %ni84, %cont80 ]
  %51 = icmp slt i32 %dli55, %cnt54
  br i1 %51, label %effects_dlb, label %effects_dle

effects_dlb:                                      ; preds = %effects_dlc
  %ila56 = alloca %list_i32, align 8
  %sizePtr57 = getelementptr inbounds %GenericList, ptr %ila56, i32 0, i32 0
  store i32 0, ptr %sizePtr57, align 4
  %capPtr58 = getelementptr inbounds %GenericList, ptr %ila56, i32 0, i32 1
  store i32 4, ptr %capPtr58, align 4
  %52 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr59 = getelementptr inbounds %GenericList, ptr %ila56, i32 0, i32 2
  store ptr %52, ptr %dataPtr59, align 8
  %isr60 = call ptr @aiml_jr_arr_sub(ptr %r, ptr @fk_effects, i32 %dli55)
  %53 = icmp ne ptr %isr60, null
  br i1 %53, label %effects_isrok, label %effects_isrdone

effects_dle:                                      ; preds = %effects_dlc
  call void @aiml_jr_free(ptr %r)
  ret ptr %hp

effects_isrok:                                    ; preds = %effects_dlb
  %icnt61 = call i32 @aiml_jr_val_len(ptr %isr60)
  br label %effects_ilc

effects_isrdone:                                  ; preds = %effects_ile, %effects_dlb
  %ilval74 = load %list_i32, ptr %ila56, align 8
  %sizePtr75 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 0
  %capPtr76 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 1
  %size77 = load i32, ptr %sizePtr75, align 4
  %capacity78 = load i32, ptr %capPtr76, align 4
  %54 = icmp eq i32 %size77, %capacity78
  br i1 %54, label %grow79, label %cont80

effects_ilc:                                      ; preds = %cont69, %effects_isrok
  %iidx62 = phi i32 [ 0, %effects_isrok ], [ %ini73, %cont69 ]
  %55 = icmp slt i32 %iidx62, %icnt61
  br i1 %55, label %effects_ilb, label %effects_ile

effects_ilb:                                      ; preds = %effects_ilc
  %ve63 = call i32 @aiml_jr_val_i32(ptr %isr60, i32 %iidx62)
  %sizePtr64 = getelementptr inbounds %GenericList, ptr %ila56, i32 0, i32 0
  %capPtr65 = getelementptr inbounds %GenericList, ptr %ila56, i32 0, i32 1
  %size66 = load i32, ptr %sizePtr64, align 4
  %capacity67 = load i32, ptr %capPtr65, align 4
  %56 = icmp eq i32 %size66, %capacity67
  br i1 %56, label %grow68, label %cont69

effects_ile:                                      ; preds = %effects_ilc
  call void @aiml_jr_free(ptr %isr60)
  br label %effects_isrdone

grow68:                                           ; preds = %effects_ilb
  %57 = mul i32 %capacity67, 2
  %58 = icmp sgt i32 %57, 0
  %59 = select i1 %58, i32 %57, i32 4
  store i32 %59, ptr %capPtr65, align 4
  %dataPtr70 = getelementptr inbounds %GenericList, ptr %ila56, i32 0, i32 2
  %60 = load ptr, ptr %dataPtr70, align 8
  %61 = zext i32 %59 to i64
  %62 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %61
  %63 = call ptr @realloc(ptr %60, i64 %62)
  store ptr %63, ptr %dataPtr70, align 8
  br label %cont69

cont69:                                           ; preds = %grow68, %effects_ilb
  %dataPtr71 = getelementptr inbounds %GenericList, ptr %ila56, i32 0, i32 2
  %dataBase72 = load ptr, ptr %dataPtr71, align 8
  %64 = getelementptr inbounds i32, ptr %dataBase72, i32 %size66
  store i32 %ve63, ptr %64, align 4
  %65 = add i32 %size66, 1
  store i32 %65, ptr %sizePtr64, align 4
  %ini73 = add i32 %iidx62, 1
  br label %effects_ilc

grow79:                                           ; preds = %effects_isrdone
  %66 = mul i32 %capacity78, 2
  %67 = icmp sgt i32 %66, 0
  %68 = select i1 %67, i32 %66, i32 4
  store i32 %68, ptr %capPtr76, align 4
  %dataPtr81 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  %69 = load ptr, ptr %dataPtr81, align 8
  %70 = zext i32 %68 to i64
  %71 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), %70
  %72 = call ptr @realloc(ptr %69, i64 %71)
  store ptr %72, ptr %dataPtr81, align 8
  br label %cont80

cont80:                                           ; preds = %grow79, %effects_isrdone
  %dataPtr82 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  %dataBase83 = load ptr, ptr %dataPtr82, align 8
  %73 = getelementptr inbounds %list_i32, ptr %dataBase83, i32 %size77
  store %list_i32 %ilval74, ptr %73, align 8
  %74 = add i32 %size77, 1
  store i32 %74, ptr %sizePtr75, align 4
  %ni84 = add i32 %dli55, 1
  br label %effects_dlc
}

declare ptr @aiml_jr_parse(ptr)

declare ptr @malloc(i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #1

declare ptr @aiml_jr_str(ptr, ptr)

declare i32 @aiml_jr_arr_len(ptr, ptr)

declare ptr @aiml_jr_arr_sub(ptr, ptr, i32)

declare i32 @aiml_jr_val_len(ptr)

declare i32 @aiml_jr_val_i32(ptr, i32)

declare ptr @realloc(ptr, i64)

declare void @aiml_jr_free(ptr)

declare ptr @memcpy(ptr, ptr, i64)

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

declare void @exit(i32)

declare ptr @aiml_path_intern(ptr)

declare i32 @aiml_io_remove_all(ptr)

declare ptr @aiml_db_kv_open_typed_persist(ptr, i64, ptr, ptr, i32)

declare void @aiml_db_kv_set(ptr, ptr, ptr, i64)

declare ptr @aiml_db_kv_get(ptr, ptr)

declare i32 @strcmp(ptr, ptr)

declare i32 @aiml_db_checkpoint_all()

attributes #0 = { noinline }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: write) }
