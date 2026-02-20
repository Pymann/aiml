; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%DivisionByZeroException = type { ptr }
%DivisionByZeroException.0 = type { ptr }

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [27 x i8] c"Starting Exception Test...\00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@2 = private unnamed_addr constant [28 x i8] c"Inside Try - About to throw\00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@3 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@4 = private unnamed_addr constant [24 x i8] c"Division by Zero Error!\00", align 1
@5 = private unnamed_addr constant [27 x i8] c"This should NOT be printed\00", align 1
@fmt_str.2 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@6 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@7 = private unnamed_addr constant [18 x i8] c"Caught Exception!\00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@8 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@9 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@10 = private unnamed_addr constant [10 x i8] c"Message: \00", align 1
@fmt_str.4 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@11 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@12 = private unnamed_addr constant [32 x i8] c"Program recovered and finished.\00", align 1
@fmt_str.5 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@13 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@14 = private unnamed_addr constant [27 x i8] c"Starting Exception Test...\00", align 1
@fmt_str.7 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@15 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@16 = private unnamed_addr constant [28 x i8] c"Inside Try - About to throw\00", align 1
@fmt_str.8 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@17 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@18 = private unnamed_addr constant [24 x i8] c"Division by Zero Error!\00", align 1
@19 = private unnamed_addr constant [27 x i8] c"This should NOT be printed\00", align 1
@fmt_str.9 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@20 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@21 = private unnamed_addr constant [18 x i8] c"Caught Exception!\00", align 1
@fmt_str.10 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@22 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@23 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@24 = private unnamed_addr constant [10 x i8] c"Message: \00", align 1
@25 = private unnamed_addr constant [10 x i8] c"e.message\00", align 1
@fmt_str.11 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@26 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@27 = private unnamed_addr constant [32 x i8] c"Program recovered and finished.\00", align 1
@fmt_str.12 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@28 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define internal void @__aiml_init() personality ptr @__gxx_personality_v0 {
entry:
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr @0)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @1)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr @2)
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @3)
  %ex_obj = call ptr @__cxa_allocate_exception(i64 8)
  store ptr @4, ptr %ex_obj, align 8
  call void @__cxa_throw(ptr %ex_obj, ptr null, ptr null)
  unreachable
  %4 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.2, ptr @5)
  %5 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @6)
  br label %try_end

try_cont:                                         ; No predecessors!

catch_block:                                      ; No predecessors!
  %lp = landingpad { ptr, i32 }
          cleanup
          catch ptr null
  %ex_obj1 = extractvalue { ptr, i32 } %lp, 0
  %adjusted_ex = call ptr @__cxa_begin_catch(ptr %ex_obj1)
  %ex_msg = load ptr, ptr %adjusted_ex, align 8
  %e_alloc = alloca %DivisionByZeroException, align 8
  %msg_dest = getelementptr inbounds %DivisionByZeroException, ptr %e_alloc, i32 0, i32 0
  store ptr %ex_msg, ptr %msg_dest, align 8
  %6 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr @7)
  %7 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @8)
  %8 = call i64 @strlen(ptr @9)
  %9 = call i64 @strlen(ptr @10)
  %10 = add i64 %8, %9
  %11 = add i64 %10, 1
  %12 = call ptr @malloc(i64 %11)
  %13 = call ptr @strcpy(ptr %12, ptr @9)
  %14 = call ptr @strcat(ptr %12, ptr @10)
  %member_addr = getelementptr inbounds %DivisionByZeroException, ptr %e_alloc, i32 0, i32 0
  %message_val = load ptr, ptr %member_addr, align 8
  %15 = call i64 @strlen(ptr %12)
  %16 = call i64 @strlen(ptr %message_val)
  %17 = add i64 %15, %16
  %18 = add i64 %17, 1
  %19 = call ptr @malloc(i64 %18)
  %20 = call ptr @strcpy(ptr %19, ptr %12)
  %21 = call ptr @strcat(ptr %19, ptr %message_val)
  %22 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.4, ptr %19)
  %23 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @11)
  %end_catch = call void @__cxa_end_catch()
  br label %try_end

try_end:                                          ; preds = %catch_block, %entry
  %24 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.5, ptr @12)
  %25 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @13)
  ret void
}

declare i32 @dprintf(i32, ptr, ...)

declare i32 @__gxx_personality_v0(...)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare ptr @__cxa_begin_catch(ptr)

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare void @__cxa_end_catch()

define i32 @main(i32 %argc, ptr %argv) {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  call void @__aiml_init()
  ret i32 0
}

define internal void @__aiml_init.6() personality ptr @__gxx_personality_v0 {
entry:
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.7, ptr @14)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @15)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.8, ptr @16)
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @17)
  %ex_obj = call ptr @__cxa_allocate_exception(i64 8)
  store ptr @18, ptr %ex_obj, align 8
  call void @__cxa_throw(ptr %ex_obj, ptr null, ptr null)
  unreachable
  %4 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.9, ptr @19)
  %5 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @20)
  br label %try_end

try_cont:                                         ; No predecessors!

catch_block:                                      ; No predecessors!
  %lp = landingpad { ptr, i32 }
          cleanup
          catch ptr null
  %ex_obj1 = extractvalue { ptr, i32 } %lp, 0
  %adjusted_ex = call ptr @__cxa_begin_catch(ptr %ex_obj1)
  %ex_msg = load ptr, ptr %adjusted_ex, align 8
  %e_alloc = alloca %DivisionByZeroException.0, align 8
  %msg_dest = getelementptr inbounds %DivisionByZeroException.0, ptr %e_alloc, i32 0, i32 0
  store ptr %ex_msg, ptr %msg_dest, align 8
  %6 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.10, ptr @21)
  %7 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @22)
  %8 = call i64 @strlen(ptr @23)
  %9 = call i64 @strlen(ptr @24)
  %10 = add i64 %8, %9
  %11 = add i64 %10, 1
  %12 = call ptr @malloc(i64 %11)
  %13 = call ptr @strcpy(ptr %12, ptr @23)
  %14 = call ptr @strcat(ptr %12, ptr @24)
  %15 = call i64 @strlen(ptr %12)
  %16 = call i64 @strlen(ptr @25)
  %17 = add i64 %15, %16
  %18 = add i64 %17, 1
  %19 = call ptr @malloc(i64 %18)
  %20 = call ptr @strcpy(ptr %19, ptr %12)
  %21 = call ptr @strcat(ptr %19, ptr @25)
  %22 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.11, ptr %19)
  %23 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @26)
  %end_catch = call void @__cxa_end_catch()
  br label %try_end

try_end:                                          ; preds = %catch_block, %entry
  %24 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.12, ptr @27)
  %25 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @28)
  ret void
}
