; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@__global_pass = global i32 0
@__global_fail = global i32 0
@__global_s = global ptr null
@__global_tok = global ptr null
@__global_bearer = global ptr null
@__global_mid = global ptr null
@__global_tail_empty = global ptr null
@__global_full_copy = global ptr null
@__global_zero_len = global ptr null
@__global_single_char = global ptr null
@__global_empty_s = global ptr null
@__global_empty_sub = global ptr null
@__global_sw1 = global i32 0
@__global_sw2 = global i32 0
@__global_sw3 = global i32 0
@__global_sw4 = global i32 0
@__global_sw5 = global i32 0
@__global_sw6 = global i32 0
@__global_sw7 = global i32 0
@__global_idx1 = global i32 0
@__global_idx2 = global i32 0
@__global_idx3 = global i32 0
@__global_idx4 = global i32 0
@__global_idx5 = global i32 0
@__global_idx6 = global i32 0
@__global_idx7 = global i32 0
@__global_idx8 = global i32 0
@__global_idx9 = global i32 0
@__global_pi1 = global ptr null
@__global_pi1_val = global i32 0
@__global_pi2 = global ptr null
@__global_pi2_val = global i32 0
@__global_pi3 = global ptr null
@__global_pi3_val = global i32 0
@__global_pi4 = global ptr null
@__global_pi4_val = global i32 0
@__global_pi5 = global ptr null
@__global_pi5_val = global i32 0
@__global_pi6 = global ptr null
@__global_pi6_val = global i32 0
@__global_pf1 = global ptr null
@__global_pf1_val = global float 0.000000e+00
@__global_pf2 = global ptr null
@__global_pf2_val = global float 0.000000e+00
@__global_pf3 = global ptr null
@__global_pf3_val = global float 0.000000e+00
@__global_pf4 = global ptr null
@__global_pf4_val = global float 0.000000e+00
@__global_pf5 = global ptr null
@__global_pf5_val = global float 0.000000e+00
@__global_pf6 = global ptr null
@__global_pf6_val = global float 0.000000e+00
@__global_pf7 = global ptr null
@__global_pf7_val = global float 0.000000e+00
@0 = private unnamed_addr constant [14 x i8] c"Bearer abc123\00", align 1
@1 = private unnamed_addr constant [12 x i8] c"substr(7): \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [7 x i8] c"abc123\00", align 1
@4 = private unnamed_addr constant [15 x i8] c"FAIL substr(7)\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [14 x i8] c"substr(0,6): \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [7 x i8] c"Bearer\00", align 1
@9 = private unnamed_addr constant [17 x i8] c"FAIL substr(0,6)\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [14 x i8] c"substr(8,3): \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"bc1\00", align 1
@14 = private unnamed_addr constant [17 x i8] c"FAIL substr(8,3)\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [13 x i8] c"substr(13): \00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@19 = private unnamed_addr constant [16 x i8] c"FAIL substr(13)\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [15 x i8] c"substr(0,13): \00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [14 x i8] c"Bearer abc123\00", align 1
@24 = private unnamed_addr constant [18 x i8] c"FAIL substr(0,13)\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [14 x i8] c"substr(4,0): \00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@28 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@29 = private unnamed_addr constant [17 x i8] c"FAIL substr(4,0)\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@30 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@31 = private unnamed_addr constant [15 x i8] c"substr(12,1): \00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [2 x i8] c"3\00", align 1
@34 = private unnamed_addr constant [18 x i8] c"FAIL substr(12,1)\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@37 = private unnamed_addr constant [18 x i8] c"empty substr(0): \00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@40 = private unnamed_addr constant [21 x i8] c"FAIL empty substr(0)\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [8 x i8] c"Bearer \00", align 1
@43 = private unnamed_addr constant [21 x i8] c"starts_with Bearer: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [22 x i8] c"FAIL starts_with true\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@46 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@47 = private unnamed_addr constant [7 x i8] c"Token \00", align 1
@48 = private unnamed_addr constant [20 x i8] c"starts_with Token: \00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@49 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@50 = private unnamed_addr constant [23 x i8] c"FAIL starts_with false\00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@51 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@52 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@53 = private unnamed_addr constant [20 x i8] c"starts_with empty: \00", align 1
@print_null_str.20 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@54 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@55 = private unnamed_addr constant [23 x i8] c"FAIL starts_with empty\00", align 1
@print_null_str.21 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@57 = private unnamed_addr constant [14 x i8] c"Bearer abc123\00", align 1
@58 = private unnamed_addr constant [26 x i8] c"starts_with full string: \00", align 1
@print_null_str.22 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@59 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@60 = private unnamed_addr constant [22 x i8] c"FAIL starts_with full\00", align 1
@print_null_str.23 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@61 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@62 = private unnamed_addr constant [15 x i8] c"Bearer abc1234\00", align 1
@63 = private unnamed_addr constant [28 x i8] c"starts_with longer string: \00", align 1
@print_null_str.24 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@64 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@65 = private unnamed_addr constant [24 x i8] c"FAIL starts_with longer\00", align 1
@print_null_str.25 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@66 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@67 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@68 = private unnamed_addr constant [26 x i8] c"empty starts_with empty: \00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@69 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@70 = private unnamed_addr constant [29 x i8] c"FAIL empty starts_with empty\00", align 1
@print_null_str.27 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@71 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@72 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@73 = private unnamed_addr constant [22 x i8] c"empty starts_with x: \00", align 1
@print_null_str.28 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@74 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@75 = private unnamed_addr constant [25 x i8] c"FAIL empty starts_with x\00", align 1
@print_null_str.29 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@76 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@77 = private unnamed_addr constant [4 x i8] c"abc\00", align 1
@78 = private unnamed_addr constant [15 x i8] c"index_of abc: \00", align 1
@print_null_str.30 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@79 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@80 = private unnamed_addr constant [20 x i8] c"FAIL index_of found\00", align 1
@print_null_str.31 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@81 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@82 = private unnamed_addr constant [4 x i8] c"xyz\00", align 1
@83 = private unnamed_addr constant [15 x i8] c"index_of xyz: \00", align 1
@print_null_str.32 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@84 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@85 = private unnamed_addr constant [24 x i8] c"FAIL index_of not found\00", align 1
@print_null_str.33 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@86 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@87 = private unnamed_addr constant [7 x i8] c"Bearer\00", align 1
@88 = private unnamed_addr constant [18 x i8] c"index_of Bearer: \00", align 1
@print_null_str.34 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@89 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@90 = private unnamed_addr constant [21 x i8] c"FAIL index_of prefix\00", align 1
@print_null_str.35 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@91 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@92 = private unnamed_addr constant [4 x i8] c"123\00", align 1
@93 = private unnamed_addr constant [15 x i8] c"index_of 123: \00", align 1
@print_null_str.36 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@94 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@95 = private unnamed_addr constant [21 x i8] c"FAIL index_of suffix\00", align 1
@print_null_str.37 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@96 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@97 = private unnamed_addr constant [2 x i8] c" \00", align 1
@98 = private unnamed_addr constant [17 x i8] c"index_of space: \00", align 1
@print_null_str.38 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@99 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@100 = private unnamed_addr constant [20 x i8] c"FAIL index_of space\00", align 1
@print_null_str.39 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@101 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@102 = private unnamed_addr constant [2 x i8] c"a\00", align 1
@103 = private unnamed_addr constant [13 x i8] c"index_of a: \00", align 1
@print_null_str.40 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@104 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@105 = private unnamed_addr constant [26 x i8] c"FAIL index_of single char\00", align 1
@print_null_str.41 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@106 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@107 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@108 = private unnamed_addr constant [17 x i8] c"index_of empty: \00", align 1
@print_null_str.42 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@109 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@110 = private unnamed_addr constant [20 x i8] c"FAIL index_of empty\00", align 1
@print_null_str.43 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@111 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@112 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@113 = private unnamed_addr constant [23 x i8] c"empty index_of empty: \00", align 1
@print_null_str.44 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@114 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@115 = private unnamed_addr constant [26 x i8] c"FAIL empty index_of empty\00", align 1
@print_null_str.45 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@116 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@117 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@118 = private unnamed_addr constant [19 x i8] c"empty index_of x: \00", align 1
@print_null_str.46 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@119 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@120 = private unnamed_addr constant [22 x i8] c"FAIL empty index_of x\00", align 1
@print_null_str.47 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@121 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@122 = private unnamed_addr constant [3 x i8] c"42\00", align 1
@123 = private unnamed_addr constant [17 x i8] c"parse_i32 '42': \00", align 1
@print_null_str.48 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@124 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@125 = private unnamed_addr constant [18 x i8] c"FAIL parse_i32 42\00", align 1
@print_null_str.49 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@126 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@127 = private unnamed_addr constant [3 x i8] c"-7\00", align 1
@128 = private unnamed_addr constant [17 x i8] c"parse_i32 '-7': \00", align 1
@print_null_str.50 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@129 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@130 = private unnamed_addr constant [18 x i8] c"FAIL parse_i32 -7\00", align 1
@print_null_str.51 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@131 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@132 = private unnamed_addr constant [2 x i8] c"0\00", align 1
@133 = private unnamed_addr constant [16 x i8] c"parse_i32 '0': \00", align 1
@print_null_str.52 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@134 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@135 = private unnamed_addr constant [17 x i8] c"FAIL parse_i32 0\00", align 1
@print_null_str.53 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@136 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@137 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@138 = private unnamed_addr constant [20 x i8] c"parse_i32 'hello': \00", align 1
@print_null_str.54 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@139 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@140 = private unnamed_addr constant [27 x i8] c"FAIL parse_i32 non-numeric\00", align 1
@print_null_str.55 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@141 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@142 = private unnamed_addr constant [8 x i8] c"  123  \00", align 1
@143 = private unnamed_addr constant [22 x i8] c"parse_i32 '  123  ': \00", align 1
@print_null_str.56 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@144 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@145 = private unnamed_addr constant [26 x i8] c"FAIL parse_i32 whitespace\00", align 1
@print_null_str.57 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@146 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@147 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@148 = private unnamed_addr constant [15 x i8] c"parse_i32 '': \00", align 1
@print_null_str.58 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@149 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@150 = private unnamed_addr constant [21 x i8] c"FAIL parse_i32 empty\00", align 1
@print_null_str.59 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@151 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@152 = private unnamed_addr constant [5 x i8] c"3.14\00", align 1
@153 = private unnamed_addr constant [19 x i8] c"parse_f32 '3.14': \00", align 1
@fmt_f32 = private unnamed_addr constant [5 x i8] c"%.6g\00", align 1
@print_null_str.60 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@154 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@155 = private unnamed_addr constant [20 x i8] c"FAIL parse_f32 3.14\00", align 1
@print_null_str.61 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@156 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@157 = private unnamed_addr constant [5 x i8] c"-0.5\00", align 1
@158 = private unnamed_addr constant [19 x i8] c"parse_f32 '-0.5': \00", align 1
@print_null_str.62 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@159 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@160 = private unnamed_addr constant [20 x i8] c"FAIL parse_f32 -0.5\00", align 1
@print_null_str.63 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@161 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@162 = private unnamed_addr constant [4 x i8] c"0.0\00", align 1
@163 = private unnamed_addr constant [18 x i8] c"parse_f32 '0.0': \00", align 1
@print_null_str.64 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@164 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@165 = private unnamed_addr constant [19 x i8] c"FAIL parse_f32 0.0\00", align 1
@print_null_str.65 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@166 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@167 = private unnamed_addr constant [4 x i8] c"abc\00", align 1
@168 = private unnamed_addr constant [18 x i8] c"parse_f32 'abc': \00", align 1
@print_null_str.66 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@169 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@170 = private unnamed_addr constant [27 x i8] c"FAIL parse_f32 non-numeric\00", align 1
@print_null_str.67 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@171 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@172 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@173 = private unnamed_addr constant [15 x i8] c"parse_f32 '': \00", align 1
@print_null_str.68 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@174 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@175 = private unnamed_addr constant [21 x i8] c"FAIL parse_f32 empty\00", align 1
@print_null_str.69 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@176 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@177 = private unnamed_addr constant [6 x i8] c"1.5e2\00", align 1
@178 = private unnamed_addr constant [20 x i8] c"parse_f32 '1.5e2': \00", align 1
@print_null_str.70 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@179 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@180 = private unnamed_addr constant [26 x i8] c"FAIL parse_f32 scientific\00", align 1
@print_null_str.71 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@181 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@182 = private unnamed_addr constant [4 x i8] c"100\00", align 1
@183 = private unnamed_addr constant [18 x i8] c"parse_f32 '100': \00", align 1
@print_null_str.72 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@184 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@185 = private unnamed_addr constant [30 x i8] c"FAIL parse_f32 integer string\00", align 1
@print_null_str.73 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@186 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@187 = private unnamed_addr constant [17 x i8] c"string methods: \00", align 1
@188 = private unnamed_addr constant [10 x i8] c" passed, \00", align 1
@189 = private unnamed_addr constant [8 x i8] c" failed\00", align 1
@print_null_str.74 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@190 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  store i32 0, ptr @__global_pass, align 4
  store i32 0, ptr @__global_fail, align 4
  store ptr @0, ptr @__global_s, align 8
  %0 = load ptr, ptr @__global_s, align 8
  %1 = getelementptr i8, ptr %0, i64 7
  %2 = call ptr @strdup(ptr %1)
  store ptr %2, ptr @__global_tok, align 8
  %tok_val = load ptr, ptr @__global_tok, align 8
  %3 = call ptr @__aiml_str_concat(ptr @1, ptr %tok_val)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %tok_val1 = load ptr, ptr @__global_tok, align 8
  %4 = icmp eq ptr %tok_val1, null
  %5 = icmp eq ptr %tok_val1, @3
  %6 = or i1 %4, false
  br i1 %6, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %entry
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %entry
  %7 = call i32 @strcmp(ptr %tok_val1, ptr @3)
  %8 = icmp eq i32 %7, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %5, %cmp_check_null ], [ %8, %cmp_do_strcmp ]
  br i1 %strcmp_res, label %if_then, label %if_else

