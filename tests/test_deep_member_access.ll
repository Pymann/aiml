; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [32 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Outer = type { %Middle, i32 }
%Middle = type { %Inner, i32 }
%Inner = type { i32 }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [12 x i8] c"deep read: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [13 x i8] c"deep write: \00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [18 x i8] c"two level count: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [14 x i8] c"three level: \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [26 x i8] c"three level after write: \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_sname_Inner = private constant [6 x i8] c"Inner\00"
@__aiml_sfield_Inner_value = private constant [6 x i8] c"value\00"
@__aiml_sname_Middle = private constant [7 x i8] c"Middle\00"
@__aiml_sfield_Middle_inner = private constant [6 x i8] c"inner\00"
@__aiml_sfield_Middle_count = private constant [6 x i8] c"count\00"
@__aiml_sname_Outer = private constant [6 x i8] c"Outer\00"
@__aiml_sfield_Outer_mid = private constant [4 x i8] c"mid\00"
@__aiml_sfield_Outer_tag = private constant [4 x i8] c"tag\00"
@__aiml_schemas = global [3 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_Inner, i32 1, i32 1, i32 4, i32 4, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Inner_value }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Middle, i32 1, i32 2, i32 8, i32 8, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Middle_inner }, %AimlFieldInfo { ptr @__aiml_sfield_Middle_count }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Outer, i32 1, i32 2, i32 12, i32 16, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Outer_mid }, %AimlFieldInfo { ptr @__aiml_sfield_Outer_tag }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 3

