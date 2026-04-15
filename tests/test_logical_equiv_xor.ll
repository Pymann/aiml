; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@tr_fname = private unnamed_addr constant [5 x i8] c"main\00", align 1
@tr_args = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@tr_dir = private unnamed_addr constant [3 x i8] c"=>\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @main]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 1
@__aiml_schema_count = constant i32 0

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %tr_active = call i32 @aiml_trace_active(ptr @tr_fname)
  %tr_cond = icmp ne i32 %tr_active, 0
  br i1 %tr_cond, label %tr_enter, label %tr_body

tr_enter:                                         ; preds = %entry
  call void @aiml_trace(ptr @tr_fname, ptr @tr_dir, ptr @tr_args)
  br label %tr_body

tr_body:                                          ; preds = %tr_enter, %entry
  %t = alloca i8, align 1
  store i8 0, ptr %t, align 1
  store i8 1, ptr %t, align 1
  %f = alloca i8, align 1
  store i8 0, ptr %f, align 1
  store i8 0, ptr %f, align 1
  %e_tt = alloca i8, align 1
  store i8 0, ptr %e_tt, align 1
  %t_val = load i8, ptr %t, align 1
  %t_val1 = load i8, ptr %t, align 1
  %tobool = icmp ne i8 %t_val, 0
  %tobool2 = icmp ne i8 %t_val1, 0
  %equiv = icmp eq i1 %tobool, %tobool2
  %zext_bool = zext i1 %equiv to i8
  store i8 %zext_bool, ptr %e_tt, align 1
  %e_tf = alloca i8, align 1
  store i8 0, ptr %e_tf, align 1
  %t_val3 = load i8, ptr %t, align 1
  %f_val = load i8, ptr %f, align 1
  %tobool4 = icmp ne i8 %t_val3, 0
  %tobool5 = icmp ne i8 %f_val, 0
  %equiv6 = icmp eq i1 %tobool4, %tobool5
  %zext_bool7 = zext i1 %equiv6 to i8
  store i8 %zext_bool7, ptr %e_tf, align 1
  %e_ft = alloca i8, align 1
  store i8 0, ptr %e_ft, align 1
  %f_val8 = load i8, ptr %f, align 1
  %t_val9 = load i8, ptr %t, align 1
  %tobool10 = icmp ne i8 %f_val8, 0
  %tobool11 = icmp ne i8 %t_val9, 0
  %equiv12 = icmp eq i1 %tobool10, %tobool11
  %zext_bool13 = zext i1 %equiv12 to i8
  store i8 %zext_bool13, ptr %e_ft, align 1
  %e_ff = alloca i8, align 1
  store i8 0, ptr %e_ff, align 1
  %f_val14 = load i8, ptr %f, align 1
  %f_val15 = load i8, ptr %f, align 1
  %tobool16 = icmp ne i8 %f_val14, 0
  %tobool17 = icmp ne i8 %f_val15, 0
  %equiv18 = icmp eq i1 %tobool16, %tobool17
  %zext_bool19 = zext i1 %equiv18 to i8
  store i8 %zext_bool19, ptr %e_ff, align 1
  %e_tt_val = load i8, ptr %e_tt, align 1
  %0 = sext i8 %e_tt_val to i32
  %1 = icmp ne i32 %0, 1
  br i1 %1, label %if_then, label %if_merge

if_then:                                          ; preds = %tr_body
  ret i32 1

if_merge:                                         ; preds = %ret_dead, %tr_body
  %e_tf_val = load i8, ptr %e_tf, align 1
  %2 = sext i8 %e_tf_val to i32
  %3 = icmp ne i32 %2, 0
  br i1 %3, label %if_then20, label %if_merge21

ret_dead:                                         ; No predecessors!
  br label %if_merge

if_then20:                                        ; preds = %if_merge
  ret i32 2

if_merge21:                                       ; preds = %ret_dead22, %if_merge
  %e_ft_val = load i8, ptr %e_ft, align 1
  %4 = sext i8 %e_ft_val to i32
  %5 = icmp ne i32 %4, 0
  br i1 %5, label %if_then23, label %if_merge24

ret_dead22:                                       ; No predecessors!
  br label %if_merge21

if_then23:                                        ; preds = %if_merge21
  ret i32 3

if_merge24:                                       ; preds = %ret_dead25, %if_merge21
  %e_ff_val = load i8, ptr %e_ff, align 1
  %6 = sext i8 %e_ff_val to i32
  %7 = icmp ne i32 %6, 1
  br i1 %7, label %if_then26, label %if_merge27