if_then:                                          ; preds = %cmp_merge
  %pass_val = load i32, ptr @__global_pass, align 4
  %9 = add i32 %pass_val, 1
  store i32 %9, ptr @__global_pass, align 4
  br label %if_merge

if_else:                                          ; preds = %cmp_merge
  %fail_val = load i32, ptr @__global_fail, align 4
  %10 = add i32 %fail_val, 1
  store i32 %10, ptr @__global_fail, align 4
  %print_call2 = call i32 (ptr, ...) @printf(ptr @5, ptr @4)
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  %11 = load ptr, ptr @__global_s, align 8
  %12 = getelementptr i8, ptr %11, i64 0
  %13 = call ptr @strndup(ptr %12, i64 6)
  store ptr %13, ptr @__global_bearer, align 8
  %bearer_val = load ptr, ptr @__global_bearer, align 8
  %14 = call ptr @__aiml_str_concat(ptr @6, ptr %bearer_val)
  %print_null_check3 = icmp eq ptr %14, null
  %print_safe_str4 = select i1 %print_null_check3, ptr @print_null_str.2, ptr %14
  %print_call5 = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str4)
  %bearer_val6 = load ptr, ptr @__global_bearer, align 8
  %15 = icmp eq ptr %bearer_val6, null
  %16 = icmp eq ptr %bearer_val6, @8
  %17 = or i1 %15, false
  br i1 %17, label %cmp_check_null7, label %cmp_do_strcmp8

cmp_check_null7:                                  ; preds = %if_merge
  br label %cmp_merge9

cmp_do_strcmp8:                                   ; preds = %if_merge
  %18 = call i32 @strcmp(ptr %bearer_val6, ptr @8)
  %19 = icmp eq i32 %18, 0
  br label %cmp_merge9

cmp_merge9:                                       ; preds = %cmp_do_strcmp8, %cmp_check_null7
  %strcmp_res10 = phi i1 [ %16, %cmp_check_null7 ], [ %19, %cmp_do_strcmp8 ]
  br i1 %strcmp_res10, label %if_then11, label %if_else12

if_then11:                                        ; preds = %cmp_merge9
  %pass_val14 = load i32, ptr @__global_pass, align 4
  %20 = add i32 %pass_val14, 1
  store i32 %20, ptr @__global_pass, align 4
  br label %if_merge13

if_else12:                                        ; preds = %cmp_merge9
  %fail_val15 = load i32, ptr @__global_fail, align 4
  %21 = add i32 %fail_val15, 1
  store i32 %21, ptr @__global_fail, align 4
  %print_call16 = call i32 (ptr, ...) @printf(ptr @10, ptr @9)
  br label %if_merge13

if_merge13:                                       ; preds = %if_else12, %if_then11
  %22 = load ptr, ptr @__global_s, align 8
  %23 = getelementptr i8, ptr %22, i64 8
  %24 = call ptr @strndup(ptr %23, i64 3)
  store ptr %24, ptr @__global_mid, align 8
  %mid_val = load ptr, ptr @__global_mid, align 8
  %25 = call ptr @__aiml_str_concat(ptr @11, ptr %mid_val)
  %print_null_check17 = icmp eq ptr %25, null
  %print_safe_str18 = select i1 %print_null_check17, ptr @print_null_str.4, ptr %25
  %print_call19 = call i32 (ptr, ...) @printf(ptr @12, ptr %print_safe_str18)
  %mid_val20 = load ptr, ptr @__global_mid, align 8
  %26 = icmp eq ptr %mid_val20, null
  %27 = icmp eq ptr %mid_val20, @13
  %28 = or i1 %26, false
  br i1 %28, label %cmp_check_null21, label %cmp_do_strcmp22

cmp_check_null21:                                 ; preds = %if_merge13
  br label %cmp_merge23

cmp_do_strcmp22:                                  ; preds = %if_merge13
  %29 = call i32 @strcmp(ptr %mid_val20, ptr @13)
  %30 = icmp eq i32 %29, 0
  br label %cmp_merge23

cmp_merge23:                                      ; preds = %cmp_do_strcmp22, %cmp_check_null21
  %strcmp_res24 = phi i1 [ %27, %cmp_check_null21 ], [ %30, %cmp_do_strcmp22 ]
  br i1 %strcmp_res24, label %if_then25, label %if_else26

if_then25:                                        ; preds = %cmp_merge23
  %pass_val28 = load i32, ptr @__global_pass, align 4
  %31 = add i32 %pass_val28, 1
  store i32 %31, ptr @__global_pass, align 4
  br label %if_merge27

if_else26:                                        ; preds = %cmp_merge23
  %fail_val29 = load i32, ptr @__global_fail, align 4
  %32 = add i32 %fail_val29, 1
  store i32 %32, ptr @__global_fail, align 4
  %print_call30 = call i32 (ptr, ...) @printf(ptr @15, ptr @14)
  br label %if_merge27

if_merge27:                                       ; preds = %if_else26, %if_then25
  %33 = load ptr, ptr @__global_s, align 8
  %34 = getelementptr i8, ptr %33, i64 13
  %35 = call ptr @strdup(ptr %34)
  store ptr %35, ptr @__global_tail_empty, align 8
  %tail_empty_val = load ptr, ptr @__global_tail_empty, align 8
  %36 = call ptr @__aiml_str_concat(ptr @16, ptr %tail_empty_val)
  %print_null_check31 = icmp eq ptr %36, null
  %print_safe_str32 = select i1 %print_null_check31, ptr @print_null_str.6, ptr %36
  %print_call33 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str32)
  %tail_empty_val34 = load ptr, ptr @__global_tail_empty, align 8
  %37 = icmp eq ptr %tail_empty_val34, null
  %38 = icmp eq ptr %tail_empty_val34, @18
  %39 = or i1 %37, false
  br i1 %39, label %cmp_check_null35, label %cmp_do_strcmp36

cmp_check_null35:                                 ; preds = %if_merge27
  br label %cmp_merge37

cmp_do_strcmp36:                                  ; preds = %if_merge27
  %40 = call i32 @strcmp(ptr %tail_empty_val34, ptr @18)
  %41 = icmp eq i32 %40, 0
  br label %cmp_merge37

cmp_merge37:                                      ; preds = %cmp_do_strcmp36, %cmp_check_null35
  %strcmp_res38 = phi i1 [ %38, %cmp_check_null35 ], [ %41, %cmp_do_strcmp36 ]
  br i1 %strcmp_res38, label %if_then39, label %if_else40

if_then39:                                        ; preds = %cmp_merge37
  %pass_val42 = load i32, ptr @__global_pass, align 4
  %42 = add i32 %pass_val42, 1
  store i32 %42, ptr @__global_pass, align 4
  br label %if_merge41

if_else40:                                        ; preds = %cmp_merge37
  %fail_val43 = load i32, ptr @__global_fail, align 4
  %43 = add i32 %fail_val43, 1
  store i32 %43, ptr @__global_fail, align 4
  %print_call44 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
  br label %if_merge41

if_merge41:                                       ; preds = %if_else40, %if_then39
  %44 = load ptr, ptr @__global_s, align 8
  %45 = getelementptr i8, ptr %44, i64 0
  %46 = call ptr @strndup(ptr %45, i64 13)
  store ptr %46, ptr @__global_full_copy, align 8
  %full_copy_val = load ptr, ptr @__global_full_copy, align 8
  %47 = call ptr @__aiml_str_concat(ptr @21, ptr %full_copy_val)
  %print_null_check45 = icmp eq ptr %47, null
  %print_safe_str46 = select i1 %print_null_check45, ptr @print_null_str.8, ptr %47
  %print_call47 = call i32 (ptr, ...) @printf(ptr @22, ptr %print_safe_str46)
  %full_copy_val48 = load ptr, ptr @__global_full_copy, align 8
  %48 = icmp eq ptr %full_copy_val48, null
  %49 = icmp eq ptr %full_copy_val48, @23
  %50 = or i1 %48, false
  br i1 %50, label %cmp_check_null49, label %cmp_do_strcmp50

cmp_check_null49:                                 ; preds = %if_merge41
  br label %cmp_merge51

cmp_do_strcmp50:                                  ; preds = %if_merge41
  %51 = call i32 @strcmp(ptr %full_copy_val48, ptr @23)
  %52 = icmp eq i32 %51, 0
  br label %cmp_merge51