define void @__aiml_init() {
entry:
  %o = alloca %Outer, align 8
  store %Outer zeroinitializer, ptr %o, align 4
  %chain_gep_w = getelementptr inbounds %Outer, ptr %o, i32 0, i32 0
  %chain_gep_w1 = getelementptr inbounds %Middle, ptr %chain_gep_w, i32 0, i32 0
  %set_prop_ptr_w = getelementptr inbounds %Inner, ptr %chain_gep_w1, i32 0, i32 0
  store i32 99, ptr %set_prop_ptr_w, align 4
  %r = alloca i32, align 4
  store i32 0, ptr %r, align 4
  %chain_gep = getelementptr inbounds %Outer, ptr %o, i32 0, i32 0
  %chain_gep2 = getelementptr inbounds %Middle, ptr %chain_gep, i32 0, i32 0
  %prop_ptr = getelementptr inbounds %Inner, ptr %chain_gep2, i32 0, i32 0
  %value_val = load i32, ptr %prop_ptr, align 4
  store i32 %value_val, ptr %r, align 4
  %r_val = load i32, ptr %r, align 4
  %0 = call ptr @__aiml_int_to_string(i32 %r_val)
  %1 = call ptr @__aiml_str_concat(ptr @0, ptr %0)
  %print_null_check = icmp eq ptr %1, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %1
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr %print_safe_str)
  %chain_gep_w3 = getelementptr inbounds %Outer, ptr %o, i32 0, i32 0
  %chain_gep_w4 = getelementptr inbounds %Middle, ptr %chain_gep_w3, i32 0, i32 0
  %set_prop_ptr_w5 = getelementptr inbounds %Inner, ptr %chain_gep_w4, i32 0, i32 0
  store i32 7, ptr %set_prop_ptr_w5, align 4
  %w = alloca i32, align 4
  store i32 0, ptr %w, align 4
  %chain_gep6 = getelementptr inbounds %Outer, ptr %o, i32 0, i32 0
  %chain_gep7 = getelementptr inbounds %Middle, ptr %chain_gep6, i32 0, i32 0
  %prop_ptr8 = getelementptr inbounds %Inner, ptr %chain_gep7, i32 0, i32 0
  %value_val9 = load i32, ptr %prop_ptr8, align 4
  store i32 %value_val9, ptr %w, align 4
  %w_val = load i32, ptr %w, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %w_val)
  %3 = call ptr @__aiml_str_concat(ptr @2, ptr %2)
  %print_null_check10 = icmp eq ptr %3, null
  %print_safe_str11 = select i1 %print_null_check10, ptr @print_null_str.1, ptr %3
  %print_call12 = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str11)
  %chain_gep_w13 = getelementptr inbounds %Outer, ptr %o, i32 0, i32 0
  %set_prop_ptr_w14 = getelementptr inbounds %Middle, ptr %chain_gep_w13, i32 0, i32 1
  store i32 10, ptr %set_prop_ptr_w14, align 4
  %mc = alloca i32, align 4
  store i32 0, ptr %mc, align 4
  %chain_gep15 = getelementptr inbounds %Outer, ptr %o, i32 0, i32 0
  %prop_ptr16 = getelementptr inbounds %Middle, ptr %chain_gep15, i32 0, i32 1
  %count_val = load i32, ptr %prop_ptr16, align 4
  store i32 %count_val, ptr %mc, align 4
  %mc_val = load i32, ptr %mc, align 4
  %4 = call ptr @__aiml_int_to_string(i32 %mc_val)
  %5 = call ptr @__aiml_str_concat(ptr @4, ptr %4)
  %print_null_check17 = icmp eq ptr %5, null
  %print_safe_str18 = select i1 %print_null_check17, ptr @print_null_str.2, ptr %5
  %print_call19 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str18)
  %o2 = alloca %Outer, align 8
  store %Outer zeroinitializer, ptr %o2, align 4
  %chain_gep_w20 = getelementptr inbounds %Outer, ptr %o2, i32 0, i32 0
  %chain_gep_w21 = getelementptr inbounds %Middle, ptr %chain_gep_w20, i32 0, i32 0
  %set_prop_ptr_w22 = getelementptr inbounds %Inner, ptr %chain_gep_w21, i32 0, i32 0
  store i32 42, ptr %set_prop_ptr_w22, align 4
  %pr = alloca i32, align 4
  store i32 0, ptr %pr, align 4
  %chain_gep23 = getelementptr inbounds %Outer, ptr %o2, i32 0, i32 0
  %chain_gep24 = getelementptr inbounds %Middle, ptr %chain_gep23, i32 0, i32 0
  %prop_ptr25 = getelementptr inbounds %Inner, ptr %chain_gep24, i32 0, i32 0
  %value_val26 = load i32, ptr %prop_ptr25, align 4
  store i32 %value_val26, ptr %pr, align 4
  %pr_val = load i32, ptr %pr, align 4
  %6 = call ptr @__aiml_int_to_string(i32 %pr_val)
  %7 = call ptr @__aiml_str_concat(ptr @6, ptr %6)
  %print_null_check27 = icmp eq ptr %7, null
  %print_safe_str28 = select i1 %print_null_check27, ptr @print_null_str.3, ptr %7
  %print_call29 = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str28)
  %chain_gep_w30 = getelementptr inbounds %Outer, ptr %o2, i32 0, i32 0
  %chain_gep_w31 = getelementptr inbounds %Middle, ptr %chain_gep_w30, i32 0, i32 0
  %set_prop_ptr_w32 = getelementptr inbounds %Inner, ptr %chain_gep_w31, i32 0, i32 0
  store i32 55, ptr %set_prop_ptr_w32, align 4
  %pw = alloca i32, align 4
  store i32 0, ptr %pw, align 4
  %chain_gep33 = getelementptr inbounds %Outer, ptr %o2, i32 0, i32 0
  %chain_gep34 = getelementptr inbounds %Middle, ptr %chain_gep33, i32 0, i32 0
  %prop_ptr35 = getelementptr inbounds %Inner, ptr %chain_gep34, i32 0, i32 0
  %value_val36 = load i32, ptr %prop_ptr35, align 4
  store i32 %value_val36, ptr %pw, align 4
  %pw_val = load i32, ptr %pw, align 4
  %8 = call ptr @__aiml_int_to_string(i32 %pw_val)
  %9 = call ptr @__aiml_str_concat(ptr @8, ptr %8)
  %print_null_check37 = icmp eq ptr %9, null
  %print_safe_str38 = select i1 %print_null_check37, ptr @print_null_str.4, ptr %9
  %print_call39 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str38)
  ret void
}

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

attributes #0 = { noinline }
