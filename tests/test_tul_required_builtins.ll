; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_string = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__global_pass = global i32 0
@__global_fail = global i32 0
@__global_mixed = global ptr null
@__global_lowered = global ptr null
@__global_empty_s = global ptr null
@__global_empty_lower = global ptr null
@__global_trimmed = global ptr null
@__global_spaces = global ptr null
@__global_spaces_trimmed = global ptr null
@__global_csv = global ptr null
@__global_replaced = global ptr null
@__global_unchanged = global ptr null
@__global_words_src = global ptr null
@__global_words = global %list_string zeroinitializer
@__global_csv_parts = global %list_string zeroinitializer
@__global_single_part = global %list_string zeroinitializer
@0 = private unnamed_addr constant [16 x i8] c"  HeLLo WOrld  \00", align 1
@1 = private unnamed_addr constant [8 x i8] c"lower='\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@2 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [16 x i8] c"  hello world  \00", align 1
@5 = private unnamed_addr constant [17 x i8] c"FAIL lower basic\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@8 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@9 = private unnamed_addr constant [17 x i8] c"FAIL lower empty\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [7 x i8] c"trim='\00", align 1
@12 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [12 x i8] c"HeLLo WOrld\00", align 1
@15 = private unnamed_addr constant [16 x i8] c"FAIL trim basic\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [7 x i8] c"   \09  \00", align 1
@18 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@19 = private unnamed_addr constant [26 x i8] c"FAIL trim whitespace-only\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [22 x i8] c"alpha,beta,gamma,beta\00", align 1
@22 = private unnamed_addr constant [5 x i8] c"beta\00", align 1
@23 = private unnamed_addr constant [2 x i8] c"B\00", align 1
@24 = private unnamed_addr constant [10 x i8] c"replace='\00", align 1
@25 = private unnamed_addr constant [2 x i8] c"'\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [16 x i8] c"alpha,B,gamma,B\00", align 1
@28 = private unnamed_addr constant [29 x i8] c"FAIL replace all occurrences\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [6 x i8] c"delta\00", align 1
@31 = private unnamed_addr constant [2 x i8] c"X\00", align 1
@32 = private unnamed_addr constant [22 x i8] c"alpha,beta,gamma,beta\00", align 1
@33 = private unnamed_addr constant [18 x i8] c"FAIL replace miss\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@35 = private unnamed_addr constant [17 x i8] c"fire earth water\00", align 1
@36 = private unnamed_addr constant [2 x i8] c" \00", align 1
@37 = private unnamed_addr constant [22 x i8] c"FAIL split words len=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [5 x i8] c"fire\00", align 1
@40 = private unnamed_addr constant [20 x i8] c"FAIL split words-0=\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [6 x i8] c"earth\00", align 1
@43 = private unnamed_addr constant [20 x i8] c"FAIL split words-1=\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [6 x i8] c"water\00", align 1
@46 = private unnamed_addr constant [20 x i8] c"FAIL split words-2=\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@47 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@48 = private unnamed_addr constant [2 x i8] c",\00", align 1
@49 = private unnamed_addr constant [20 x i8] c"FAIL split csv len=\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@50 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@51 = private unnamed_addr constant [5 x i8] c"beta\00", align 1
@52 = private unnamed_addr constant [21 x i8] c"FAIL split csv last=\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@54 = private unnamed_addr constant [2 x i8] c",\00", align 1
@55 = private unnamed_addr constant [25 x i8] c"FAIL split no-delim len=\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@57 = private unnamed_addr constant [12 x i8] c"HeLLo WOrld\00", align 1
@58 = private unnamed_addr constant [25 x i8] c"FAIL split no-delim val=\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@59 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@60 = private unnamed_addr constant [24 x i8] c"tul required builtins: \00", align 1
@61 = private unnamed_addr constant [10 x i8] c" passed, \00", align 1
@62 = private unnamed_addr constant [8 x i8] c" failed\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@63 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  store i32 0, ptr @__global_pass, align 4
  store i32 0, ptr @__global_fail, align 4
  store ptr @0, ptr @__global_mixed, align 8
  %0 = load ptr, ptr @__global_mixed, align 8
  %1 = call ptr @aiml_str_lower(ptr %0)
  store ptr %1, ptr @__global_lowered, align 8
  %lowered_val = load ptr, ptr @__global_lowered, align 8
  %2 = call ptr @__aiml_str_concat(ptr @1, ptr %lowered_val)
  %3 = call ptr @__aiml_str_concat(ptr %2, ptr @2)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str)
  %lowered_val1 = load ptr, ptr @__global_lowered, align 8
  %4 = icmp eq ptr %lowered_val1, null
  %5 = icmp eq ptr %lowered_val1, @4
  %6 = or i1 %4, false
  br i1 %6, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %entry
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %entry
  %7 = call i32 @strcmp(ptr %lowered_val1, ptr @4)
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
  %print_call2 = call i32 (ptr, ...) @printf(ptr @6, ptr @5)
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  store ptr @7, ptr @__global_empty_s, align 8
  %11 = load ptr, ptr @__global_empty_s, align 8
  %12 = call ptr @aiml_str_lower(ptr %11)
  store ptr %12, ptr @__global_empty_lower, align 8
  %empty_lower_val = load ptr, ptr @__global_empty_lower, align 8
  %13 = icmp eq ptr %empty_lower_val, null
  %14 = icmp eq ptr %empty_lower_val, @8
  %15 = or i1 %13, false
  br i1 %15, label %cmp_check_null3, label %cmp_do_strcmp4