cmp_merge51:                                      ; preds = %cmp_do_strcmp50, %cmp_check_null49
  %strcmp_res52 = phi i1 [ %49, %cmp_check_null49 ], [ %52, %cmp_do_strcmp50 ]
  br i1 %strcmp_res52, label %if_then53, label %if_else54

if_then53:                                        ; preds = %cmp_merge51
  %pass_val56 = load i32, ptr @__global_pass, align 4
  %53 = add i32 %pass_val56, 1
  store i32 %53, ptr @__global_pass, align 4
  br label %if_merge55

if_else54:                                        ; preds = %cmp_merge51
  %fail_val57 = load i32, ptr @__global_fail, align 4
  %54 = add i32 %fail_val57, 1
  store i32 %54, ptr @__global_fail, align 4
  %print_call58 = call i32 (ptr, ...) @printf(ptr @25, ptr @24)
  br label %if_merge55

if_merge55:                                       ; preds = %if_else54, %if_then53
  %55 = load ptr, ptr @__global_s, align 8
  %56 = getelementptr i8, ptr %55, i64 4
  %57 = call ptr @strndup(ptr %56, i64 0)
  store ptr %57, ptr @__global_zero_len, align 8
  %zero_len_val = load ptr, ptr @__global_zero_len, align 8
  %58 = call ptr @__aiml_str_concat(ptr @26, ptr %zero_len_val)
  %print_null_check59 = icmp eq ptr %58, null
  %print_safe_str60 = select i1 %print_null_check59, ptr @print_null_str.10, ptr %58
  %print_call61 = call i32 (ptr, ...) @printf(ptr @27, ptr %print_safe_str60)
  %zero_len_val62 = load ptr, ptr @__global_zero_len, align 8
  %59 = icmp eq ptr %zero_len_val62, null
  %60 = icmp eq ptr %zero_len_val62, @28
  %61 = or i1 %59, false
  br i1 %61, label %cmp_check_null63, label %cmp_do_strcmp64

cmp_check_null63:                                 ; preds = %if_merge55
  br label %cmp_merge65

cmp_do_strcmp64:                                  ; preds = %if_merge55
  %62 = call i32 @strcmp(ptr %zero_len_val62, ptr @28)
  %63 = icmp eq i32 %62, 0
  br label %cmp_merge65

cmp_merge65:                                      ; preds = %cmp_do_strcmp64, %cmp_check_null63
  %strcmp_res66 = phi i1 [ %60, %cmp_check_null63 ], [ %63, %cmp_do_strcmp64 ]
  br i1 %strcmp_res66, label %if_then67, label %if_else68

if_then67:                                        ; preds = %cmp_merge65
  %pass_val70 = load i32, ptr @__global_pass, align 4
  %64 = add i32 %pass_val70, 1
  store i32 %64, ptr @__global_pass, align 4
  br label %if_merge69

if_else68:                                        ; preds = %cmp_merge65
  %fail_val71 = load i32, ptr @__global_fail, align 4
  %65 = add i32 %fail_val71, 1
  store i32 %65, ptr @__global_fail, align 4
  %print_call72 = call i32 (ptr, ...) @printf(ptr @30, ptr @29)
  br label %if_merge69

if_merge69:                                       ; preds = %if_else68, %if_then67
  %66 = load ptr, ptr @__global_s, align 8
  %67 = getelementptr i8, ptr %66, i64 12
  %68 = call ptr @strndup(ptr %67, i64 1)
  store ptr %68, ptr @__global_single_char, align 8
  %single_char_val = load ptr, ptr @__global_single_char, align 8
  %69 = call ptr @__aiml_str_concat(ptr @31, ptr %single_char_val)
  %print_null_check73 = icmp eq ptr %69, null
  %print_safe_str74 = select i1 %print_null_check73, ptr @print_null_str.12, ptr %69
  %print_call75 = call i32 (ptr, ...) @printf(ptr @32, ptr %print_safe_str74)
  %single_char_val76 = load ptr, ptr @__global_single_char, align 8
  %70 = icmp eq ptr %single_char_val76, null
  %71 = icmp eq ptr %single_char_val76, @33
  %72 = or i1 %70, false
  br i1 %72, label %cmp_check_null77, label %cmp_do_strcmp78

cmp_check_null77:                                 ; preds = %if_merge69
  br label %cmp_merge79

cmp_do_strcmp78:                                  ; preds = %if_merge69
  %73 = call i32 @strcmp(ptr %single_char_val76, ptr @33)
  %74 = icmp eq i32 %73, 0
  br label %cmp_merge79

cmp_merge79:                                      ; preds = %cmp_do_strcmp78, %cmp_check_null77
  %strcmp_res80 = phi i1 [ %71, %cmp_check_null77 ], [ %74, %cmp_do_strcmp78 ]
  br i1 %strcmp_res80, label %if_then81, label %if_else82

if_then81:                                        ; preds = %cmp_merge79
  %pass_val84 = load i32, ptr @__global_pass, align 4
  %75 = add i32 %pass_val84, 1
  store i32 %75, ptr @__global_pass, align 4
  br label %if_merge83

if_else82:                                        ; preds = %cmp_merge79
  %fail_val85 = load i32, ptr @__global_fail, align 4
  %76 = add i32 %fail_val85, 1
  store i32 %76, ptr @__global_fail, align 4
  %print_call86 = call i32 (ptr, ...) @printf(ptr @35, ptr @34)
  br label %if_merge83

if_merge83:                                       ; preds = %if_else82, %if_then81
  store ptr @36, ptr @__global_empty_s, align 8
  %77 = load ptr, ptr @__global_empty_s, align 8
  %78 = getelementptr i8, ptr %77, i64 0
  %79 = call ptr @strdup(ptr %78)
  store ptr %79, ptr @__global_empty_sub, align 8
  %empty_sub_val = load ptr, ptr @__global_empty_sub, align 8
  %80 = call ptr @__aiml_str_concat(ptr @37, ptr %empty_sub_val)
  %print_null_check87 = icmp eq ptr %80, null
  %print_safe_str88 = select i1 %print_null_check87, ptr @print_null_str.14, ptr %80
  %print_call89 = call i32 (ptr, ...) @printf(ptr @38, ptr %print_safe_str88)
  %empty_sub_val90 = load ptr, ptr @__global_empty_sub, align 8
  %81 = icmp eq ptr %empty_sub_val90, null
  %82 = icmp eq ptr %empty_sub_val90, @39
  %83 = or i1 %81, false
  br i1 %83, label %cmp_check_null91, label %cmp_do_strcmp92

cmp_check_null91:                                 ; preds = %if_merge83
  br label %cmp_merge93

cmp_do_strcmp92:                                  ; preds = %if_merge83
  %84 = call i32 @strcmp(ptr %empty_sub_val90, ptr @39)
  %85 = icmp eq i32 %84, 0
  br label %cmp_merge93

cmp_merge93:                                      ; preds = %cmp_do_strcmp92, %cmp_check_null91
  %strcmp_res94 = phi i1 [ %82, %cmp_check_null91 ], [ %85, %cmp_do_strcmp92 ]
  br i1 %strcmp_res94, label %if_then95, label %if_else96

if_then95:                                        ; preds = %cmp_merge93
  %pass_val98 = load i32, ptr @__global_pass, align 4
  %86 = add i32 %pass_val98, 1
  store i32 %86, ptr @__global_pass, align 4
  br label %if_merge97

if_else96:                                        ; preds = %cmp_merge93
  %fail_val99 = load i32, ptr @__global_fail, align 4
  %87 = add i32 %fail_val99, 1
  store i32 %87, ptr @__global_fail, align 4
  %print_call100 = call i32 (ptr, ...) @printf(ptr @41, ptr @40)
  br label %if_merge97

if_merge97:                                       ; preds = %if_else96, %if_then95
  %88 = load ptr, ptr @__global_s, align 8
  %89 = call i64 @strlen(ptr @42)
  %90 = call i32 @strncmp(ptr %88, ptr @42, i64 %89)
  %91 = icmp eq i32 %90, 0
  %92 = zext i1 %91 to i32
  store i32 %92, ptr @__global_sw1, align 4
  %sw1_val = load i32, ptr @__global_sw1, align 4
  %93 = call ptr @__aiml_int_to_string(i32 %sw1_val)
  %94 = call ptr @__aiml_str_concat(ptr @43, ptr %93)
  %print_null_check101 = icmp eq ptr %94, null
  %print_safe_str102 = select i1 %print_null_check101, ptr @print_null_str.16, ptr %94
  %print_call103 = call i32 (ptr, ...) @printf(ptr @44, ptr %print_safe_str102)
  %sw1_val104 = load i32, ptr @__global_sw1, align 4
  %95 = icmp eq i32 %sw1_val104, 1
  br i1 %95, label %if_then105, label %if_else106

if_then105:                                       ; preds = %if_merge97
  %pass_val108 = load i32, ptr @__global_pass, align 4
  %96 = add i32 %pass_val108, 1
  store i32 %96, ptr @__global_pass, align 4
  br label %if_merge107

if_else106:                                       ; preds = %if_merge97
  %fail_val109 = load i32, ptr @__global_fail, align 4
  %97 = add i32 %fail_val109, 1
  store i32 %97, ptr @__global_fail, align 4
  %print_call110 = call i32 (ptr, ...) @printf(ptr @46, ptr @45)
  br label %if_merge107

if_merge107:                                      ; preds = %if_else106, %if_then105
  %98 = load ptr, ptr @__global_s, align 8
  %99 = call i64 @strlen(ptr @47)
  %100 = call i32 @strncmp(ptr %98, ptr @47, i64 %99)
  %101 = icmp eq i32 %100, 0
  %102 = zext i1 %101 to i32
  store i32 %102, ptr @__global_sw2, align 4
  %sw2_val = load i32, ptr @__global_sw2, align 4
  %103 = call ptr @__aiml_int_to_string(i32 %sw2_val)
  %104 = call ptr @__aiml_str_concat(ptr @48, ptr %103)
  %print_null_check111 = icmp eq ptr %104, null
  %print_safe_str112 = select i1 %print_null_check111, ptr @print_null_str.18, ptr %104
  %print_call113 = call i32 (ptr, ...) @printf(ptr @49, ptr %print_safe_str112)
  %sw2_val114 = load i32, ptr @__global_sw2, align 4
  %105 = icmp eq i32 %sw2_val114, 0
  br i1 %105, label %if_then115, label %if_else116

if_then115:                                       ; preds = %if_merge107
  %pass_val118 = load i32, ptr @__global_pass, align 4
  %106 = add i32 %pass_val118, 1
  store i32 %106, ptr @__global_pass, align 4
  br label %if_merge117

if_else116:                                       ; preds = %if_merge107
  %fail_val119 = load i32, ptr @__global_fail, align 4
  %107 = add i32 %fail_val119, 1
  store i32 %107, ptr @__global_fail, align 4
  %print_call120 = call i32 (ptr, ...) @printf(ptr @51, ptr @50)
  br label %if_merge117