ret_dead25:                                       ; No predecessors!
  br label %if_merge24

if_then26:                                        ; preds = %if_merge24
  ret i32 4

if_merge27:                                       ; preds = %ret_dead28, %if_merge24
  %x_tt = alloca i8, align 1
  store i8 0, ptr %x_tt, align 1
  %t_val29 = load i8, ptr %t, align 1
  %t_val30 = load i8, ptr %t, align 1
  %tobool31 = icmp ne i8 %t_val29, 0
  %tobool32 = icmp ne i8 %t_val30, 0
  %xor = xor i1 %tobool31, %tobool32
  %zext_bool33 = zext i1 %xor to i8
  store i8 %zext_bool33, ptr %x_tt, align 1
  %x_tf = alloca i8, align 1
  store i8 0, ptr %x_tf, align 1
  %t_val34 = load i8, ptr %t, align 1
  %f_val35 = load i8, ptr %f, align 1
  %tobool36 = icmp ne i8 %t_val34, 0
  %tobool37 = icmp ne i8 %f_val35, 0
  %xor38 = xor i1 %tobool36, %tobool37
  %zext_bool39 = zext i1 %xor38 to i8
  store i8 %zext_bool39, ptr %x_tf, align 1
  %x_ft = alloca i8, align 1
  store i8 0, ptr %x_ft, align 1
  %f_val40 = load i8, ptr %f, align 1
  %t_val41 = load i8, ptr %t, align 1
  %tobool42 = icmp ne i8 %f_val40, 0
  %tobool43 = icmp ne i8 %t_val41, 0
  %xor44 = xor i1 %tobool42, %tobool43
  %zext_bool45 = zext i1 %xor44 to i8
  store i8 %zext_bool45, ptr %x_ft, align 1
  %x_ff = alloca i8, align 1
  store i8 0, ptr %x_ff, align 1
  %f_val46 = load i8, ptr %f, align 1
  %f_val47 = load i8, ptr %f, align 1
  %tobool48 = icmp ne i8 %f_val46, 0
  %tobool49 = icmp ne i8 %f_val47, 0
  %xor50 = xor i1 %tobool48, %tobool49
  %zext_bool51 = zext i1 %xor50 to i8
  store i8 %zext_bool51, ptr %x_ff, align 1
  %x_tt_val = load i8, ptr %x_tt, align 1
  %8 = sext i8 %x_tt_val to i32
  %9 = icmp ne i32 %8, 0
  br i1 %9, label %if_then52, label %if_merge53

ret_dead28:                                       ; No predecessors!
  br label %if_merge27

if_then52:                                        ; preds = %if_merge27
  ret i32 5

if_merge53:                                       ; preds = %ret_dead54, %if_merge27
  %x_tf_val = load i8, ptr %x_tf, align 1
  %10 = sext i8 %x_tf_val to i32
  %11 = icmp ne i32 %10, 1
  br i1 %11, label %if_then55, label %if_merge56

ret_dead54:                                       ; No predecessors!
  br label %if_merge53

if_then55:                                        ; preds = %if_merge53
  ret i32 6

if_merge56:                                       ; preds = %ret_dead57, %if_merge53
  %x_ft_val = load i8, ptr %x_ft, align 1
  %12 = sext i8 %x_ft_val to i32
  %13 = icmp ne i32 %12, 1
  br i1 %13, label %if_then58, label %if_merge59

ret_dead57:                                       ; No predecessors!
  br label %if_merge56

if_then58:                                        ; preds = %if_merge56
  ret i32 7

if_merge59:                                       ; preds = %ret_dead60, %if_merge56
  %x_ff_val = load i8, ptr %x_ff, align 1
  %14 = sext i8 %x_ff_val to i32
  %15 = icmp ne i32 %14, 0
  br i1 %15, label %if_then61, label %if_merge62

ret_dead60:                                       ; No predecessors!
  br label %if_merge59

if_then61:                                        ; preds = %if_merge59
  ret i32 8