cmp_check_null3:                                  ; preds = %if_merge
  br label %cmp_merge5

cmp_do_strcmp4:                                   ; preds = %if_merge
  %16 = call i32 @strcmp(ptr %empty_lower_val, ptr @8)
  %17 = icmp eq i32 %16, 0
  br label %cmp_merge5

cmp_merge5:                                       ; preds = %cmp_do_strcmp4, %cmp_check_null3
  %strcmp_res6 = phi i1 [ %14, %cmp_check_null3 ], [ %17, %cmp_do_strcmp4 ]
  br i1 %strcmp_res6, label %if_then7, label %if_else8

if_then7:                                         ; preds = %cmp_merge5
  %pass_val10 = load i32, ptr @__global_pass, align 4
  %18 = add i32 %pass_val10, 1
  store i32 %18, ptr @__global_pass, align 4
  br label %if_merge9

if_else8:                                         ; preds = %cmp_merge5
  %fail_val11 = load i32, ptr @__global_fail, align 4
  %19 = add i32 %fail_val11, 1
  store i32 %19, ptr @__global_fail, align 4
  %print_call12 = call i32 (ptr, ...) @printf(ptr @10, ptr @9)
  br label %if_merge9

if_merge9:                                        ; preds = %if_else8, %if_then7
  %20 = load ptr, ptr @__global_mixed, align 8
  %21 = call ptr @aiml_str_trim(ptr %20)
  store ptr %21, ptr @__global_trimmed, align 8
  %trimmed_val = load ptr, ptr @__global_trimmed, align 8
  %22 = call ptr @__aiml_str_concat(ptr @11, ptr %trimmed_val)
  %23 = call ptr @__aiml_str_concat(ptr %22, ptr @12)
  %print_null_check13 = icmp eq ptr %23, null
  %print_safe_str14 = select i1 %print_null_check13, ptr @print_null_str.3, ptr %23
  %print_call15 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str14)
  %trimmed_val16 = load ptr, ptr @__global_trimmed, align 8
  %24 = icmp eq ptr %trimmed_val16, null
  %25 = icmp eq ptr %trimmed_val16, @14
  %26 = or i1 %24, false
  br i1 %26, label %cmp_check_null17, label %cmp_do_strcmp18

cmp_check_null17:                                 ; preds = %if_merge9
  br label %cmp_merge19

cmp_do_strcmp18:                                  ; preds = %if_merge9
  %27 = call i32 @strcmp(ptr %trimmed_val16, ptr @14)
  %28 = icmp eq i32 %27, 0
  br label %cmp_merge19

cmp_merge19:                                      ; preds = %cmp_do_strcmp18, %cmp_check_null17
  %strcmp_res20 = phi i1 [ %25, %cmp_check_null17 ], [ %28, %cmp_do_strcmp18 ]
  br i1 %strcmp_res20, label %if_then21, label %if_else22

if_then21:                                        ; preds = %cmp_merge19
  %pass_val24 = load i32, ptr @__global_pass, align 4
  %29 = add i32 %pass_val24, 1
  store i32 %29, ptr @__global_pass, align 4
  br label %if_merge23