if_merge117:                                      ; preds = %if_else116, %if_then115
  %108 = load ptr, ptr @__global_s, align 8
  %109 = call i64 @strlen(ptr @52)
  %110 = call i32 @strncmp(ptr %108, ptr @52, i64 %109)
  %111 = icmp eq i32 %110, 0
  %112 = zext i1 %111 to i32
  store i32 %112, ptr @__global_sw3, align 4
  %sw3_val = load i32, ptr @__global_sw3, align 4
  %113 = call ptr @__aiml_int_to_string(i32 %sw3_val)
  %114 = call ptr @__aiml_str_concat(ptr @53, ptr %113)
  %print_null_check121 = icmp eq ptr %114, null
  %print_safe_str122 = select i1 %print_null_check121, ptr @print_null_str.20, ptr %114
  %print_call123 = call i32 (ptr, ...) @printf(ptr @54, ptr %print_safe_str122)
  %sw3_val124 = load i32, ptr @__global_sw3, align 4
  %115 = icmp eq i32 %sw3_val124, 1
  br i1 %115, label %if_then125, label %if_else126

if_then125:                                       ; preds = %if_merge117
  %pass_val128 = load i32, ptr @__global_pass, align 4
  %116 = add i32 %pass_val128, 1
  store i32 %116, ptr @__global_pass, align 4
  br label %if_merge127

if_else126:                                       ; preds = %if_merge117
  %fail_val129 = load i32, ptr @__global_fail, align 4
  %117 = add i32 %fail_val129, 1
  store i32 %117, ptr @__global_fail, align 4
  %print_call130 = call i32 (ptr, ...) @printf(ptr @56, ptr @55)
  br label %if_merge127

if_merge127:                                      ; preds = %if_else126, %if_then125
  %118 = load ptr, ptr @__global_s, align 8
  %119 = call i64 @strlen(ptr @57)
  %120 = call i32 @strncmp(ptr %118, ptr @57, i64 %119)
  %121 = icmp eq i32 %120, 0
  %122 = zext i1 %121 to i32
  store i32 %122, ptr @__global_sw4, align 4
  %sw4_val = load i32, ptr @__global_sw4, align 4
  %123 = call ptr @__aiml_int_to_string(i32 %sw4_val)
  %124 = call ptr @__aiml_str_concat(ptr @58, ptr %123)
  %print_null_check131 = icmp eq ptr %124, null
  %print_safe_str132 = select i1 %print_null_check131, ptr @print_null_str.22, ptr %124
  %print_call133 = call i32 (ptr, ...) @printf(ptr @59, ptr %print_safe_str132)
  %sw4_val134 = load i32, ptr @__global_sw4, align 4
  %125 = icmp eq i32 %sw4_val134, 1
  br i1 %125, label %if_then135, label %if_else136

if_then135:                                       ; preds = %if_merge127
  %pass_val138 = load i32, ptr @__global_pass, align 4
  %126 = add i32 %pass_val138, 1
  store i32 %126, ptr @__global_pass, align 4
  br label %if_merge137

if_else136:                                       ; preds = %if_merge127
  %fail_val139 = load i32, ptr @__global_fail, align 4
  %127 = add i32 %fail_val139, 1
  store i32 %127, ptr @__global_fail, align 4
  %print_call140 = call i32 (ptr, ...) @printf(ptr @61, ptr @60)
  br label %if_merge137

if_merge137:                                      ; preds = %if_else136, %if_then135
  %128 = load ptr, ptr @__global_s, align 8
  %129 = call i64 @strlen(ptr @62)
  %130 = call i32 @strncmp(ptr %128, ptr @62, i64 %129)
  %131 = icmp eq i32 %130, 0
  %132 = zext i1 %131 to i32
  store i32 %132, ptr @__global_sw5, align 4
  %sw5_val = load i32, ptr @__global_sw5, align 4
  %133 = call ptr @__aiml_int_to_string(i32 %sw5_val)
  %134 = call ptr @__aiml_str_concat(ptr @63, ptr %133)
  %print_null_check141 = icmp eq ptr %134, null
  %print_safe_str142 = select i1 %print_null_check141, ptr @print_null_str.24, ptr %134
  %print_call143 = call i32 (ptr, ...) @printf(ptr @64, ptr %print_safe_str142)
  %sw5_val144 = load i32, ptr @__global_sw5, align 4
  %135 = icmp eq i32 %sw5_val144, 0
  br i1 %135, label %if_then145, label %if_else146

if_then145:                                       ; preds = %if_merge137
  %pass_val148 = load i32, ptr @__global_pass, align 4
  %136 = add i32 %pass_val148, 1
  store i32 %136, ptr @__global_pass, align 4
  br label %if_merge147

if_else146:                                       ; preds = %if_merge137
  %fail_val149 = load i32, ptr @__global_fail, align 4
  %137 = add i32 %fail_val149, 1
  store i32 %137, ptr @__global_fail, align 4
  %print_call150 = call i32 (ptr, ...) @printf(ptr @66, ptr @65)
  br label %if_merge147

if_merge147:                                      ; preds = %if_else146, %if_then145
  %138 = load ptr, ptr @__global_empty_s, align 8
  %139 = call i64 @strlen(ptr @67)
  %140 = call i32 @strncmp(ptr %138, ptr @67, i64 %139)
  %141 = icmp eq i32 %140, 0
  %142 = zext i1 %141 to i32
  store i32 %142, ptr @__global_sw6, align 4
  %sw6_val = load i32, ptr @__global_sw6, align 4
  %143 = call ptr @__aiml_int_to_string(i32 %sw6_val)
  %144 = call ptr @__aiml_str_concat(ptr @68, ptr %143)
  %print_null_check151 = icmp eq ptr %144, null
  %print_safe_str152 = select i1 %print_null_check151, ptr @print_null_str.26, ptr %144
  %print_call153 = call i32 (ptr, ...) @printf(ptr @69, ptr %print_safe_str152)
  %sw6_val154 = load i32, ptr @__global_sw6, align 4
  %145 = icmp eq i32 %sw6_val154, 1
  br i1 %145, label %if_then155, label %if_else156

if_then155:                                       ; preds = %if_merge147
  %pass_val158 = load i32, ptr @__global_pass, align 4
  %146 = add i32 %pass_val158, 1
  store i32 %146, ptr @__global_pass, align 4
  br label %if_merge157

if_else156:                                       ; preds = %if_merge147
  %fail_val159 = load i32, ptr @__global_fail, align 4
  %147 = add i32 %fail_val159, 1
  store i32 %147, ptr @__global_fail, align 4
  %print_call160 = call i32 (ptr, ...) @printf(ptr @71, ptr @70)
  br label %if_merge157

if_merge157:                                      ; preds = %if_else156, %if_then155
  %148 = load ptr, ptr @__global_empty_s, align 8
  %149 = call i64 @strlen(ptr @72)
  %150 = call i32 @strncmp(ptr %148, ptr @72, i64 %149)
  %151 = icmp eq i32 %150, 0
  %152 = zext i1 %151 to i32
  store i32 %152, ptr @__global_sw7, align 4
  %sw7_val = load i32, ptr @__global_sw7, align 4
  %153 = call ptr @__aiml_int_to_string(i32 %sw7_val)
  %154 = call ptr @__aiml_str_concat(ptr @73, ptr %153)
  %print_null_check161 = icmp eq ptr %154, null
  %print_safe_str162 = select i1 %print_null_check161, ptr @print_null_str.28, ptr %154
  %print_call163 = call i32 (ptr, ...) @printf(ptr @74, ptr %print_safe_str162)
  %sw7_val164 = load i32, ptr @__global_sw7, align 4
  %155 = icmp eq i32 %sw7_val164, 0
  br i1 %155, label %if_then165, label %if_else166

if_then165:                                       ; preds = %if_merge157
  %pass_val168 = load i32, ptr @__global_pass, align 4
  %156 = add i32 %pass_val168, 1
  store i32 %156, ptr @__global_pass, align 4
  br label %if_merge167

if_else166:                                       ; preds = %if_merge157
  %fail_val169 = load i32, ptr @__global_fail, align 4
  %157 = add i32 %fail_val169, 1
  store i32 %157, ptr @__global_fail, align 4
  %print_call170 = call i32 (ptr, ...) @printf(ptr @76, ptr @75)
  br label %if_merge167

if_merge167:                                      ; preds = %if_else166, %if_then165
  %158 = load ptr, ptr @__global_s, align 8
  %159 = call ptr @strstr(ptr %158, ptr @77)
  %160 = icmp eq ptr %159, null
  %161 = ptrtoint ptr %159 to i64
  %162 = ptrtoint ptr %158 to i64
  %163 = sub i64 %161, %162
  %164 = sdiv exact i64 %163, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %165 = trunc i64 %164 to i32
  %166 = select i1 %160, i32 -1, i32 %165
  store i32 %166, ptr @__global_idx1, align 4
  %idx1_val = load i32, ptr @__global_idx1, align 4
  %167 = call ptr @__aiml_int_to_string(i32 %idx1_val)
  %168 = call ptr @__aiml_str_concat(ptr @78, ptr %167)
  %print_null_check171 = icmp eq ptr %168, null
  %print_safe_str172 = select i1 %print_null_check171, ptr @print_null_str.30, ptr %168
  %print_call173 = call i32 (ptr, ...) @printf(ptr @79, ptr %print_safe_str172)
  %idx1_val174 = load i32, ptr @__global_idx1, align 4
  %169 = icmp eq i32 %idx1_val174, 7
  br i1 %169, label %if_then175, label %if_else176

if_then175:                                       ; preds = %if_merge167
  %pass_val178 = load i32, ptr @__global_pass, align 4
  %170 = add i32 %pass_val178, 1
  store i32 %170, ptr @__global_pass, align 4
  br label %if_merge177

if_else176:                                       ; preds = %if_merge167
  %fail_val179 = load i32, ptr @__global_fail, align 4
  %171 = add i32 %fail_val179, 1
  store i32 %171, ptr @__global_fail, align 4
  %print_call180 = call i32 (ptr, ...) @printf(ptr @81, ptr @80)
  br label %if_merge177

if_merge177:                                      ; preds = %if_else176, %if_then175
  %172 = load ptr, ptr @__global_s, align 8
  %173 = call ptr @strstr(ptr %172, ptr @82)
  %174 = icmp eq ptr %173, null
  %175 = ptrtoint ptr %173 to i64
  %176 = ptrtoint ptr %172 to i64
  %177 = sub i64 %175, %176
  %178 = sdiv exact i64 %177, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %179 = trunc i64 %178 to i32
  %180 = select i1 %174, i32 -1, i32 %179
  store i32 %180, ptr @__global_idx2, align 4
  %idx2_val = load i32, ptr @__global_idx2, align 4
  %181 = call ptr @__aiml_int_to_string(i32 %idx2_val)
  %182 = call ptr @__aiml_str_concat(ptr @83, ptr %181)
  %print_null_check181 = icmp eq ptr %182, null
  %print_safe_str182 = select i1 %print_null_check181, ptr @print_null_str.32, ptr %182
  %print_call183 = call i32 (ptr, ...) @printf(ptr @84, ptr %print_safe_str182)
  %idx2_val184 = load i32, ptr @__global_idx2, align 4
  %183 = icmp eq i32 %idx2_val184, -1
  br i1 %183, label %if_then185, label %if_else186

