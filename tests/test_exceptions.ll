; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%DivisionByZeroException = type { ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [27 x i8] c"Starting Exception Test...\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [28 x i8] c"Inside Try - About to throw\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [24 x i8] c"Division by Zero Error!\00", align 1
@5 = private unnamed_addr constant [27 x i8] c"This should NOT be printed\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [18 x i8] c"Caught Exception!\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [10 x i8] c"Message: \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() personality ptr @__gxx_personality_v0 {
entry:
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0)
  br label %try_block

try_block:                                        ; preds = %entry
  %print_call1 = call i32 (ptr, ...) @printf(ptr @3, ptr @2)
  %calloc_call = call ptr @calloc(i64 1, i64 8)
  %message_gep = getelementptr inbounds %DivisionByZeroException, ptr %calloc_call, i32 0, i32 0
  store ptr @4, ptr %message_gep, align 8
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr %calloc_call, ptr %exc_alloc, align 8
  invoke void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
          to label %throw_unreachable unwind label %catch_block

catch_block:                                      ; preds = %try_block
  %lpad = landingpad { ptr, i32 }
          catch ptr null
  %exc_ptr = extractvalue { ptr, i32 } %lpad, 0
  %raw_exc = call ptr @__cxa_begin_catch(ptr %exc_ptr)
  %actual_exc_ptr = load ptr, ptr %raw_exc, align 8
  %e = alloca ptr, align 8
  store ptr %actual_exc_ptr, ptr %e, align 8
  %print_call3 = call i32 (ptr, ...) @printf(ptr @8, ptr @7)
  %prop_ptr_base = load ptr, ptr %e, align 8
  %prop_ptr = getelementptr inbounds %DivisionByZeroException, ptr %prop_ptr_base, i32 0, i32 0
  %message_val = load ptr, ptr %prop_ptr, align 8
  %0 = call ptr @__aiml_str_concat(ptr @9, ptr %message_val)
  %print_null_check = icmp eq ptr %0, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.4, ptr %0
  %print_call4 = call i32 (ptr, ...) @printf(ptr @10, ptr %print_safe_str)
  call void @__cxa_end_catch()
  br label %try_cont

try_cont:                                         ; preds = %catch_block, %throw_unreachable
  ret void

throw_unreachable:                                ; preds = %try_block
  unreachable
  %print_call2 = call i32 (ptr, ...) @printf(ptr @6, ptr @5)
  br label %try_cont
}

declare i32 @printf(ptr, ...)

declare ptr @calloc(i64, i64)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @__gxx_personality_v0(...)

declare ptr @__cxa_begin_catch(ptr)

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

declare void @__cxa_end_catch()

attributes #0 = { noinline }