if_else22:                                        ; preds = %cmp_merge19
  %fail_val25 = load i32, ptr @__global_fail, align 4
  %30 = add i32 %fail_val25, 1
  store i32 %30, ptr @__global_fail, align 4
  %print_call26 = call i32 (ptr, ...) @printf(ptr @16, ptr @15)
  br label %if_merge23

if_merge23:                                       ; preds = %if_else22, %if_then21
  store ptr @17, ptr @__global_spaces, align 8
  %31 = load ptr, ptr @__global_spaces, align 8
  %32 = call ptr @aiml_str_trim(ptr %31)
  store ptr %32, ptr @__global_spaces_trimmed, align 8
  %spaces_trimmed_val = load ptr, ptr @__global_spaces_trimmed, align 8
  %33 = icmp eq ptr %spaces_trimmed_val, null
  %34 = icmp eq ptr %spaces_trimmed_val, @18
  %35 = or i1 %33, false
  br i1 %35, label %cmp_check_null27, label %cmp_do_strcmp28

cmp_check_null27:                                 ; preds = %if_merge23
  br label %cmp_merge29

cmp_do_strcmp28:                                  ; preds = %if_merge23
  %36 = call i32 @strcmp(ptr %spaces_trimmed_val, ptr @18)
  %37 = icmp eq i32 %36, 0
  br label %cmp_merge29

cmp_merge29:                                      ; preds = %cmp_do_strcmp28, %cmp_check_null27
  %strcmp_res30 = phi i1 [ %34, %cmp_check_null27 ], [ %37, %cmp_do_strcmp28 ]
  br i1 %strcmp_res30, label %if_then31, label %if_else32

if_then31:                                        ; preds = %cmp_merge29
  %pass_val34 = load i32, ptr @__global_pass, align 4
  %38 = add i32 %pass_val34, 1
  store i32 %38, ptr @__global_pass, align 4
  br label %if_merge33

if_else32:                                        ; preds = %cmp_merge29
  %fail_val35 = load i32, ptr @__global_fail, align 4
  %39 = add i32 %fail_val35, 1
  store i32 %39, ptr @__global_fail, align 4
  %print_call36 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
  br label %if_merge33

if_merge33:                                       ; preds = %if_else32, %if_then31
  store ptr @21, ptr @__global_csv, align 8
  %40 = load ptr, ptr @__global_csv, align 8
  %41 = call ptr @aiml_str_replace(ptr %40, ptr @22, ptr @23)
  store ptr %41, ptr @__global_replaced, align 8
  %replaced_val = load ptr, ptr @__global_replaced, align 8
  %42 = call ptr @__aiml_str_concat(ptr @24, ptr %replaced_val)
  %43 = call ptr @__aiml_str_concat(ptr %42, ptr @25)
  %print_null_check37 = icmp eq ptr %43, null
  %print_safe_str38 = select i1 %print_null_check37, ptr @print_null_str.6, ptr %43
  %print_call39 = call i32 (ptr, ...) @printf(ptr @26, ptr %print_safe_str38)
  %replaced_val40 = load ptr, ptr @__global_replaced, align 8
  %44 = icmp eq ptr %replaced_val40, null
  %45 = icmp eq ptr %replaced_val40, @27
  %46 = or i1 %44, false
  br i1 %46, label %cmp_check_null41, label %cmp_do_strcmp42

cmp_check_null41:                                 ; preds = %if_merge33
  br label %cmp_merge43

cmp_do_strcmp42:                                  ; preds = %if_merge33
  %47 = call i32 @strcmp(ptr %replaced_val40, ptr @27)
  %48 = icmp eq i32 %47, 0
  br label %cmp_merge43

cmp_merge43:                                      ; preds = %cmp_do_strcmp42, %cmp_check_null41
  %strcmp_res44 = phi i1 [ %45, %cmp_check_null41 ], [ %48, %cmp_do_strcmp42 ]
  br i1 %strcmp_res44, label %if_then45, label %if_else46

if_then45:                                        ; preds = %cmp_merge43
  %pass_val48 = load i32, ptr @__global_pass, align 4
  %49 = add i32 %pass_val48, 1
  store i32 %49, ptr @__global_pass, align 4
  br label %if_merge47

if_else46:                                        ; preds = %cmp_merge43
  %fail_val49 = load i32, ptr @__global_fail, align 4
  %50 = add i32 %fail_val49, 1
  store i32 %50, ptr @__global_fail, align 4
  %print_call50 = call i32 (ptr, ...) @printf(ptr @29, ptr @28)
  br label %if_merge47