if_then185:                                       ; preds = %if_merge177
  %pass_val188 = load i32, ptr @__global_pass, align 4
  %184 = add i32 %pass_val188, 1
  store i32 %184, ptr @__global_pass, align 4
  br label %if_merge187

if_else186:                                       ; preds = %if_merge177
  %fail_val189 = load i32, ptr @__global_fail, align 4
  %185 = add i32 %fail_val189, 1
  store i32 %185, ptr @__global_fail, align 4
  %print_call190 = call i32 (ptr, ...) @printf(ptr @86, ptr @85)
  br label %if_merge187

if_merge187:                                      ; preds = %if_else186, %if_then185
  %186 = load ptr, ptr @__global_s, align 8
  %187 = call ptr @strstr(ptr %186, ptr @87)
  %188 = icmp eq ptr %187, null
  %189 = ptrtoint ptr %187 to i64
  %190 = ptrtoint ptr %186 to i64
  %191 = sub i64 %189, %190
  %192 = sdiv exact i64 %191, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %193 = trunc i64 %192 to i32
  %194 = select i1 %188, i32 -1, i32 %193
  store i32 %194, ptr @__global_idx3, align 4
  %idx3_val = load i32, ptr @__global_idx3, align 4
  %195 = call ptr @__aiml_int_to_string(i32 %idx3_val)
  %196 = call ptr @__aiml_str_concat(ptr @88, ptr %195)
  %print_null_check191 = icmp eq ptr %196, null
  %print_safe_str192 = select i1 %print_null_check191, ptr @print_null_str.34, ptr %196
  %print_call193 = call i32 (ptr, ...) @printf(ptr @89, ptr %print_safe_str192)
  %idx3_val194 = load i32, ptr @__global_idx3, align 4
  %197 = icmp eq i32 %idx3_val194, 0
  br i1 %197, label %if_then195, label %if_else196

if_then195:                                       ; preds = %if_merge187
  %pass_val198 = load i32, ptr @__global_pass, align 4
  %198 = add i32 %pass_val198, 1
  store i32 %198, ptr @__global_pass, align 4
  br label %if_merge197

if_else196:                                       ; preds = %if_merge187
  %fail_val199 = load i32, ptr @__global_fail, align 4
  %199 = add i32 %fail_val199, 1
  store i32 %199, ptr @__global_fail, align 4
  %print_call200 = call i32 (ptr, ...) @printf(ptr @91, ptr @90)
  br label %if_merge197

if_merge197:                                      ; preds = %if_else196, %if_then195
  %200 = load ptr, ptr @__global_s, align 8
  %201 = call ptr @strstr(ptr %200, ptr @92)
  %202 = icmp eq ptr %201, null
  %203 = ptrtoint ptr %201 to i64
  %204 = ptrtoint ptr %200 to i64
  %205 = sub i64 %203, %204
  %206 = sdiv exact i64 %205, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %207 = trunc i64 %206 to i32
  %208 = select i1 %202, i32 -1, i32 %207
  store i32 %208, ptr @__global_idx4, align 4
  %idx4_val = load i32, ptr @__global_idx4, align 4
  %209 = call ptr @__aiml_int_to_string(i32 %idx4_val)
  %210 = call ptr @__aiml_str_concat(ptr @93, ptr %209)
  %print_null_check201 = icmp eq ptr %210, null
  %print_safe_str202 = select i1 %print_null_check201, ptr @print_null_str.36, ptr %210
  %print_call203 = call i32 (ptr, ...) @printf(ptr @94, ptr %print_safe_str202)
  %idx4_val204 = load i32, ptr @__global_idx4, align 4
  %211 = icmp eq i32 %idx4_val204, 10
  br i1 %211, label %if_then205, label %if_else206

if_then205:                                       ; preds = %if_merge197
  %pass_val208 = load i32, ptr @__global_pass, align 4
  %212 = add i32 %pass_val208, 1
  store i32 %212, ptr @__global_pass, align 4
  br label %if_merge207

if_else206:                                       ; preds = %if_merge197
  %fail_val209 = load i32, ptr @__global_fail, align 4
  %213 = add i32 %fail_val209, 1
  store i32 %213, ptr @__global_fail, align 4
  %print_call210 = call i32 (ptr, ...) @printf(ptr @96, ptr @95)
  br label %if_merge207

if_merge207:                                      ; preds = %if_else206, %if_then205
  %214 = load ptr, ptr @__global_s, align 8
  %215 = call ptr @strstr(ptr %214, ptr @97)
  %216 = icmp eq ptr %215, null
  %217 = ptrtoint ptr %215 to i64
  %218 = ptrtoint ptr %214 to i64
  %219 = sub i64 %217, %218
  %220 = sdiv exact i64 %219, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %221 = trunc i64 %220 to i32
  %222 = select i1 %216, i32 -1, i32 %221
  store i32 %222, ptr @__global_idx5, align 4
  %idx5_val = load i32, ptr @__global_idx5, align 4
  %223 = call ptr @__aiml_int_to_string(i32 %idx5_val)
  %224 = call ptr @__aiml_str_concat(ptr @98, ptr %223)
  %print_null_check211 = icmp eq ptr %224, null
  %print_safe_str212 = select i1 %print_null_check211, ptr @print_null_str.38, ptr %224
  %print_call213 = call i32 (ptr, ...) @printf(ptr @99, ptr %print_safe_str212)
  %idx5_val214 = load i32, ptr @__global_idx5, align 4
  %225 = icmp eq i32 %idx5_val214, 6
  br i1 %225, label %if_then215, label %if_else216

if_then215:                                       ; preds = %if_merge207
  %pass_val218 = load i32, ptr @__global_pass, align 4
  %226 = add i32 %pass_val218, 1
  store i32 %226, ptr @__global_pass, align 4
  br label %if_merge217

if_else216:                                       ; preds = %if_merge207
  %fail_val219 = load i32, ptr @__global_fail, align 4
  %227 = add i32 %fail_val219, 1
  store i32 %227, ptr @__global_fail, align 4
  %print_call220 = call i32 (ptr, ...) @printf(ptr @101, ptr @100)
  br label %if_merge217

if_merge217:                                      ; preds = %if_else216, %if_then215
  %228 = load ptr, ptr @__global_s, align 8
  %229 = call ptr @strstr(ptr %228, ptr @102)
  %230 = icmp eq ptr %229, null
  %231 = ptrtoint ptr %229 to i64
  %232 = ptrtoint ptr %228 to i64
  %233 = sub i64 %231, %232
  %234 = sdiv exact i64 %233, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %235 = trunc i64 %234 to i32
  %236 = select i1 %230, i32 -1, i32 %235
  store i32 %236, ptr @__global_idx6, align 4
  %idx6_val = load i32, ptr @__global_idx6, align 4
  %237 = call ptr @__aiml_int_to_string(i32 %idx6_val)
  %238 = call ptr @__aiml_str_concat(ptr @103, ptr %237)
  %print_null_check221 = icmp eq ptr %238, null
  %print_safe_str222 = select i1 %print_null_check221, ptr @print_null_str.40, ptr %238
  %print_call223 = call i32 (ptr, ...) @printf(ptr @104, ptr %print_safe_str222)
  %idx6_val224 = load i32, ptr @__global_idx6, align 4
  %239 = icmp eq i32 %idx6_val224, 2
  br i1 %239, label %if_then225, label %if_else226

if_then225:                                       ; preds = %if_merge217
  %pass_val228 = load i32, ptr @__global_pass, align 4
  %240 = add i32 %pass_val228, 1
  store i32 %240, ptr @__global_pass, align 4
  br label %if_merge227

if_else226:                                       ; preds = %if_merge217
  %fail_val229 = load i32, ptr @__global_fail, align 4
  %241 = add i32 %fail_val229, 1
  store i32 %241, ptr @__global_fail, align 4
  %print_call230 = call i32 (ptr, ...) @printf(ptr @106, ptr @105)
  br label %if_merge227

if_merge227:                                      ; preds = %if_else226, %if_then225
  %242 = load ptr, ptr @__global_s, align 8
  %243 = call ptr @strstr(ptr %242, ptr @107)
  %244 = icmp eq ptr %243, null
  %245 = ptrtoint ptr %243 to i64
  %246 = ptrtoint ptr %242 to i64
  %247 = sub i64 %245, %246
  %248 = sdiv exact i64 %247, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %249 = trunc i64 %248 to i32
  %250 = select i1 %244, i32 -1, i32 %249
  store i32 %250, ptr @__global_idx7, align 4
  %idx7_val = load i32, ptr @__global_idx7, align 4
  %251 = call ptr @__aiml_int_to_string(i32 %idx7_val)
  %252 = call ptr @__aiml_str_concat(ptr @108, ptr %251)
  %print_null_check231 = icmp eq ptr %252, null
  %print_safe_str232 = select i1 %print_null_check231, ptr @print_null_str.42, ptr %252
  %print_call233 = call i32 (ptr, ...) @printf(ptr @109, ptr %print_safe_str232)
  %idx7_val234 = load i32, ptr @__global_idx7, align 4
  %253 = icmp eq i32 %idx7_val234, 0
  br i1 %253, label %if_then235, label %if_else236

if_then235:                                       ; preds = %if_merge227
  %pass_val238 = load i32, ptr @__global_pass, align 4
  %254 = add i32 %pass_val238, 1
  store i32 %254, ptr @__global_pass, align 4
  br label %if_merge237

if_else236:                                       ; preds = %if_merge227
  %fail_val239 = load i32, ptr @__global_fail, align 4
  %255 = add i32 %fail_val239, 1
  store i32 %255, ptr @__global_fail, align 4
  %print_call240 = call i32 (ptr, ...) @printf(ptr @111, ptr @110)
  br label %if_merge237

if_merge237:                                      ; preds = %if_else236, %if_then235
  %256 = load ptr, ptr @__global_empty_s, align 8
  %257 = call ptr @strstr(ptr %256, ptr @112)
  %258 = icmp eq ptr %257, null
  %259 = ptrtoint ptr %257 to i64
  %260 = ptrtoint ptr %256 to i64
  %261 = sub i64 %259, %260
  %262 = sdiv exact i64 %261, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %263 = trunc i64 %262 to i32
  %264 = select i1 %258, i32 -1, i32 %263
  store i32 %264, ptr @__global_idx8, align 4
  %idx8_val = load i32, ptr @__global_idx8, align 4
  %265 = call ptr @__aiml_int_to_string(i32 %idx8_val)
  %266 = call ptr @__aiml_str_concat(ptr @113, ptr %265)
  %print_null_check241 = icmp eq ptr %266, null
  %print_safe_str242 = select i1 %print_null_check241, ptr @print_null_str.44, ptr %266
  %print_call243 = call i32 (ptr, ...) @printf(ptr @114, ptr %print_safe_str242)
  %idx8_val244 = load i32, ptr @__global_idx8, align 4
  %267 = icmp eq i32 %idx8_val244, 0
  br i1 %267, label %if_then245, label %if_else246

if_then245:                                       ; preds = %if_merge237
  %pass_val248 = load i32, ptr @__global_pass, align 4
  %268 = add i32 %pass_val248, 1
  store i32 %268, ptr @__global_pass, align 4
  br label %if_merge247

