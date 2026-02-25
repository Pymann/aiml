; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%thread_handle = type { i64, i8, i8 }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [8 x i8] c"Worker \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@1 = private unnamed_addr constant [10 x i8] c": started\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [8 x i8] c"Worker \00", align 1
@4 = private unnamed_addr constant [7 x i8] c": done\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [22 x i8] c"Main: threads spawned\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [21 x i8] c"Main: threads joined\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [29 x i8] c"ERROR: should not reach here\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [50 x i8] c"Caught InvalidThreadStateException on double-join\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [26 x i8] c"Main: in critical section\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [11 x i8] c"Main: done\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() personality ptr @__gxx_personality_v0 {
entry:
  %t1 = alloca %thread_handle, align 8
  store %thread_handle zeroinitializer, ptr %t1, align 8
  %t2 = alloca %thread_handle, align 8
  store %thread_handle zeroinitializer, ptr %t2, align 8
  %args_raw = call ptr @malloc(i64 4)
  %0 = getelementptr inbounds { i32 }, ptr %args_raw, i32 0, i32 0
  store i32 1, ptr %0, align 4
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %1 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_worker, ptr %args_raw)
  store ptr %handle_raw, ptr %t1, align 8
  %args_raw2 = call ptr @malloc(i64 4)
  %2 = getelementptr inbounds { i32 }, ptr %args_raw2, i32 0, i32 0
  store i32 2, ptr %2, align 4
  %handle_raw3 = call ptr @malloc(i64 16)
  %joined_ptr4 = getelementptr inbounds %thread_handle, ptr %handle_raw3, i32 0, i32 1
  store i8 0, ptr %joined_ptr4, align 1
  %detached_ptr5 = getelementptr inbounds %thread_handle, ptr %handle_raw3, i32 0, i32 2
  store i8 0, ptr %detached_ptr5, align 1
  %tid_gep6 = getelementptr inbounds %thread_handle, ptr %handle_raw3, i32 0, i32 0
  %3 = call i32 @pthread_create(ptr %tid_gep6, ptr null, ptr @__aiml_spawn_worker, ptr %args_raw2)
  store ptr %handle_raw3, ptr %t2, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @7, ptr @6)
  %handle_ptr = load ptr, ptr %t1, align 8
  %detached_ptr7 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr7, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

entry1:                                           ; No predecessors!