if_merge47:                                       ; preds = %if_else46, %if_then45
  %51 = load ptr, ptr @__global_csv, align 8
  %52 = call ptr @aiml_str_replace(ptr %51, ptr @30, ptr @31)
  store ptr %52, ptr @__global_unchanged, align 8
  %unchanged_val = load ptr, ptr @__global_unchanged, align 8
  %53 = icmp eq ptr %unchanged_val, null
  %54 = icmp eq ptr %unchanged_val, @32
  %55 = or i1 %53, false
  br i1 %55, label %cmp_check_null51, label %cmp_do_strcmp52

cmp_check_null51:                                 ; preds = %if_merge47
  br label %cmp_merge53

cmp_do_strcmp52:                                  ; preds = %if_merge47
  %56 = call i32 @strcmp(ptr %unchanged_val, ptr @32)
  %57 = icmp eq i32 %56, 0
  br label %cmp_merge53

cmp_merge53:                                      ; preds = %cmp_do_strcmp52, %cmp_check_null51
  %strcmp_res54 = phi i1 [ %54, %cmp_check_null51 ], [ %57, %cmp_do_strcmp52 ]
  br i1 %strcmp_res54, label %if_then55, label %if_else56

if_then55:                                        ; preds = %cmp_merge53
  %pass_val58 = load i32, ptr @__global_pass, align 4
  %58 = add i32 %pass_val58, 1
  store i32 %58, ptr @__global_pass, align 4
  br label %if_merge57

if_else56:                                        ; preds = %cmp_merge53
  %fail_val59 = load i32, ptr @__global_fail, align 4
  %59 = add i32 %fail_val59, 1
  store i32 %59, ptr @__global_fail, align 4
  %print_call60 = call i32 (ptr, ...) @printf(ptr @34, ptr @33)
  br label %if_merge57

if_merge57:                                       ; preds = %if_else56, %if_then55
  store ptr @35, ptr @__global_words_src, align 8
  store i32 0, ptr @__global_words, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 1), align 4
  %60 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %60, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 2), align 8
  %61 = load ptr, ptr @__global_words_src, align 8
  %split_alloca = alloca %GenericList, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %split_alloca, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %split_alloca, i32 0, i32 1
  store i32 8, ptr %capPtr, align 4
  %62 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr = getelementptr inbounds %GenericList, ptr %split_alloca, i32 0, i32 2
  store ptr %62, ptr %dataPtr, align 8
  %63 = call i32 @aiml_str_split_fill(ptr %61, ptr @36, ptr %split_alloca)
  %64 = call ptr @memcpy(ptr @__global_words, ptr %split_alloca, i64 16)
  %sizeVal = load i32, ptr @__global_words, align 4
  %65 = icmp ne i32 %sizeVal, 3
  br i1 %65, label %if_then61, label %if_else62

if_then61:                                        ; preds = %if_merge57
  %fail_val64 = load i32, ptr @__global_fail, align 4
  %66 = add i32 %fail_val64, 1
  store i32 %66, ptr @__global_fail, align 4
  %sizeVal65 = load i32, ptr @__global_words, align 4
  %67 = call ptr @__aiml_int_to_string(i32 %sizeVal65)
  %68 = call ptr @__aiml_str_concat(ptr @37, ptr %67)
  %print_null_check66 = icmp eq ptr %68, null
  %print_safe_str67 = select i1 %print_null_check66, ptr @print_null_str.9, ptr %68
  %print_call68 = call i32 (ptr, ...) @printf(ptr @38, ptr %print_safe_str67)
  br label %if_merge63

if_else62:                                        ; preds = %if_merge57
  %pass_val69 = load i32, ptr @__global_pass, align 4
  %69 = add i32 %pass_val69, 1
  store i32 %69, ptr @__global_pass, align 4
  br label %if_merge63

if_merge63:                                       ; preds = %if_else62, %if_then61
  %dataBase = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 2), align 8
  %70 = getelementptr inbounds ptr, ptr %dataBase, i32 0
  %lst_elem = load ptr, ptr %70, align 8
  %71 = icmp eq ptr %lst_elem, null
  %72 = icmp eq ptr %lst_elem, @39
  %73 = or i1 %71, false
  br i1 %73, label %cmp_check_null70, label %cmp_do_strcmp71