if_else246:                                       ; preds = %if_merge237
  %fail_val249 = load i32, ptr @__global_fail, align 4
  %269 = add i32 %fail_val249, 1
  store i32 %269, ptr @__global_fail, align 4
  %print_call250 = call i32 (ptr, ...) @printf(ptr @116, ptr @115)
  br label %if_merge247

if_merge247:                                      ; preds = %if_else246, %if_then245
  %270 = load ptr, ptr @__global_empty_s, align 8
  %271 = call ptr @strstr(ptr %270, ptr @117)
  %272 = icmp eq ptr %271, null
  %273 = ptrtoint ptr %271 to i64
  %274 = ptrtoint ptr %270 to i64
  %275 = sub i64 %273, %274
  %276 = sdiv exact i64 %275, ptrtoint (ptr getelementptr (i8, ptr null, i32 1) to i64)
  %277 = trunc i64 %276 to i32
  %278 = select i1 %272, i32 -1, i32 %277
  store i32 %278, ptr @__global_idx9, align 4
  %idx9_val = load i32, ptr @__global_idx9, align 4
  %279 = call ptr @__aiml_int_to_string(i32 %idx9_val)
  %280 = call ptr @__aiml_str_concat(ptr @118, ptr %279)
  %print_null_check251 = icmp eq ptr %280, null
  %print_safe_str252 = select i1 %print_null_check251, ptr @print_null_str.46, ptr %280
  %print_call253 = call i32 (ptr, ...) @printf(ptr @119, ptr %print_safe_str252)
  %idx9_val254 = load i32, ptr @__global_idx9, align 4
  %281 = icmp eq i32 %idx9_val254, -1
  br i1 %281, label %if_then255, label %if_else256

if_then255:                                       ; preds = %if_merge247
  %pass_val258 = load i32, ptr @__global_pass, align 4
  %282 = add i32 %pass_val258, 1
  store i32 %282, ptr @__global_pass, align 4
  br label %if_merge257

if_else256:                                       ; preds = %if_merge247
  %fail_val259 = load i32, ptr @__global_fail, align 4
  %283 = add i32 %fail_val259, 1
  store i32 %283, ptr @__global_fail, align 4
  %print_call260 = call i32 (ptr, ...) @printf(ptr @121, ptr @120)
  br label %if_merge257

if_merge257:                                      ; preds = %if_else256, %if_then255
  store ptr @122, ptr @__global_pi1, align 8
  %284 = load ptr, ptr @__global_pi1, align 8
  %pi32_null = icmp eq ptr %284, null
  br i1 %pi32_null, label %pi32_null_bb, label %pi32_ok

pi32_null_bb:                                     ; preds = %if_merge257
  br label %pi32_merge

pi32_ok:                                          ; preds = %if_merge257
  %285 = call i32 @aiml_str_parse_i32(ptr %284)
  br label %pi32_merge

pi32_merge:                                       ; preds = %pi32_ok, %pi32_null_bb
  %pi32 = phi i32 [ 0, %pi32_null_bb ], [ %285, %pi32_ok ]
  store i32 %pi32, ptr @__global_pi1_val, align 4
  %pi1_val_val = load i32, ptr @__global_pi1_val, align 4
  %286 = call ptr @__aiml_int_to_string(i32 %pi1_val_val)
  %287 = call ptr @__aiml_str_concat(ptr @123, ptr %286)
  %print_null_check261 = icmp eq ptr %287, null
  %print_safe_str262 = select i1 %print_null_check261, ptr @print_null_str.48, ptr %287
  %print_call263 = call i32 (ptr, ...) @printf(ptr @124, ptr %print_safe_str262)
  %pi1_val_val264 = load i32, ptr @__global_pi1_val, align 4
  %288 = icmp eq i32 %pi1_val_val264, 42
  br i1 %288, label %if_then265, label %if_else266

if_then265:                                       ; preds = %pi32_merge
  %pass_val268 = load i32, ptr @__global_pass, align 4
  %289 = add i32 %pass_val268, 1
  store i32 %289, ptr @__global_pass, align 4
  br label %if_merge267

if_else266:                                       ; preds = %pi32_merge
  %fail_val269 = load i32, ptr @__global_fail, align 4
  %290 = add i32 %fail_val269, 1
  store i32 %290, ptr @__global_fail, align 4
  %print_call270 = call i32 (ptr, ...) @printf(ptr @126, ptr @125)
  br label %if_merge267

if_merge267:                                      ; preds = %if_else266, %if_then265
  store ptr @127, ptr @__global_pi2, align 8
  %291 = load ptr, ptr @__global_pi2, align 8
  %pi32_null271 = icmp eq ptr %291, null
  br i1 %pi32_null271, label %pi32_null_bb272, label %pi32_ok273

pi32_null_bb272:                                  ; preds = %if_merge267
  br label %pi32_merge274

pi32_ok273:                                       ; preds = %if_merge267
  %292 = call i32 @aiml_str_parse_i32(ptr %291)
  br label %pi32_merge274

pi32_merge274:                                    ; preds = %pi32_ok273, %pi32_null_bb272
  %pi32275 = phi i32 [ 0, %pi32_null_bb272 ], [ %292, %pi32_ok273 ]
  store i32 %pi32275, ptr @__global_pi2_val, align 4
  %pi2_val_val = load i32, ptr @__global_pi2_val, align 4
  %293 = call ptr @__aiml_int_to_string(i32 %pi2_val_val)
  %294 = call ptr @__aiml_str_concat(ptr @128, ptr %293)
  %print_null_check276 = icmp eq ptr %294, null
  %print_safe_str277 = select i1 %print_null_check276, ptr @print_null_str.50, ptr %294
  %print_call278 = call i32 (ptr, ...) @printf(ptr @129, ptr %print_safe_str277)
  %pi2_val_val279 = load i32, ptr @__global_pi2_val, align 4
  %295 = icmp eq i32 %pi2_val_val279, -7
  br i1 %295, label %if_then280, label %if_else281

if_then280:                                       ; preds = %pi32_merge274
  %pass_val283 = load i32, ptr @__global_pass, align 4
  %296 = add i32 %pass_val283, 1
  store i32 %296, ptr @__global_pass, align 4
  br label %if_merge282

if_else281:                                       ; preds = %pi32_merge274
  %fail_val284 = load i32, ptr @__global_fail, align 4
  %297 = add i32 %fail_val284, 1
  store i32 %297, ptr @__global_fail, align 4
  %print_call285 = call i32 (ptr, ...) @printf(ptr @131, ptr @130)
  br label %if_merge282

if_merge282:                                      ; preds = %if_else281, %if_then280
  store ptr @132, ptr @__global_pi3, align 8
  %298 = load ptr, ptr @__global_pi3, align 8
  %pi32_null286 = icmp eq ptr %298, null
  br i1 %pi32_null286, label %pi32_null_bb287, label %pi32_ok288

pi32_null_bb287:                                  ; preds = %if_merge282
  br label %pi32_merge289

pi32_ok288:                                       ; preds = %if_merge282
  %299 = call i32 @aiml_str_parse_i32(ptr %298)
  br label %pi32_merge289

pi32_merge289:                                    ; preds = %pi32_ok288, %pi32_null_bb287
  %pi32290 = phi i32 [ 0, %pi32_null_bb287 ], [ %299, %pi32_ok288 ]
  store i32 %pi32290, ptr @__global_pi3_val, align 4
  %pi3_val_val = load i32, ptr @__global_pi3_val, align 4
  %300 = call ptr @__aiml_int_to_string(i32 %pi3_val_val)
  %301 = call ptr @__aiml_str_concat(ptr @133, ptr %300)
  %print_null_check291 = icmp eq ptr %301, null
  %print_safe_str292 = select i1 %print_null_check291, ptr @print_null_str.52, ptr %301
  %print_call293 = call i32 (ptr, ...) @printf(ptr @134, ptr %print_safe_str292)
  %pi3_val_val294 = load i32, ptr @__global_pi3_val, align 4
  %302 = icmp eq i32 %pi3_val_val294, 0
  br i1 %302, label %if_then295, label %if_else296

if_then295:                                       ; preds = %pi32_merge289
  %pass_val298 = load i32, ptr @__global_pass, align 4
  %303 = add i32 %pass_val298, 1
  store i32 %303, ptr @__global_pass, align 4
  br label %if_merge297

if_else296:                                       ; preds = %pi32_merge289
  %fail_val299 = load i32, ptr @__global_fail, align 4
  %304 = add i32 %fail_val299, 1
  store i32 %304, ptr @__global_fail, align 4
  %print_call300 = call i32 (ptr, ...) @printf(ptr @136, ptr @135)
  br label %if_merge297

if_merge297:                                      ; preds = %if_else296, %if_then295
  store ptr @137, ptr @__global_pi4, align 8
  %305 = load ptr, ptr @__global_pi4, align 8
  %pi32_null301 = icmp eq ptr %305, null
  br i1 %pi32_null301, label %pi32_null_bb302, label %pi32_ok303

pi32_null_bb302:                                  ; preds = %if_merge297
  br label %pi32_merge304

pi32_ok303:                                       ; preds = %if_merge297
  %306 = call i32 @aiml_str_parse_i32(ptr %305)
  br label %pi32_merge304

pi32_merge304:                                    ; preds = %pi32_ok303, %pi32_null_bb302
  %pi32305 = phi i32 [ 0, %pi32_null_bb302 ], [ %306, %pi32_ok303 ]
  store i32 %pi32305, ptr @__global_pi4_val, align 4
  %pi4_val_val = load i32, ptr @__global_pi4_val, align 4
  %307 = call ptr @__aiml_int_to_string(i32 %pi4_val_val)
  %308 = call ptr @__aiml_str_concat(ptr @138, ptr %307)
  %print_null_check306 = icmp eq ptr %308, null
  %print_safe_str307 = select i1 %print_null_check306, ptr @print_null_str.54, ptr %308
  %print_call308 = call i32 (ptr, ...) @printf(ptr @139, ptr %print_safe_str307)
  %pi4_val_val309 = load i32, ptr @__global_pi4_val, align 4
  %309 = icmp eq i32 %pi4_val_val309, 0
  br i1 %309, label %if_then310, label %if_else311

if_then310:                                       ; preds = %pi32_merge304
  %pass_val313 = load i32, ptr @__global_pass, align 4
  %310 = add i32 %pass_val313, 1
  store i32 %310, ptr @__global_pass, align 4
  br label %if_merge312

if_else311:                                       ; preds = %pi32_merge304
  %fail_val314 = load i32, ptr @__global_fail, align 4
  %311 = add i32 %fail_val314, 1
  store i32 %311, ptr @__global_fail, align 4
  %print_call315 = call i32 (ptr, ...) @printf(ptr @141, ptr @140)
  br label %if_merge312

if_merge312:                                      ; preds = %if_else311, %if_then310
  store ptr @142, ptr @__global_pi5, align 8
  %312 = load ptr, ptr @__global_pi5, align 8
  %pi32_null316 = icmp eq ptr %312, null
  br i1 %pi32_null316, label %pi32_null_bb317, label %pi32_ok318

pi32_null_bb317:                                  ; preds = %if_merge312
  br label %pi32_merge319

