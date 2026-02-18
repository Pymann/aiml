; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

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
@9 = private unnamed_addr constant [32 x i8] c"Program recovered and finished.\00", align 1
@fmt_str.4 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@10 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr @0)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @1)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr @2)
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @3)
  %ex_obj = call ptr @__cxa_allocate_exception(i64 8)
  store ptr @4, ptr %ex_obj, align 8
  invoke void @__cxa_throw(ptr %ex_obj, ptr null, ptr null)
          to label %throw_cont unwind label %catch_block

try_cont:                                         ; No predecessors!

catch_block:                                      ; preds = %entry
  %4 = landingpad { ptr, i32 }
          cleanup
          catch ptr null
  %5 = extractvalue { ptr, i32 } %4, 0
  %6 = call ptr @__cxa_begin_catch(ptr %5)
  %ex_msg = load ptr, ptr %6, align 8
  %7 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr @7)
  %8 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @8)
  call void @__cxa_end_catch()
  br label %try_end

throw_cont:                                       ; preds = %entry
  unreachable
  %9 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.2, ptr @5)
  %10 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @6)
  br label %try_end

try_end:                                          ; preds = %catch_block, %throw_cont
  %11 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.4, ptr @9)
  %12 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @10)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @dprintf(i32, ptr, ...)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare ptr @__cxa_begin_catch(ptr)

declare void @__cxa_end_catch()