cmp_check_null70:                                 ; preds = %if_merge63
  br label %cmp_merge72

cmp_do_strcmp71:                                  ; preds = %if_merge63
  %74 = call i32 @strcmp(ptr %lst_elem, ptr @39)
  %75 = icmp eq i32 %74, 0
  br label %cmp_merge72

cmp_merge72:                                      ; preds = %cmp_do_strcmp71, %cmp_check_null70
  %strcmp_res73 = phi i1 [ %72, %cmp_check_null70 ], [ %75, %cmp_do_strcmp71 ]
  %76 = sext i1 %strcmp_res73 to i32
  %77 = icmp eq i32 %76, 0
  br i1 %77, label %if_then74, label %if_else75

if_then74:                                        ; preds = %cmp_merge72
  %fail_val77 = load i32, ptr @__global_fail, align 4
  %78 = add i32 %fail_val77, 1
  store i32 %78, ptr @__global_fail, align 4
  %dataBase78 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 2), align 8
  %79 = getelementptr inbounds ptr, ptr %dataBase78, i32 0
  %lst_elem79 = load ptr, ptr %79, align 8
  %80 = call ptr @__aiml_str_concat(ptr @40, ptr %lst_elem79)
  %print_null_check80 = icmp eq ptr %80, null
  %print_safe_str81 = select i1 %print_null_check80, ptr @print_null_str.10, ptr %80
  %print_call82 = call i32 (ptr, ...) @printf(ptr @41, ptr %print_safe_str81)
  br label %if_merge76

if_else75:                                        ; preds = %cmp_merge72
  %pass_val83 = load i32, ptr @__global_pass, align 4
  %81 = add i32 %pass_val83, 1
  store i32 %81, ptr @__global_pass, align 4
  br label %if_merge76

if_merge76:                                       ; preds = %if_else75, %if_then74
  %dataBase84 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 2), align 8
  %82 = getelementptr inbounds ptr, ptr %dataBase84, i32 1
  %lst_elem85 = load ptr, ptr %82, align 8
  %83 = icmp eq ptr %lst_elem85, null
  %84 = icmp eq ptr %lst_elem85, @42
  %85 = or i1 %83, false
  br i1 %85, label %cmp_check_null86, label %cmp_do_strcmp87

cmp_check_null86:                                 ; preds = %if_merge76
  br label %cmp_merge88

cmp_do_strcmp87:                                  ; preds = %if_merge76
  %86 = call i32 @strcmp(ptr %lst_elem85, ptr @42)
  %87 = icmp eq i32 %86, 0
  br label %cmp_merge88

cmp_merge88:                                      ; preds = %cmp_do_strcmp87, %cmp_check_null86
  %strcmp_res89 = phi i1 [ %84, %cmp_check_null86 ], [ %87, %cmp_do_strcmp87 ]
  %88 = sext i1 %strcmp_res89 to i32
  %89 = icmp eq i32 %88, 0
  br i1 %89, label %if_then90, label %if_else91

if_then90:                                        ; preds = %cmp_merge88
  %fail_val93 = load i32, ptr @__global_fail, align 4
  %90 = add i32 %fail_val93, 1
  store i32 %90, ptr @__global_fail, align 4
  %dataBase94 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 2), align 8
  %91 = getelementptr inbounds ptr, ptr %dataBase94, i32 1
  %lst_elem95 = load ptr, ptr %91, align 8
  %92 = call ptr @__aiml_str_concat(ptr @43, ptr %lst_elem95)
  %print_null_check96 = icmp eq ptr %92, null
  %print_safe_str97 = select i1 %print_null_check96, ptr @print_null_str.11, ptr %92
  %print_call98 = call i32 (ptr, ...) @printf(ptr @44, ptr %print_safe_str97)
  br label %if_merge92

if_else91:                                        ; preds = %cmp_merge88
  %pass_val99 = load i32, ptr @__global_pass, align 4
  %93 = add i32 %pass_val99, 1
  store i32 %93, ptr @__global_pass, align 4
  br label %if_merge92

if_merge92:                                       ; preds = %if_else91, %if_then90
  %dataBase100 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 2), align 8
  %94 = getelementptr inbounds ptr, ptr %dataBase100, i32 2
  %lst_elem101 = load ptr, ptr %94, align 8
  %95 = icmp eq ptr %lst_elem101, null
  %96 = icmp eq ptr %lst_elem101, @45
  %97 = or i1 %95, false
  br i1 %97, label %cmp_check_null102, label %cmp_do_strcmp103

