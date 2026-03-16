; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [8 x i8] c"verbose\00", align 1
@1 = private unnamed_addr constant [25 x i8] c"phase A: verbose message\00", align 1
@log_loc = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@2 = private unnamed_addr constant [23 x i8] c"phase A: debug message\00", align 1
@log_loc.1 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@3 = private unnamed_addr constant [22 x i8] c"phase A: info message\00", align 1
@log_loc.2 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@4 = private unnamed_addr constant [22 x i8] c"phase A: warn message\00", align 1
@log_loc.3 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@5 = private unnamed_addr constant [23 x i8] c"phase A: error message\00", align 1
@log_loc.4 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@6 = private unnamed_addr constant [33 x i8] c"phase A: deprecated feature used\00", align 1
@log_loc.5 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@7 = private unnamed_addr constant [27 x i8] c"phase A: system diagnostic\00", align 1
@log_loc.6 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@8 = private unnamed_addr constant [13 x i8] c"phase A done\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [5 x i8] c"warn\00", align 1
@11 = private unnamed_addr constant [27 x i8] c"phase B: should NOT appear\00", align 1
@log_loc.7 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@12 = private unnamed_addr constant [27 x i8] c"phase B: should NOT appear\00", align 1
@log_loc.8 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@13 = private unnamed_addr constant [27 x i8] c"phase B: should NOT appear\00", align 1
@log_loc.9 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@14 = private unnamed_addr constant [28 x i8] c"phase B: warn SHOULD appear\00", align 1
@log_loc.10 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@15 = private unnamed_addr constant [29 x i8] c"phase B: error SHOULD appear\00", align 1
@log_loc.11 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@16 = private unnamed_addr constant [35 x i8] c"phase B: deprecated ALWAYS appears\00", align 1
@log_loc.12 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@17 = private unnamed_addr constant [31 x i8] c"phase B: system ALWAYS appears\00", align 1
@log_loc.13 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@18 = private unnamed_addr constant [13 x i8] c"phase B done\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [10 x i8] c"127.0.0.1\00", align 1
@21 = private unnamed_addr constant [5 x i8] c"info\00", align 1
@22 = private unnamed_addr constant [21 x i8] c"server listening on \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@23 = private unnamed_addr constant [2 x i8] c":\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@log_loc.15 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@24 = private unnamed_addr constant [13 x i8] c"phase C done\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [23 x i8] c"/tmp/aiml_test_log.txt\00", align 1
@27 = private unnamed_addr constant [38 x i8] c"phase D: this goes to file AND stderr\00", align 1
@log_loc.17 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@28 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@29 = private unnamed_addr constant [13 x i8] c"phase D done\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@30 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@31 = private unnamed_addr constant [5 x i8] c"info\00", align 1
@32 = private unnamed_addr constant [40 x i8] c"phase E: should NOT appear (below info)\00", align 1
@log_loc.19 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@33 = private unnamed_addr constant [33 x i8] c"phase E: should appear (at info)\00", align 1
@log_loc.20 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@34 = private unnamed_addr constant [8 x i8] c"verbose\00", align 1
@35 = private unnamed_addr constant [15 x i8] c"fn_log_basic: \00", align 1
@log_loc.21 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@36 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@37 = private unnamed_addr constant [13 x i8] c"phase F done\00", align 1
@print_null_str.22 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [10 x i8] c"x is zero\00", align 1
@log_loc.23 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@40 = private unnamed_addr constant [14 x i8] c"x is not zero\00", align 1
@log_loc.24 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@41 = private unnamed_addr constant [15 x i8] c"fn_log_if done\00", align 1
@log_loc.25 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@42 = private unnamed_addr constant [13 x i8] c"phase G done\00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@44 = private unnamed_addr constant [16 x i8] c"returning early\00", align 1
@log_loc.27 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@45 = private unnamed_addr constant [6 x i8] c"early\00", align 1
@46 = private unnamed_addr constant [17 x i8] c"returning normal\00", align 1
@log_loc.28 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@47 = private unnamed_addr constant [7 x i8] c"normal\00", align 1
@48 = private unnamed_addr constant [20 x i8] c"fn_log_if_ret(0) = \00", align 1
@print_null_str.29 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@49 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@50 = private unnamed_addr constant [20 x i8] c"fn_log_if_ret(1) = \00", align 1
@print_null_str.30 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@51 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@52 = private unnamed_addr constant [13 x i8] c"phase H done\00", align 1
@print_null_str.31 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@54 = private unnamed_addr constant [8 x i8] c"loop i=\00", align 1
@log_loc.32 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@55 = private unnamed_addr constant [21 x i8] c"loop finished after \00", align 1
@56 = private unnamed_addr constant [12 x i8] c" iterations\00", align 1
@log_loc.33 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@57 = private unnamed_addr constant [13 x i8] c"phase I done\00", align 1
@print_null_str.34 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@58 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@59 = private unnamed_addr constant [6 x i8] c"a > 0\00", align 1
@log_loc.35 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@60 = private unnamed_addr constant [10 x i8] c"b > 0 too\00", align 1
@log_loc.36 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@61 = private unnamed_addr constant [7 x i8] c"b <= 0\00", align 1
@log_loc.37 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@62 = private unnamed_addr constant [7 x i8] c"a <= 0\00", align 1
@log_loc.38 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@63 = private unnamed_addr constant [15 x i8] c"nested(1,1) = \00", align 1
@print_null_str.39 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@64 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@65 = private unnamed_addr constant [15 x i8] c"nested(1,0) = \00", align 1
@print_null_str.40 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@66 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@67 = private unnamed_addr constant [15 x i8] c"nested(0,5) = \00", align 1
@print_null_str.41 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@68 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@69 = private unnamed_addr constant [13 x i8] c"phase J done\00", align 1
@print_null_str.42 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@70 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@71 = private unnamed_addr constant [2 x i8] c"v\00", align 1
@log_loc.43 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@72 = private unnamed_addr constant [2 x i8] c"d\00", align 1
@log_loc.44 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@73 = private unnamed_addr constant [2 x i8] c"i\00", align 1
@log_loc.45 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@74 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@log_loc.46 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@75 = private unnamed_addr constant [2 x i8] c"e\00", align 1
@log_loc.47 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@76 = private unnamed_addr constant [4 x i8] c"dep\00", align 1
@log_loc.48 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@77 = private unnamed_addr constant [4 x i8] c"sys\00", align 1
@log_loc.49 = private unnamed_addr constant [22 x i8] c"tests/test_log.aiml:?\00", align 1
@78 = private unnamed_addr constant [13 x i8] c"phase K done\00", align 1
@print_null_str.50 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@79 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@80 = private unnamed_addr constant [19 x i8] c"all log tests done\00", align 1
@print_null_str.51 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@81 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [6 x [16 x ptr]] [[16 x ptr] [ptr @fn_log_basic, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @fn_log_if, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @fn_log_if_ret, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @fn_log_while, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @fn_nested_if_log, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @fn_all_levels, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [6 x ptr] [ptr @fn_log_basic, ptr @fn_log_if, ptr @fn_log_if_ret, ptr @fn_log_while, ptr @fn_nested_if_log, ptr @fn_all_levels]
@__aiml_fptable_versions = global [6 x i32] [i32 1, i32 1, i32 1, i32 1, i32 1, i32 1]
@__aiml_fname_fn_log_basic = private constant [13 x i8] c"fn_log_basic\00"
@__aiml_fname_fn_log_if = private constant [10 x i8] c"fn_log_if\00"
@__aiml_fname_fn_log_if_ret = private constant [14 x i8] c"fn_log_if_ret\00"
@__aiml_fname_fn_log_while = private constant [13 x i8] c"fn_log_while\00"
@__aiml_fname_fn_nested_if_log = private constant [17 x i8] c"fn_nested_if_log\00"
@__aiml_fname_fn_all_levels = private constant [14 x i8] c"fn_all_levels\00"
@__aiml_func_names = constant [6 x ptr] [ptr @__aiml_fname_fn_log_basic, ptr @__aiml_fname_fn_log_if, ptr @__aiml_fname_fn_log_if_ret, ptr @__aiml_fname_fn_log_while, ptr @__aiml_fname_fn_nested_if_log, ptr @__aiml_fname_fn_all_levels]
@__aiml_func_count = constant i32 6
@__aiml_schema_count = constant i32 0

define void @fn_log_basic(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %tag = alloca ptr, align 8
  store ptr %0, ptr %tag, align 8
  %tag_val = load ptr, ptr %tag, align 8
  %1 = call ptr @__aiml_str_concat(ptr @35, ptr %tag_val)
  call void @aiml_log_info(ptr %1, ptr @log_loc.21)
  ret void
}

define void @fn_log_if(i32 %0) personality ptr @__gxx_personality_v0 {
entry:
  %x = alloca i32, align 4
  store i32 %0, ptr %x, align 4
  %x_val = load i32, ptr %x, align 4
  %1 = icmp eq i32 %x_val, 0
  br i1 %1, label %if_then, label %if_else

if_then:                                          ; preds = %entry
  call void @aiml_log_debug(ptr @39, ptr @log_loc.23)
  br label %if_merge

if_else:                                          ; preds = %entry
  call void @aiml_log_debug(ptr @40, ptr @log_loc.24)
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  call void @aiml_log_info(ptr @41, ptr @log_loc.25)
  ret void
}

define ptr @fn_log_if_ret(i32 %0) personality ptr @__gxx_personality_v0 {
entry:
  %x = alloca i32, align 4
  store i32 %0, ptr %x, align 4
  %x_val = load i32, ptr %x, align 4
  %1 = icmp eq i32 %x_val, 0
  br i1 %1, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  call void @aiml_log_warn(ptr @44, ptr @log_loc.27)
  ret ptr @45

if_merge:                                         ; preds = %entry
  call void @aiml_log_info(ptr @46, ptr @log_loc.28)
  ret ptr @47
}

define void @fn_log_while(i32 %0) personality ptr @__gxx_personality_v0 {
entry:
  %n = alloca i32, align 4
  store i32 %0, ptr %n, align 4
  %i = alloca i32, align 4
  store i32 0, ptr %i, align 4
  store i32 0, ptr %i, align 4
  br label %while_loop

while_loop:                                       ; preds = %while_body, %entry
  %i_val = load i32, ptr %i, align 4
  %n_val = load i32, ptr %n, align 4
  %1 = icmp slt i32 %i_val, %n_val
  br i1 %1, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %i_val1 = load i32, ptr %i, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %i_val1)
  %3 = call ptr @__aiml_str_concat(ptr @54, ptr %2)
  call void @aiml_log_debug(ptr %3, ptr @log_loc.32)
  %i_val2 = load i32, ptr %i, align 4
  %4 = add i32 %i_val2, 1
  store i32 %4, ptr %i, align 4
  br label %while_loop

while_end:                                        ; preds = %while_loop
  %n_val3 = load i32, ptr %n, align 4
  %5 = call ptr @__aiml_int_to_string(i32 %n_val3)
  %6 = call ptr @__aiml_str_concat(ptr @55, ptr %5)
  %7 = call ptr @__aiml_str_concat(ptr %6, ptr @56)
  call void @aiml_log_info(ptr %7, ptr @log_loc.33)
  ret void
}

define i32 @fn_nested_if_log(i32 %0, i32 %1) personality ptr @__gxx_personality_v0 {
entry:
  %a = alloca i32, align 4
  store i32 %0, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 %1, ptr %b, align 4
  %a_val = load i32, ptr %a, align 4
  %2 = icmp sgt i32 %a_val, 0
  br i1 %2, label %if_then, label %if_else

if_then:                                          ; preds = %entry
  call void @aiml_log_debug(ptr @59, ptr @log_loc.35)
  %b_val = load i32, ptr %b, align 4
  %3 = icmp sgt i32 %b_val, 0
  br i1 %3, label %if_then1, label %if_else2

if_else:                                          ; preds = %entry
  call void @aiml_log_warn(ptr @62, ptr @log_loc.38)
  ret i32 -1

if_merge:                                         ; preds = %if_merge3
  ret i32 0

if_then1:                                         ; preds = %if_then
  call void @aiml_log_debug(ptr @60, ptr @log_loc.36)
  ret i32 1

if_else2:                                         ; preds = %if_then
  call void @aiml_log_warn(ptr @61, ptr @log_loc.37)
  ret i32 0

if_merge3:                                        ; No predecessors!
  br label %if_merge
}

define void @fn_all_levels() personality ptr @__gxx_personality_v0 {
entry:
  call void @aiml_log_verbose(ptr @71, ptr @log_loc.43)
  call void @aiml_log_debug(ptr @72, ptr @log_loc.44)
  call void @aiml_log_info(ptr @73, ptr @log_loc.45)
  call void @aiml_log_warn(ptr @74, ptr @log_loc.46)
  call void @aiml_log_error(ptr @75, ptr @log_loc.47)
  call void @aiml_log_deprecated(ptr @76, ptr @log_loc.48)
  call void @aiml_log_system(ptr @77, ptr @log_loc.49)
  ret void
}

define void @__aiml_init() {
entry:
  call void @aiml_log_set_level(ptr @0)
  call void @aiml_log_verbose(ptr @1, ptr @log_loc)
  call void @aiml_log_debug(ptr @2, ptr @log_loc.1)
  call void @aiml_log_info(ptr @3, ptr @log_loc.2)
  call void @aiml_log_warn(ptr @4, ptr @log_loc.3)
  call void @aiml_log_error(ptr @5, ptr @log_loc.4)
  call void @aiml_log_deprecated(ptr @6, ptr @log_loc.5)
  call void @aiml_log_system(ptr @7, ptr @log_loc.6)
  %print_call = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  call void @aiml_log_set_level(ptr @10)
  call void @aiml_log_verbose(ptr @11, ptr @log_loc.7)
  call void @aiml_log_debug(ptr @12, ptr @log_loc.8)
  call void @aiml_log_info(ptr @13, ptr @log_loc.9)
  call void @aiml_log_warn(ptr @14, ptr @log_loc.10)
  call void @aiml_log_error(ptr @15, ptr @log_loc.11)
  call void @aiml_log_deprecated(ptr @16, ptr @log_loc.12)
  call void @aiml_log_system(ptr @17, ptr @log_loc.13)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @19, ptr @18)
  %port = alloca i32, align 4
  store i32 0, ptr %port, align 4
  store i32 8080, ptr %port, align 4
  %host = alloca ptr, align 8
  store ptr null, ptr %host, align 8
  store ptr @20, ptr %host, align 8
  call void @aiml_log_set_level(ptr @21)
  %host_val = load ptr, ptr %host, align 8
  %0 = call ptr @__aiml_str_concat(ptr @22, ptr %host_val)
  %1 = call ptr @__aiml_str_concat(ptr %0, ptr @23)
  %port_val = load i32, ptr %port, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %port_val)
  %3 = call ptr @__aiml_str_concat(ptr %1, ptr %2)
  call void @aiml_log_info(ptr %3, ptr @log_loc.15)
  %print_call2 = call i32 (ptr, ...) @printf(ptr @25, ptr @24)
  call void @aiml_log_set_file(ptr @26)
  call void @aiml_log_info(ptr @27, ptr @log_loc.17)
  call void @aiml_log_set_file(ptr @28)
  %print_call3 = call i32 (ptr, ...) @printf(ptr @30, ptr @29)
  call void @aiml_log_set_level(ptr @31)
  call void @aiml_log_debug(ptr @32, ptr @log_loc.19)
  call void @aiml_log_info(ptr @33, ptr @log_loc.20)
  call void @aiml_log_set_level(ptr @34)
  call void @fn_log_basic(ptr @36)
  %print_call4 = call i32 (ptr, ...) @printf(ptr @38, ptr @37)
  call void @fn_log_if(i32 0)
  call void @fn_log_if(i32 1)
  %print_call5 = call i32 (ptr, ...) @printf(ptr @43, ptr @42)
  %r1 = alloca ptr, align 8
  store ptr null, ptr %r1, align 8
  %4 = call ptr @fn_log_if_ret(i32 0)
  store ptr %4, ptr %r1, align 8
  %r1_val = load ptr, ptr %r1, align 8
  %5 = call ptr @__aiml_str_concat(ptr @48, ptr %r1_val)
  %print_null_check = icmp eq ptr %5, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.29, ptr %5
  %print_call6 = call i32 (ptr, ...) @printf(ptr @49, ptr %print_safe_str)
  %r2 = alloca ptr, align 8
  store ptr null, ptr %r2, align 8
  %6 = call ptr @fn_log_if_ret(i32 1)
  store ptr %6, ptr %r2, align 8
  %r2_val = load ptr, ptr %r2, align 8
  %7 = call ptr @__aiml_str_concat(ptr @50, ptr %r2_val)
  %print_null_check7 = icmp eq ptr %7, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.30, ptr %7
  %print_call9 = call i32 (ptr, ...) @printf(ptr @51, ptr %print_safe_str8)
  %print_call10 = call i32 (ptr, ...) @printf(ptr @53, ptr @52)
  call void @fn_log_while(i32 3)
  %print_call11 = call i32 (ptr, ...) @printf(ptr @58, ptr @57)
  %v1 = alloca i32, align 4
  store i32 0, ptr %v1, align 4
  %8 = call i32 @fn_nested_if_log(i32 1, i32 1)
  store i32 %8, ptr %v1, align 4
  %v1_val = load i32, ptr %v1, align 4
  %9 = call ptr @__aiml_int_to_string(i32 %v1_val)
  %10 = call ptr @__aiml_str_concat(ptr @63, ptr %9)
  %print_null_check12 = icmp eq ptr %10, null
  %print_safe_str13 = select i1 %print_null_check12, ptr @print_null_str.39, ptr %10
  %print_call14 = call i32 (ptr, ...) @printf(ptr @64, ptr %print_safe_str13)
  %v2 = alloca i32, align 4
  store i32 0, ptr %v2, align 4
  %11 = call i32 @fn_nested_if_log(i32 1, i32 0)
  store i32 %11, ptr %v2, align 4
  %v2_val = load i32, ptr %v2, align 4
  %12 = call ptr @__aiml_int_to_string(i32 %v2_val)
  %13 = call ptr @__aiml_str_concat(ptr @65, ptr %12)
  %print_null_check15 = icmp eq ptr %13, null
  %print_safe_str16 = select i1 %print_null_check15, ptr @print_null_str.40, ptr %13
  %print_call17 = call i32 (ptr, ...) @printf(ptr @66, ptr %print_safe_str16)
  %v3 = alloca i32, align 4
  store i32 0, ptr %v3, align 4
  %14 = call i32 @fn_nested_if_log(i32 0, i32 5)
  store i32 %14, ptr %v3, align 4
  %v3_val = load i32, ptr %v3, align 4
  %15 = call ptr @__aiml_int_to_string(i32 %v3_val)
  %16 = call ptr @__aiml_str_concat(ptr @67, ptr %15)
  %print_null_check18 = icmp eq ptr %16, null
  %print_safe_str19 = select i1 %print_null_check18, ptr @print_null_str.41, ptr %16
  %print_call20 = call i32 (ptr, ...) @printf(ptr @68, ptr %print_safe_str19)
  %print_call21 = call i32 (ptr, ...) @printf(ptr @70, ptr @69)
  call void @fn_all_levels()
  %print_call22 = call i32 (ptr, ...) @printf(ptr @79, ptr @78)
  %print_call23 = call i32 (ptr, ...) @printf(ptr @81, ptr @80)
  ret void
}

declare void @aiml_log_set_level(ptr)

declare void @aiml_log_verbose(ptr, ptr)

declare void @aiml_log_debug(ptr, ptr)

declare void @aiml_log_info(ptr, ptr)

declare void @aiml_log_warn(ptr, ptr)

declare void @aiml_log_error(ptr, ptr)

declare void @aiml_log_deprecated(ptr, ptr)

declare void @aiml_log_system(ptr, ptr)

declare i32 @printf(ptr, ...)

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

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

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare void @aiml_log_set_file(ptr)

declare i32 @__gxx_personality_v0(...)

attributes #0 = { noinline }
