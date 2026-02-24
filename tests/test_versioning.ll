; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%buffer = type { i32, i32, i32, i32, ptr }
%UserV1 = type { ptr }
%UserV2 = type { ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@1 = private unnamed_addr constant [9 x i8] c"no-email\00", align 1
@2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@3 = private unnamed_addr constant [7 x i8] c"Name: \00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [6 x i8] c"%s%s\0A\00", align 1
@5 = private unnamed_addr constant [8 x i8] c"Email: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [6 x i8] c"%s%s\0A\00", align 1
@7 = private unnamed_addr constant [9 x i8] c"no-email\00", align 1
@8 = private unnamed_addr constant [37 x i8] c"SUCCESS: Email skipped and defaulted\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [37 x i8] c"FAIL: Email was overwritten or wrong\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %buf = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %buf, align 8
  %0 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  store i32 0, ptr %0, align 4
  %1 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 1
  store i32 1024, ptr %1, align 4
  %2 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  store i32 0, ptr %2, align 4
  %3 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  store i32 0, ptr %3, align 4
  %4 = call ptr @malloc(i64 1024)
  %5 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  store ptr %4, ptr %5, align 8
  %u1 = alloca %UserV1, align 8
  store %UserV1 zeroinitializer, ptr %u1, align 8
  %set_prop_ptr = getelementptr inbounds %UserV1, ptr %u1, i32 0, i32 0
  store ptr @0, ptr %set_prop_ptr, align 8
  %u1_val = load %UserV1, ptr %u1, align 8
  %6 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  %7 = load i32, ptr %6, align 4
  %8 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %9 = load ptr, ptr %8, align 8
  %10 = getelementptr inbounds i8, ptr %9, i32 %7
  store i64 1, ptr %10, align 8
  %11 = add i32 %7, 8
  store i32 %11, ptr %6, align 4
  %12 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  %13 = load i32, ptr %12, align 4
  %14 = icmp sgt i32 %11, %13
  %15 = select i1 %14, i32 %11, i32 %13
  store i32 %15, ptr %12, align 4
  %16 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  %17 = load i32, ptr %16, align 4
  %18 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %19 = load ptr, ptr %18, align 8
  %20 = getelementptr inbounds i8, ptr %19, i32 %17
  store %UserV1 %u1_val, ptr %20, align 8
  %21 = add i32 %17, 8
  store i32 %21, ptr %16, align 4
  %22 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 0
  %23 = load i32, ptr %22, align 4
  %24 = icmp sgt i32 %21, %23
  %25 = select i1 %24, i32 %21, i32 %23
  store i32 %25, ptr %22, align 4
  %26 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 3
  store i64 0, ptr %26, align 8
  %27 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  store i64 0, ptr %27, align 8
  %u = alloca %UserV2, align 8
  store %UserV2 zeroinitializer, ptr %u, align 8
  %28 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  %29 = load i32, ptr %28, align 4
  %30 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %31 = load ptr, ptr %30, align 8
  %32 = getelementptr inbounds i8, ptr %31, i32 %29
  %33 = load i64, ptr %32, align 8
  %34 = add i32 %29, 8
  store i32 %34, ptr %28, align 4
  %read_obj = alloca %UserV2, align 8
  %35 = getelementptr inbounds %UserV2, ptr %read_obj, i32 0, i32 1
  store ptr @1, ptr %35, align 8
  %36 = getelementptr inbounds %UserV2, ptr %read_obj, i32 0, i32 0
  store ptr @2, ptr %36, align 8
  %37 = icmp uge i64 %33, 1
  br i1 %37, label %ver_read, label %ver_skip

ver_read:                                         ; preds = %entry
  %38 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  %39 = load i32, ptr %38, align 4
  %40 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %41 = load ptr, ptr %40, align 8
  %42 = getelementptr inbounds i8, ptr %41, i32 %39
  %43 = load ptr, ptr %42, align 8
  %44 = add i32 %39, 8
  store i32 %44, ptr %38, align 4
  %45 = getelementptr inbounds %UserV2, ptr %read_obj, i32 0, i32 0
  store ptr %43, ptr %45, align 8
  br label %ver_skip

ver_skip:                                         ; preds = %ver_read, %entry
  %46 = icmp uge i64 %33, 2
  br i1 %46, label %ver_read1, label %ver_skip2

ver_read1:                                        ; preds = %ver_skip
  %47 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 2
  %48 = load i32, ptr %47, align 4
  %49 = getelementptr inbounds %buffer, ptr %buf, i32 0, i32 4
  %50 = load ptr, ptr %49, align 8
  %51 = getelementptr inbounds i8, ptr %50, i32 %48
  %52 = load ptr, ptr %51, align 8
  %53 = add i32 %48, 8
  store i32 %53, ptr %47, align 4
  %54 = getelementptr inbounds %UserV2, ptr %read_obj, i32 0, i32 1
  store ptr %52, ptr %54, align 8
  br label %ver_skip2

ver_skip2:                                        ; preds = %ver_read1, %ver_skip
  %read_res = load %UserV2, ptr %read_obj, align 8
  store %UserV2 %read_res, ptr %u, align 8
  %prop_ptr = getelementptr inbounds %UserV2, ptr %u, i32 0, i32 0
  %name_val = load ptr, ptr %prop_ptr, align 8
  %print_null_check = icmp eq ptr %name_val, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.1, ptr %name_val
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr @3, ptr %print_safe_str)
  %prop_ptr3 = getelementptr inbounds %UserV2, ptr %u, i32 0, i32 1
  %email_val = load ptr, ptr %prop_ptr3, align 8
  %print_null_check4 = icmp eq ptr %email_val, null
  %print_safe_str5 = select i1 %print_null_check4, ptr @print_null_str.3, ptr %email_val
  %print_call6 = call i32 (ptr, ...) @printf(ptr @6, ptr @5, ptr %print_safe_str5)
  %prop_ptr7 = getelementptr inbounds %UserV2, ptr %u, i32 0, i32 1
  %email_val8 = load ptr, ptr %prop_ptr7, align 8
  %55 = icmp eq ptr %email_val8, null
  %56 = icmp eq ptr %email_val8, @7
  %57 = or i1 %55, false
  br i1 %57, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %ver_skip2
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %ver_skip2
  %58 = call i32 @strcmp(ptr %email_val8, ptr @7)
  %59 = icmp eq i32 %58, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %streq_res = phi i1 [ %56, %cmp_check_null ], [ %59, %cmp_do_strcmp ]
  br i1 %streq_res, label %if_then, label %if_else

if_then:                                          ; preds = %cmp_merge
  %print_call9 = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  br label %if_merge

if_else:                                          ; preds = %cmp_merge
  %print_call10 = call i32 (ptr, ...) @printf(ptr @11, ptr @10)
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  ret void
}

declare ptr @malloc(i64)

declare i32 @printf(ptr, ...)

declare i32 @strcmp(ptr, ptr)