cmp_check_null102:                                ; preds = %if_merge92
  br label %cmp_merge104

cmp_do_strcmp103:                                 ; preds = %if_merge92
  %98 = call i32 @strcmp(ptr %lst_elem101, ptr @45)
  %99 = icmp eq i32 %98, 0
  br label %cmp_merge104

cmp_merge104:                                     ; preds = %cmp_do_strcmp103, %cmp_check_null102
  %strcmp_res105 = phi i1 [ %96, %cmp_check_null102 ], [ %99, %cmp_do_strcmp103 ]
  %100 = sext i1 %strcmp_res105 to i32
  %101 = icmp eq i32 %100, 0
  br i1 %101, label %if_then106, label %if_else107

if_then106:                                       ; preds = %cmp_merge104
  %fail_val109 = load i32, ptr @__global_fail, align 4
  %102 = add i32 %fail_val109, 1
  store i32 %102, ptr @__global_fail, align 4
  %dataBase110 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_words, i32 0, i32 2), align 8
  %103 = getelementptr inbounds ptr, ptr %dataBase110, i32 2
  %lst_elem111 = load ptr, ptr %103, align 8
  %104 = call ptr @__aiml_str_concat(ptr @46, ptr %lst_elem111)
  %print_null_check112 = icmp eq ptr %104, null
  %print_safe_str113 = select i1 %print_null_check112, ptr @print_null_str.12, ptr %104
  %print_call114 = call i32 (ptr, ...) @printf(ptr @47, ptr %print_safe_str113)
  br label %if_merge108

if_else107:                                       ; preds = %cmp_merge104
  %pass_val115 = load i32, ptr @__global_pass, align 4
  %105 = add i32 %pass_val115, 1
  store i32 %105, ptr @__global_pass, align 4
  br label %if_merge108

if_merge108:                                      ; preds = %if_else107, %if_then106
  store i32 0, ptr @__global_csv_parts, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_csv_parts, i32 0, i32 1), align 4
  %106 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %106, ptr getelementptr inbounds (%GenericList, ptr @__global_csv_parts, i32 0, i32 2), align 8
  %107 = load ptr, ptr @__global_csv, align 8
  %split_alloca116 = alloca %GenericList, align 8
  %sizePtr117 = getelementptr inbounds %GenericList, ptr %split_alloca116, i32 0, i32 0
  store i32 0, ptr %sizePtr117, align 4
  %capPtr118 = getelementptr inbounds %GenericList, ptr %split_alloca116, i32 0, i32 1
  store i32 8, ptr %capPtr118, align 4
  %108 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr119 = getelementptr inbounds %GenericList, ptr %split_alloca116, i32 0, i32 2
  store ptr %108, ptr %dataPtr119, align 8
  %109 = call i32 @aiml_str_split_fill(ptr %107, ptr @48, ptr %split_alloca116)
  %110 = call ptr @memcpy(ptr @__global_csv_parts, ptr %split_alloca116, i64 16)
  %sizeVal120 = load i32, ptr @__global_csv_parts, align 4
  %111 = icmp ne i32 %sizeVal120, 4
  br i1 %111, label %if_then121, label %if_else122

if_then121:                                       ; preds = %if_merge108
  %fail_val124 = load i32, ptr @__global_fail, align 4
  %112 = add i32 %fail_val124, 1
  store i32 %112, ptr @__global_fail, align 4
  %sizeVal125 = load i32, ptr @__global_csv_parts, align 4
  %113 = call ptr @__aiml_int_to_string(i32 %sizeVal125)
  %114 = call ptr @__aiml_str_concat(ptr @49, ptr %113)
  %print_null_check126 = icmp eq ptr %114, null
  %print_safe_str127 = select i1 %print_null_check126, ptr @print_null_str.13, ptr %114
  %print_call128 = call i32 (ptr, ...) @printf(ptr @50, ptr %print_safe_str127)
  br label %if_merge123

if_else122:                                       ; preds = %if_merge108
  %pass_val129 = load i32, ptr @__global_pass, align 4
  %115 = add i32 %pass_val129, 1
  store i32 %115, ptr @__global_pass, align 4
  br label %if_merge123