if_merge62:                                       ; preds = %ret_dead63, %if_merge59
  %nt = alloca i8, align 1
  store i8 0, ptr %nt, align 1
  %t_val64 = load i8, ptr %t, align 1
  %tobool65 = icmp ne i8 %t_val64, 0
  %not = xor i1 %tobool65, true
  %zext_bool66 = zext i1 %not to i8
  store i8 %zext_bool66, ptr %nt, align 1
  %nf = alloca i8, align 1
  store i8 0, ptr %nf, align 1
  %f_val67 = load i8, ptr %f, align 1
  %tobool68 = icmp ne i8 %f_val67, 0
  %not69 = xor i1 %tobool68, true
  %zext_bool70 = zext i1 %not69 to i8
  store i8 %zext_bool70, ptr %nf, align 1
  %nt_val = load i8, ptr %nt, align 1
  %16 = sext i8 %nt_val to i32
  %17 = icmp ne i32 %16, 0
  br i1 %17, label %if_then71, label %if_merge72

ret_dead63:                                       ; No predecessors!
  br label %if_merge62

if_then71:                                        ; preds = %if_merge62
  ret i32 9

if_merge72:                                       ; preds = %ret_dead73, %if_merge62
  %nf_val = load i8, ptr %nf, align 1
  %18 = sext i8 %nf_val to i32
  %19 = icmp ne i32 %18, 1
  br i1 %19, label %if_then74, label %if_merge75

ret_dead73:                                       ; No predecessors!
  br label %if_merge72

if_then74:                                        ; preds = %if_merge72
  ret i32 10

if_merge75:                                       ; preds = %ret_dead76, %if_merge72
  %a = alloca i32, align 4
  store i32 0, ptr %a, align 4
  store i32 5, ptr %a, align 4
  %b = alloca i32, align 4
  store i32 0, ptr %b, align 4
  store i32 0, ptr %b, align 4
  %ec1 = alloca i8, align 1
  store i8 0, ptr %ec1, align 1
  %a_val = load i32, ptr %a, align 4
  %a_val77 = load i32, ptr %a, align 4
  %tobool78 = icmp ne i32 %a_val, 0
  %tobool79 = icmp ne i32 %a_val77, 0
  %equiv80 = icmp eq i1 %tobool78, %tobool79
  %zext_bool81 = zext i1 %equiv80 to i8
  store i8 %zext_bool81, ptr %ec1, align 1
  %ec2 = alloca i8, align 1
  store i8 0, ptr %ec2, align 1
  %a_val82 = load i32, ptr %a, align 4
  %b_val = load i32, ptr %b, align 4
  %tobool83 = icmp ne i32 %a_val82, 0
  %tobool84 = icmp ne i32 %b_val, 0
  %equiv85 = icmp eq i1 %tobool83, %tobool84
  %zext_bool86 = zext i1 %equiv85 to i8
  store i8 %zext_bool86, ptr %ec2, align 1
  %xc1 = alloca i8, align 1
  store i8 0, ptr %xc1, align 1
  %a_val87 = load i32, ptr %a, align 4
  %b_val88 = load i32, ptr %b, align 4
  %tobool89 = icmp ne i32 %a_val87, 0
  %tobool90 = icmp ne i32 %b_val88, 0
  %xor91 = xor i1 %tobool89, %tobool90
  %zext_bool92 = zext i1 %xor91 to i8
  store i8 %zext_bool92, ptr %xc1, align 1
  %ec1_val = load i8, ptr %ec1, align 1
  %20 = sext i8 %ec1_val to i32
  %21 = icmp ne i32 %20, 1
  br i1 %21, label %if_then93, label %if_merge94

ret_dead76:                                       ; No predecessors!
  br label %if_merge75

if_then93:                                        ; preds = %if_merge75
  ret i32 11

if_merge94:                                       ; preds = %ret_dead95, %if_merge75
  %ec2_val = load i8, ptr %ec2, align 1
  %22 = sext i8 %ec2_val to i32
  %23 = icmp ne i32 %22, 0
  br i1 %23, label %if_then96, label %if_merge97

ret_dead95:                                       ; No predecessors!
  br label %if_merge94

if_then96:                                        ; preds = %if_merge94
  ret i32 12

if_merge97:                                       ; preds = %ret_dead98, %if_merge94
  %xc1_val = load i8, ptr %xc1, align 1
  %24 = sext i8 %xc1_val to i32
  %25 = icmp ne i32 %24, 1
  br i1 %25, label %if_then99, label %if_merge100

ret_dead98:                                       ; No predecessors!
  br label %if_merge97

if_then99:                                        ; preds = %if_merge97
  ret i32 13

if_merge100:                                      ; preds = %ret_dead101, %if_merge97
  ret i32 0

ret_dead101:                                      ; No predecessors!
  br label %if_merge100

ret_dead102:                                      ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare i32 @aiml_trace_active(ptr)

declare void @aiml_trace(ptr, ptr, ptr)