pi32_ok318:                                       ; preds = %if_merge312
  %313 = call i32 @aiml_str_parse_i32(ptr %312)
  br label %pi32_merge319

pi32_merge319:                                    ; preds = %pi32_ok318, %pi32_null_bb317
  %pi32320 = phi i32 [ 0, %pi32_null_bb317 ], [ %313, %pi32_ok318 ]
  store i32 %pi32320, ptr @__global_pi5_val, align 4
  %pi5_val_val = load i32, ptr @__global_pi5_val, align 4
  %314 = call ptr @__aiml_int_to_string(i32 %pi5_val_val)
  %315 = call ptr @__aiml_str_concat(ptr @143, ptr %314)
  %print_null_check321 = icmp eq ptr %315, null
  %print_safe_str322 = select i1 %print_null_check321, ptr @print_null_str.56, ptr %315
  %print_call323 = call i32 (ptr, ...) @printf(ptr @144, ptr %print_safe_str322)
  %pi5_val_val324 = load i32, ptr @__global_pi5_val, align 4
  %316 = icmp eq i32 %pi5_val_val324, 123
  br i1 %316, label %if_then325, label %if_else326

if_then325:                                       ; preds = %pi32_merge319
  %pass_val328 = load i32, ptr @__global_pass, align 4
  %317 = add i32 %pass_val328, 1
  store i32 %317, ptr @__global_pass, align 4
  br label %if_merge327

if_else326:                                       ; preds = %pi32_merge319
  %fail_val329 = load i32, ptr @__global_fail, align 4
  %318 = add i32 %fail_val329, 1
  store i32 %318, ptr @__global_fail, align 4
  %print_call330 = call i32 (ptr, ...) @printf(ptr @146, ptr @145)
  br label %if_merge327

if_merge327:                                      ; preds = %if_else326, %if_then325
  store ptr @147, ptr @__global_pi6, align 8
  %319 = load ptr, ptr @__global_pi6, align 8
  %pi32_null331 = icmp eq ptr %319, null
  br i1 %pi32_null331, label %pi32_null_bb332, label %pi32_ok333

pi32_null_bb332:                                  ; preds = %if_merge327
  br label %pi32_merge334

pi32_ok333:                                       ; preds = %if_merge327
  %320 = call i32 @aiml_str_parse_i32(ptr %319)
  br label %pi32_merge334

pi32_merge334:                                    ; preds = %pi32_ok333, %pi32_null_bb332
  %pi32335 = phi i32 [ 0, %pi32_null_bb332 ], [ %320, %pi32_ok333 ]
  store i32 %pi32335, ptr @__global_pi6_val, align 4
  %pi6_val_val = load i32, ptr @__global_pi6_val, align 4
  %321 = call ptr @__aiml_int_to_string(i32 %pi6_val_val)
  %322 = call ptr @__aiml_str_concat(ptr @148, ptr %321)
  %print_null_check336 = icmp eq ptr %322, null
  %print_safe_str337 = select i1 %print_null_check336, ptr @print_null_str.58, ptr %322
  %print_call338 = call i32 (ptr, ...) @printf(ptr @149, ptr %print_safe_str337)
  %pi6_val_val339 = load i32, ptr @__global_pi6_val, align 4
  %323 = icmp eq i32 %pi6_val_val339, 0
  br i1 %323, label %if_then340, label %if_else341

if_then340:                                       ; preds = %pi32_merge334
  %pass_val343 = load i32, ptr @__global_pass, align 4
  %324 = add i32 %pass_val343, 1
  store i32 %324, ptr @__global_pass, align 4
  br label %if_merge342

if_else341:                                       ; preds = %pi32_merge334
  %fail_val344 = load i32, ptr @__global_fail, align 4
  %325 = add i32 %fail_val344, 1
  store i32 %325, ptr @__global_fail, align 4
  %print_call345 = call i32 (ptr, ...) @printf(ptr @151, ptr @150)
  br label %if_merge342

if_merge342:                                      ; preds = %if_else341, %if_then340
  store ptr @152, ptr @__global_pf1, align 8
  %326 = load ptr, ptr @__global_pf1, align 8
  %pf32_null = icmp eq ptr %326, null
  br i1 %pf32_null, label %pf32_null_bb, label %pf32_ok

pf32_null_bb:                                     ; preds = %if_merge342
  br label %pf32_merge

pf32_ok:                                          ; preds = %if_merge342
  %327 = call float @aiml_str_parse_f32(ptr %326)
  br label %pf32_merge

pf32_merge:                                       ; preds = %pf32_ok, %pf32_null_bb
  %pf32 = phi float [ 0.000000e+00, %pf32_null_bb ], [ %327, %pf32_ok ]
  store float %pf32, ptr @__global_pf1_val, align 4
  %pf1_val_val = load float, ptr @__global_pf1_val, align 4
  %328 = call ptr @__aiml_f32_to_string(float %pf1_val_val)
  %329 = call ptr @__aiml_str_concat(ptr @153, ptr %328)
  %print_null_check346 = icmp eq ptr %329, null
  %print_safe_str347 = select i1 %print_null_check346, ptr @print_null_str.60, ptr %329
  %print_call348 = call i32 (ptr, ...) @printf(ptr @154, ptr %print_safe_str347)
  %pf1_val_val349 = load float, ptr @__global_pf1_val, align 4
  %330 = fcmp ogt float %pf1_val_val349, 0x40090A3D80000000
  %pf1_val_val350 = load float, ptr @__global_pf1_val, align 4
  %331 = fcmp olt float %pf1_val_val350, 0x4009333340000000
  %and = and i1 %330, %331
  br i1 %and, label %if_then351, label %if_else352

if_then351:                                       ; preds = %pf32_merge
  %pass_val354 = load i32, ptr @__global_pass, align 4
  %332 = add i32 %pass_val354, 1
  store i32 %332, ptr @__global_pass, align 4
  br label %if_merge353

if_else352:                                       ; preds = %pf32_merge
  %fail_val355 = load i32, ptr @__global_fail, align 4
  %333 = add i32 %fail_val355, 1
  store i32 %333, ptr @__global_fail, align 4
  %print_call356 = call i32 (ptr, ...) @printf(ptr @156, ptr @155)
  br label %if_merge353

if_merge353:                                      ; preds = %if_else352, %if_then351
  store ptr @157, ptr @__global_pf2, align 8
  %334 = load ptr, ptr @__global_pf2, align 8
  %pf32_null357 = icmp eq ptr %334, null
  br i1 %pf32_null357, label %pf32_null_bb358, label %pf32_ok359

pf32_null_bb358:                                  ; preds = %if_merge353
  br label %pf32_merge360

pf32_ok359:                                       ; preds = %if_merge353
  %335 = call float @aiml_str_parse_f32(ptr %334)
  br label %pf32_merge360

pf32_merge360:                                    ; preds = %pf32_ok359, %pf32_null_bb358
  %pf32361 = phi float [ 0.000000e+00, %pf32_null_bb358 ], [ %335, %pf32_ok359 ]
  store float %pf32361, ptr @__global_pf2_val, align 4
  %pf2_val_val = load float, ptr @__global_pf2_val, align 4
  %336 = call ptr @__aiml_f32_to_string(float %pf2_val_val)
  %337 = call ptr @__aiml_str_concat(ptr @158, ptr %336)
  %print_null_check362 = icmp eq ptr %337, null
  %print_safe_str363 = select i1 %print_null_check362, ptr @print_null_str.62, ptr %337
  %print_call364 = call i32 (ptr, ...) @printf(ptr @159, ptr %print_safe_str363)
  %pf2_val_val365 = load float, ptr @__global_pf2_val, align 4
  %338 = fcmp ogt float %pf2_val_val365, 0xBFE051EB80000000
  %pf2_val_val366 = load float, ptr @__global_pf2_val, align 4
  %339 = fcmp olt float %pf2_val_val366, 0xBFDF5C2900000000
  %and367 = and i1 %338, %339
  br i1 %and367, label %if_then368, label %if_else369

if_then368:                                       ; preds = %pf32_merge360
  %pass_val371 = load i32, ptr @__global_pass, align 4
  %340 = add i32 %pass_val371, 1
  store i32 %340, ptr @__global_pass, align 4
  br label %if_merge370

if_else369:                                       ; preds = %pf32_merge360
  %fail_val372 = load i32, ptr @__global_fail, align 4
  %341 = add i32 %fail_val372, 1
  store i32 %341, ptr @__global_fail, align 4
  %print_call373 = call i32 (ptr, ...) @printf(ptr @161, ptr @160)
  br label %if_merge370

if_merge370:                                      ; preds = %if_else369, %if_then368
  store ptr @162, ptr @__global_pf3, align 8
  %342 = load ptr, ptr @__global_pf3, align 8
  %pf32_null374 = icmp eq ptr %342, null
  br i1 %pf32_null374, label %pf32_null_bb375, label %pf32_ok376

pf32_null_bb375:                                  ; preds = %if_merge370
  br label %pf32_merge377

pf32_ok376:                                       ; preds = %if_merge370
  %343 = call float @aiml_str_parse_f32(ptr %342)
  br label %pf32_merge377

pf32_merge377:                                    ; preds = %pf32_ok376, %pf32_null_bb375
  %pf32378 = phi float [ 0.000000e+00, %pf32_null_bb375 ], [ %343, %pf32_ok376 ]
  store float %pf32378, ptr @__global_pf3_val, align 4
  %pf3_val_val = load float, ptr @__global_pf3_val, align 4
  %344 = call ptr @__aiml_f32_to_string(float %pf3_val_val)
  %345 = call ptr @__aiml_str_concat(ptr @163, ptr %344)
  %print_null_check379 = icmp eq ptr %345, null
  %print_safe_str380 = select i1 %print_null_check379, ptr @print_null_str.64, ptr %345
  %print_call381 = call i32 (ptr, ...) @printf(ptr @164, ptr %print_safe_str380)
  %pf3_val_val382 = load float, ptr @__global_pf3_val, align 4
  %346 = fcmp oge float %pf3_val_val382, 0xBF847AE140000000
  %pf3_val_val383 = load float, ptr @__global_pf3_val, align 4
  %347 = fcmp ole float %pf3_val_val383, 0x3F847AE140000000
  %and384 = and i1 %346, %347
  br i1 %and384, label %if_then385, label %if_else386

if_then385:                                       ; preds = %pf32_merge377
  %pass_val388 = load i32, ptr @__global_pass, align 4
  %348 = add i32 %pass_val388, 1
  store i32 %348, ptr @__global_pass, align 4
  br label %if_merge387

if_else386:                                       ; preds = %pf32_merge377
  %fail_val389 = load i32, ptr @__global_fail, align 4
  %349 = add i32 %fail_val389, 1
  store i32 %349, ptr @__global_fail, align 4
  %print_call390 = call i32 (ptr, ...) @printf(ptr @166, ptr @165)
  br label %if_merge387

if_merge387:                                      ; preds = %if_else386, %if_then385
  store ptr @167, ptr @__global_pf4, align 8
  %350 = load ptr, ptr @__global_pf4, align 8
  %pf32_null391 = icmp eq ptr %350, null
  br i1 %pf32_null391, label %pf32_null_bb392, label %pf32_ok393