if_merge123:                                      ; preds = %if_else122, %if_then121
  %dataBase130 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_csv_parts, i32 0, i32 2), align 8
  %116 = getelementptr inbounds ptr, ptr %dataBase130, i32 3
  %lst_elem131 = load ptr, ptr %116, align 8
  %117 = icmp eq ptr %lst_elem131, null
  %118 = icmp eq ptr %lst_elem131, @51
  %119 = or i1 %117, false
  br i1 %119, label %cmp_check_null132, label %cmp_do_strcmp133

cmp_check_null132:                                ; preds = %if_merge123
  br label %cmp_merge134

cmp_do_strcmp133:                                 ; preds = %if_merge123
  %120 = call i32 @strcmp(ptr %lst_elem131, ptr @51)
  %121 = icmp eq i32 %120, 0
  br label %cmp_merge134

cmp_merge134:                                     ; preds = %cmp_do_strcmp133, %cmp_check_null132
  %strcmp_res135 = phi i1 [ %118, %cmp_check_null132 ], [ %121, %cmp_do_strcmp133 ]
  %122 = sext i1 %strcmp_res135 to i32
  %123 = icmp eq i32 %122, 0
  br i1 %123, label %if_then136, label %if_else137

if_then136:                                       ; preds = %cmp_merge134
  %fail_val139 = load i32, ptr @__global_fail, align 4
  %124 = add i32 %fail_val139, 1
  store i32 %124, ptr @__global_fail, align 4
  %dataBase140 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_csv_parts, i32 0, i32 2), align 8
  %125 = getelementptr inbounds ptr, ptr %dataBase140, i32 3
  %lst_elem141 = load ptr, ptr %125, align 8
  %126 = call ptr @__aiml_str_concat(ptr @52, ptr %lst_elem141)
  %print_null_check142 = icmp eq ptr %126, null
  %print_safe_str143 = select i1 %print_null_check142, ptr @print_null_str.14, ptr %126
  %print_call144 = call i32 (ptr, ...) @printf(ptr @53, ptr %print_safe_str143)
  br label %if_merge138

if_else137:                                       ; preds = %cmp_merge134
  %pass_val145 = load i32, ptr @__global_pass, align 4
  %127 = add i32 %pass_val145, 1
  store i32 %127, ptr @__global_pass, align 4
  br label %if_merge138

if_merge138:                                      ; preds = %if_else137, %if_then136
  store i32 0, ptr @__global_single_part, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_single_part, i32 0, i32 1), align 4
  %128 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %128, ptr getelementptr inbounds (%GenericList, ptr @__global_single_part, i32 0, i32 2), align 8
  %129 = load ptr, ptr @__global_trimmed, align 8
  %split_alloca146 = alloca %GenericList, align 8
  %sizePtr147 = getelementptr inbounds %GenericList, ptr %split_alloca146, i32 0, i32 0
  store i32 0, ptr %sizePtr147, align 4
  %capPtr148 = getelementptr inbounds %GenericList, ptr %split_alloca146, i32 0, i32 1
  store i32 8, ptr %capPtr148, align 4
  %130 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr149 = getelementptr inbounds %GenericList, ptr %split_alloca146, i32 0, i32 2
  store ptr %130, ptr %dataPtr149, align 8
  %131 = call i32 @aiml_str_split_fill(ptr %129, ptr @54, ptr %split_alloca146)
  %132 = call ptr @memcpy(ptr @__global_single_part, ptr %split_alloca146, i64 16)
  %sizeVal150 = load i32, ptr @__global_single_part, align 4
  %133 = icmp ne i32 %sizeVal150, 1
  br i1 %133, label %if_then151, label %if_else152

if_then151:                                       ; preds = %if_merge138
  %fail_val154 = load i32, ptr @__global_fail, align 4
  %134 = add i32 %fail_val154, 1
  store i32 %134, ptr @__global_fail, align 4
  %sizeVal155 = load i32, ptr @__global_single_part, align 4
  %135 = call ptr @__aiml_int_to_string(i32 %sizeVal155)
  %136 = call ptr @__aiml_str_concat(ptr @55, ptr %135)
  %print_null_check156 = icmp eq ptr %136, null
  %print_safe_str157 = select i1 %print_null_check156, ptr @print_null_str.15, ptr %136
  %print_call158 = call i32 (ptr, ...) @printf(ptr @56, ptr %print_safe_str157)
  br label %if_merge153