join_detached_throw:                              ; preds = %entry
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %entry
  %joined_ptr8 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr8, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc9 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc9, align 8
  call void @__cxa_throw(ptr %exc_alloc9, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %4 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr8, align 1
  %handle_ptr10 = load ptr, ptr %t2, align 8
  %detached_ptr11 = getelementptr inbounds %thread_handle, ptr %handle_ptr10, i32 0, i32 2
  %detached12 = load i8, ptr %detached_ptr11, align 1
  %join_detached_throw_cmp13 = icmp ne i8 %detached12, 0
  br i1 %join_detached_throw_cmp13, label %join_detached_throw14, label %join_check_joined15

join_detached_throw14:                            ; preds = %join_do
  %exc_alloc16 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc16, align 8
  call void @__cxa_throw(ptr %exc_alloc16, ptr null, ptr null)
  unreachable

join_check_joined15:                              ; preds = %join_do
  %joined_ptr17 = getelementptr inbounds %thread_handle, ptr %handle_ptr10, i32 0, i32 1
  %joined18 = load i8, ptr %joined_ptr17, align 1
  %join_already_throw_cmp19 = icmp ne i8 %joined18, 0
  br i1 %join_already_throw_cmp19, label %join_already_throw20, label %join_do21

join_already_throw20:                             ; preds = %join_check_joined15
  %exc_alloc22 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc22, align 8
  call void @__cxa_throw(ptr %exc_alloc22, ptr null, ptr null)
  unreachable

join_do21:                                        ; preds = %join_check_joined15
  %tid_ptr23 = getelementptr inbounds %thread_handle, ptr %handle_ptr10, i32 0, i32 0
  %tid24 = load i64, ptr %tid_ptr23, align 8
  %5 = call i32 @pthread_join(i64 %tid24, ptr null)
  store i8 1, ptr %joined_ptr17, align 1
  %print_call25 = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  br label %try_block

try_block:                                        ; preds = %join_do21
  %handle_ptr26 = load ptr, ptr %t1, align 8
  %detached_ptr27 = getelementptr inbounds %thread_handle, ptr %handle_ptr26, i32 0, i32 2
  %detached28 = load i8, ptr %detached_ptr27, align 1
  %join_detached_throw_cmp29 = icmp ne i8 %detached28, 0
  br i1 %join_detached_throw_cmp29, label %join_detached_throw30, label %join_check_joined31

catch_block:                                      ; preds = %join_already_throw36, %join_detached_throw30
  %lpad = landingpad { ptr, i32 }
          catch ptr null
  %exc_ptr = extractvalue { ptr, i32 } %lpad, 0
  %raw_exc = call ptr @__cxa_begin_catch(ptr %exc_ptr)
  %actual_exc_ptr = load ptr, ptr %raw_exc, align 8
  %e = alloca ptr, align 8
  store ptr %actual_exc_ptr, ptr %e, align 8
  %print_call43 = call i32 (ptr, ...) @printf(ptr @13, ptr @12)
  call void @__cxa_end_catch()
  br label %try_cont

try_cont:                                         ; preds = %catch_block, %join_do37
  %mutex = alloca ptr, align 8
  store ptr null, ptr %mutex, align 8
  %mutex_mem = call ptr @malloc(i64 40)
  store ptr %mutex_mem, ptr %mutex, align 8
  %6 = call i32 @pthread_mutex_init(ptr %mutex_mem, ptr null)
  %mutex_ptr = load ptr, ptr %mutex, align 8
  %7 = call i32 @pthread_mutex_lock(ptr %mutex_ptr)
  %print_call44 = call i32 (ptr, ...) @printf(ptr @15, ptr @14)
  %mutex_ptr45 = load ptr, ptr %mutex, align 8
  %8 = call i32 @pthread_mutex_unlock(ptr %mutex_ptr45)
  %print_call46 = call i32 (ptr, ...) @printf(ptr @17, ptr @16)
  ret void

join_detached_throw30:                            ; preds = %try_block
  %exc_alloc32 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc32, align 8
  invoke void @__cxa_throw(ptr %exc_alloc32, ptr null, ptr null)
          to label %throw_unreach unwind label %catch_block

join_check_joined31:                              ; preds = %try_block
  %joined_ptr33 = getelementptr inbounds %thread_handle, ptr %handle_ptr26, i32 0, i32 1
  %joined34 = load i8, ptr %joined_ptr33, align 1
  %join_already_throw_cmp35 = icmp ne i8 %joined34, 0
  br i1 %join_already_throw_cmp35, label %join_already_throw36, label %join_do37

throw_unreach:                                    ; preds = %join_detached_throw30
  unreachable

join_already_throw36:                             ; preds = %join_check_joined31
  %exc_alloc38 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc38, align 8
  invoke void @__cxa_throw(ptr %exc_alloc38, ptr null, ptr null)
          to label %throw_unreach39 unwind label %catch_block

join_do37:                                        ; preds = %join_check_joined31
  %tid_ptr40 = getelementptr inbounds %thread_handle, ptr %handle_ptr26, i32 0, i32 0
  %tid41 = load i64, ptr %tid_ptr40, align 8
  %9 = call i32 @pthread_join(i64 %tid41, ptr null)
  store i8 1, ptr %joined_ptr33, align 1
  %print_call42 = call i32 (ptr, ...) @printf(ptr @11, ptr @10)
  br label %try_cont

throw_unreach39:                                  ; preds = %join_already_throw36
  unreachable
}

define void @worker(i32 %0) personality ptr @__gxx_personality_v0 {
entry:
  %id = alloca i32, align 4
  store i32 %0, ptr %id, align 4
  %id_val = load i32, ptr %id, align 4
  %1 = call ptr @__aiml_int_to_string(i32 %id_val)
  %2 = call ptr @__aiml_str_concat(ptr @0, ptr %1)
  %3 = call ptr @__aiml_str_concat(ptr %2, ptr @1)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %id_val1 = load i32, ptr %id, align 4
  %4 = call ptr @__aiml_int_to_string(i32 %id_val1)
  %5 = call ptr @__aiml_str_concat(ptr @3, ptr %4)
  %6 = call ptr @__aiml_str_concat(ptr %5, ptr @4)
  %print_null_check2 = icmp eq ptr %6, null
  %print_safe_str3 = select i1 %print_null_check2, ptr @print_null_str.1, ptr %6
  %print_call4 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str3)
  ret void
}

declare i32 @__gxx_personality_v0(...)

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

define internal ptr @__aiml_spawn_worker(ptr %0) {
entry:
  %1 = getelementptr inbounds { i32 }, ptr %0, i32 0, i32 0
  %2 = load i32, ptr %1, align 4
  call void @worker(i32 %2)
  call void @free(ptr %0)
  ret ptr null
}

declare void @free(ptr)

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

declare ptr @__cxa_begin_catch(ptr)

declare void @__cxa_end_catch()

declare i32 @pthread_mutex_init(ptr, ptr)

declare i32 @pthread_mutex_lock(ptr)

declare i32 @pthread_mutex_unlock(ptr)

attributes #0 = { noinline }