pf32_null_bb392:                                  ; preds = %if_merge387
  br label %pf32_merge394

pf32_ok393:                                       ; preds = %if_merge387
  %351 = call float @aiml_str_parse_f32(ptr %350)
  br label %pf32_merge394

pf32_merge394:                                    ; preds = %pf32_ok393, %pf32_null_bb392
  %pf32395 = phi float [ 0.000000e+00, %pf32_null_bb392 ], [ %351, %pf32_ok393 ]
  store float %pf32395, ptr @__global_pf4_val, align 4
  %pf4_val_val = load float, ptr @__global_pf4_val, align 4
  %352 = call ptr @__aiml_f32_to_string(float %pf4_val_val)
  %353 = call ptr @__aiml_str_concat(ptr @168, ptr %352)
  %print_null_check396 = icmp eq ptr %353, null
  %print_safe_str397 = select i1 %print_null_check396, ptr @print_null_str.66, ptr %353
  %print_call398 = call i32 (ptr, ...) @printf(ptr @169, ptr %print_safe_str397)
  %pf4_val_val399 = load float, ptr @__global_pf4_val, align 4
  %354 = fcmp oge float %pf4_val_val399, 0xBF847AE140000000
  %pf4_val_val400 = load float, ptr @__global_pf4_val, align 4
  %355 = fcmp ole float %pf4_val_val400, 0x3F847AE140000000
  %and401 = and i1 %354, %355
  br i1 %and401, label %if_then402, label %if_else403

if_then402:                                       ; preds = %pf32_merge394
  %pass_val405 = load i32, ptr @__global_pass, align 4
  %356 = add i32 %pass_val405, 1
  store i32 %356, ptr @__global_pass, align 4
  br label %if_merge404

if_else403:                                       ; preds = %pf32_merge394
  %fail_val406 = load i32, ptr @__global_fail, align 4
  %357 = add i32 %fail_val406, 1
  store i32 %357, ptr @__global_fail, align 4
  %print_call407 = call i32 (ptr, ...) @printf(ptr @171, ptr @170)
  br label %if_merge404

if_merge404:                                      ; preds = %if_else403, %if_then402
  store ptr @172, ptr @__global_pf5, align 8
  %358 = load ptr, ptr @__global_pf5, align 8
  %pf32_null408 = icmp eq ptr %358, null
  br i1 %pf32_null408, label %pf32_null_bb409, label %pf32_ok410

pf32_null_bb409:                                  ; preds = %if_merge404
  br label %pf32_merge411

pf32_ok410:                                       ; preds = %if_merge404
  %359 = call float @aiml_str_parse_f32(ptr %358)
  br label %pf32_merge411

pf32_merge411:                                    ; preds = %pf32_ok410, %pf32_null_bb409
  %pf32412 = phi float [ 0.000000e+00, %pf32_null_bb409 ], [ %359, %pf32_ok410 ]
  store float %pf32412, ptr @__global_pf5_val, align 4
  %pf5_val_val = load float, ptr @__global_pf5_val, align 4
  %360 = call ptr @__aiml_f32_to_string(float %pf5_val_val)
  %361 = call ptr @__aiml_str_concat(ptr @173, ptr %360)
  %print_null_check413 = icmp eq ptr %361, null
  %print_safe_str414 = select i1 %print_null_check413, ptr @print_null_str.68, ptr %361
  %print_call415 = call i32 (ptr, ...) @printf(ptr @174, ptr %print_safe_str414)
  %pf5_val_val416 = load float, ptr @__global_pf5_val, align 4
  %362 = fcmp oge float %pf5_val_val416, 0xBF847AE140000000
  %pf5_val_val417 = load float, ptr @__global_pf5_val, align 4
  %363 = fcmp ole float %pf5_val_val417, 0x3F847AE140000000
  %and418 = and i1 %362, %363
  br i1 %and418, label %if_then419, label %if_else420

if_then419:                                       ; preds = %pf32_merge411
  %pass_val422 = load i32, ptr @__global_pass, align 4
  %364 = add i32 %pass_val422, 1
  store i32 %364, ptr @__global_pass, align 4
  br label %if_merge421

if_else420:                                       ; preds = %pf32_merge411
  %fail_val423 = load i32, ptr @__global_fail, align 4
  %365 = add i32 %fail_val423, 1
  store i32 %365, ptr @__global_fail, align 4
  %print_call424 = call i32 (ptr, ...) @printf(ptr @176, ptr @175)
  br label %if_merge421

if_merge421:                                      ; preds = %if_else420, %if_then419
  store ptr @177, ptr @__global_pf6, align 8
  %366 = load ptr, ptr @__global_pf6, align 8
  %pf32_null425 = icmp eq ptr %366, null
  br i1 %pf32_null425, label %pf32_null_bb426, label %pf32_ok427

pf32_null_bb426:                                  ; preds = %if_merge421
  br label %pf32_merge428

pf32_ok427:                                       ; preds = %if_merge421
  %367 = call float @aiml_str_parse_f32(ptr %366)
  br label %pf32_merge428

pf32_merge428:                                    ; preds = %pf32_ok427, %pf32_null_bb426
  %pf32429 = phi float [ 0.000000e+00, %pf32_null_bb426 ], [ %367, %pf32_ok427 ]
  store float %pf32429, ptr @__global_pf6_val, align 4
  %pf6_val_val = load float, ptr @__global_pf6_val, align 4
  %368 = call ptr @__aiml_f32_to_string(float %pf6_val_val)
  %369 = call ptr @__aiml_str_concat(ptr @178, ptr %368)
  %print_null_check430 = icmp eq ptr %369, null
  %print_safe_str431 = select i1 %print_null_check430, ptr @print_null_str.70, ptr %369
  %print_call432 = call i32 (ptr, ...) @printf(ptr @179, ptr %print_safe_str431)
  %pf6_val_val433 = load float, ptr @__global_pf6_val, align 4
  %370 = fcmp ogt float %pf6_val_val433, 1.490000e+02
  %pf6_val_val434 = load float, ptr @__global_pf6_val, align 4
  %371 = fcmp olt float %pf6_val_val434, 1.510000e+02
  %and435 = and i1 %370, %371
  br i1 %and435, label %if_then436, label %if_else437

if_then436:                                       ; preds = %pf32_merge428
  %pass_val439 = load i32, ptr @__global_pass, align 4
  %372 = add i32 %pass_val439, 1
  store i32 %372, ptr @__global_pass, align 4
  br label %if_merge438

if_else437:                                       ; preds = %pf32_merge428
  %fail_val440 = load i32, ptr @__global_fail, align 4
  %373 = add i32 %fail_val440, 1
  store i32 %373, ptr @__global_fail, align 4
  %print_call441 = call i32 (ptr, ...) @printf(ptr @181, ptr @180)
  br label %if_merge438

if_merge438:                                      ; preds = %if_else437, %if_then436
  store ptr @182, ptr @__global_pf7, align 8
  %374 = load ptr, ptr @__global_pf7, align 8
  %pf32_null442 = icmp eq ptr %374, null
  br i1 %pf32_null442, label %pf32_null_bb443, label %pf32_ok444

pf32_null_bb443:                                  ; preds = %if_merge438
  br label %pf32_merge445

pf32_ok444:                                       ; preds = %if_merge438
  %375 = call float @aiml_str_parse_f32(ptr %374)
  br label %pf32_merge445

pf32_merge445:                                    ; preds = %pf32_ok444, %pf32_null_bb443
  %pf32446 = phi float [ 0.000000e+00, %pf32_null_bb443 ], [ %375, %pf32_ok444 ]
  store float %pf32446, ptr @__global_pf7_val, align 4
  %pf7_val_val = load float, ptr @__global_pf7_val, align 4
  %376 = call ptr @__aiml_f32_to_string(float %pf7_val_val)
  %377 = call ptr @__aiml_str_concat(ptr @183, ptr %376)
  %print_null_check447 = icmp eq ptr %377, null
  %print_safe_str448 = select i1 %print_null_check447, ptr @print_null_str.72, ptr %377
  %print_call449 = call i32 (ptr, ...) @printf(ptr @184, ptr %print_safe_str448)
  %pf7_val_val450 = load float, ptr @__global_pf7_val, align 4
  %378 = fcmp ogt float %pf7_val_val450, 9.900000e+01
  %pf7_val_val451 = load float, ptr @__global_pf7_val, align 4
  %379 = fcmp olt float %pf7_val_val451, 1.010000e+02
  %and452 = and i1 %378, %379
  br i1 %and452, label %if_then453, label %if_else454

if_then453:                                       ; preds = %pf32_merge445
  %pass_val456 = load i32, ptr @__global_pass, align 4
  %380 = add i32 %pass_val456, 1
  store i32 %380, ptr @__global_pass, align 4
  br label %if_merge455

if_else454:                                       ; preds = %pf32_merge445
  %fail_val457 = load i32, ptr @__global_fail, align 4
  %381 = add i32 %fail_val457, 1
  store i32 %381, ptr @__global_fail, align 4
  %print_call458 = call i32 (ptr, ...) @printf(ptr @186, ptr @185)
  br label %if_merge455

if_merge455:                                      ; preds = %if_else454, %if_then453
  %pass_val459 = load i32, ptr @__global_pass, align 4
  %382 = call ptr @__aiml_int_to_string(i32 %pass_val459)
  %383 = call ptr @__aiml_str_concat(ptr @187, ptr %382)
  %384 = call ptr @__aiml_str_concat(ptr %383, ptr @188)
  %fail_val460 = load i32, ptr @__global_fail, align 4
  %385 = call ptr @__aiml_int_to_string(i32 %fail_val460)
  %386 = call ptr @__aiml_str_concat(ptr %384, ptr %385)
  %387 = call ptr @__aiml_str_concat(ptr %386, ptr @189)
  %print_null_check461 = icmp eq ptr %387, null
  %print_safe_str462 = select i1 %print_null_check461, ptr @print_null_str.74, ptr %387
  %print_call463 = call i32 (ptr, ...) @printf(ptr @190, ptr %print_safe_str462)
  %fail_val464 = load i32, ptr @__global_fail, align 4
  %388 = icmp eq i32 %fail_val464, 0
  br i1 %388, label %if_then465, label %if_else466

if_then465:                                       ; preds = %if_merge455
  call void @exit(i32 0)
  br label %if_merge467

if_else466:                                       ; preds = %if_merge455
  call void @exit(i32 1)
  br label %if_merge467

if_merge467:                                      ; preds = %if_else466, %if_then465
  ret void
}

declare ptr @strdup(ptr)

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

declare i32 @printf(ptr, ...)

declare i32 @strcmp(ptr, ptr)

declare ptr @strndup(ptr, i64)

declare i32 @strncmp(ptr, ptr, i64)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare ptr @strstr(ptr, ptr)

declare i32 @aiml_str_parse_i32(ptr)

declare float @aiml_str_parse_f32(ptr)

; Function Attrs: noinline
define internal ptr @__aiml_f32_to_string(float %0) #0 {
entry:
  %1 = call ptr @malloc(i64 32)
  %2 = fpext float %0 to double
  %3 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_f32, double %2)
  ret ptr %1
}

declare void @exit(i32)

attributes #0 = { noinline }