if_else152:                                       ; preds = %if_merge138
  %pass_val159 = load i32, ptr @__global_pass, align 4
  %137 = add i32 %pass_val159, 1
  store i32 %137, ptr @__global_pass, align 4
  br label %if_merge153

if_merge153:                                      ; preds = %if_else152, %if_then151
  %dataBase160 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_single_part, i32 0, i32 2), align 8
  %138 = getelementptr inbounds ptr, ptr %dataBase160, i32 0
  %lst_elem161 = load ptr, ptr %138, align 8
  %139 = icmp eq ptr %lst_elem161, null
  %140 = icmp eq ptr %lst_elem161, @57
  %141 = or i1 %139, false
  br i1 %141, label %cmp_check_null162, label %cmp_do_strcmp163

cmp_check_null162:                                ; preds = %if_merge153
  br label %cmp_merge164

cmp_do_strcmp163:                                 ; preds = %if_merge153
  %142 = call i32 @strcmp(ptr %lst_elem161, ptr @57)
  %143 = icmp eq i32 %142, 0
  br label %cmp_merge164

cmp_merge164:                                     ; preds = %cmp_do_strcmp163, %cmp_check_null162
  %strcmp_res165 = phi i1 [ %140, %cmp_check_null162 ], [ %143, %cmp_do_strcmp163 ]
  %144 = sext i1 %strcmp_res165 to i32
  %145 = icmp eq i32 %144, 0
  br i1 %145, label %if_then166, label %if_else167

if_then166:                                       ; preds = %cmp_merge164
  %fail_val169 = load i32, ptr @__global_fail, align 4
  %146 = add i32 %fail_val169, 1
  store i32 %146, ptr @__global_fail, align 4
  %dataBase170 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_single_part, i32 0, i32 2), align 8
  %147 = getelementptr inbounds ptr, ptr %dataBase170, i32 0
  %lst_elem171 = load ptr, ptr %147, align 8
  %148 = call ptr @__aiml_str_concat(ptr @58, ptr %lst_elem171)
  %print_null_check172 = icmp eq ptr %148, null
  %print_safe_str173 = select i1 %print_null_check172, ptr @print_null_str.16, ptr %148
  %print_call174 = call i32 (ptr, ...) @printf(ptr @59, ptr %print_safe_str173)
  br label %if_merge168

if_else167:                                       ; preds = %cmp_merge164
  %pass_val175 = load i32, ptr @__global_pass, align 4
  %149 = add i32 %pass_val175, 1
  store i32 %149, ptr @__global_pass, align 4
  br label %if_merge168

if_merge168:                                      ; preds = %if_else167, %if_then166
  %pass_val176 = load i32, ptr @__global_pass, align 4
  %150 = call ptr @__aiml_int_to_string(i32 %pass_val176)
  %151 = call ptr @__aiml_str_concat(ptr @60, ptr %150)
  %152 = call ptr @__aiml_str_concat(ptr %151, ptr @61)
  %fail_val177 = load i32, ptr @__global_fail, align 4
  %153 = call ptr @__aiml_int_to_string(i32 %fail_val177)
  %154 = call ptr @__aiml_str_concat(ptr %152, ptr %153)
  %155 = call ptr @__aiml_str_concat(ptr %154, ptr @62)
  %print_null_check178 = icmp eq ptr %155, null
  %print_safe_str179 = select i1 %print_null_check178, ptr @print_null_str.17, ptr %155
  %print_call180 = call i32 (ptr, ...) @printf(ptr @63, ptr %print_safe_str179)
  %fail_val181 = load i32, ptr @__global_fail, align 4
  %156 = icmp eq i32 %fail_val181, 0
  br i1 %156, label %if_then182, label %if_else183

if_then182:                                       ; preds = %if_merge168
  call void @exit(i32 0)
  br label %if_merge184

if_else183:                                       ; preds = %if_merge168
  call void @exit(i32 1)
  br label %if_merge184

if_merge184:                                      ; preds = %if_else183, %if_then182
  ret void
}

declare ptr @aiml_str_lower(ptr)

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

declare ptr @aiml_str_trim(ptr)

declare ptr @aiml_str_replace(ptr, ptr, ptr)

declare i32 @aiml_str_split_fill(ptr, ptr, ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare void @exit(i32)

attributes #0 = { noinline }
