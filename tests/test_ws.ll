; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%thread_handle = type { i64, i8, i8 }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [10 x i8] c"127.0.0.1\00", align 1
@1 = private unnamed_addr constant [31 x i8] c"ws server fd (should be > 0): \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [15 x i8] c"lifecycle done\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [10 x i8] c"127.0.0.1\00", align 1
@6 = private unnamed_addr constant [16 x i8] c"ws server got: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [8 x i8] c"ws_pong\00", align 1
@9 = private unnamed_addr constant [10 x i8] c"127.0.0.1\00", align 1
@10 = private unnamed_addr constant [6 x i8] c"/chat\00", align 1
@11 = private unnamed_addr constant [8 x i8] c"ws_ping\00", align 1
@12 = private unnamed_addr constant [16 x i8] c"ws client got: \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [27 x i8] c"tests/tls_certs/server.crt\00", align 1
@15 = private unnamed_addr constant [27 x i8] c"tests/tls_certs/server.key\00", align 1
@16 = private unnamed_addr constant [10 x i8] c"127.0.0.1\00", align 1
@17 = private unnamed_addr constant [17 x i8] c"wss server got: \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [9 x i8] c"wss_pong\00", align 1
@20 = private unnamed_addr constant [10 x i8] c"127.0.0.1\00", align 1
@21 = private unnamed_addr constant [2 x i8] c"/\00", align 1
@22 = private unnamed_addr constant [9 x i8] c"wss_ping\00", align 1
@23 = private unnamed_addr constant [17 x i8] c"wss client got: \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [14 x i8] c"ws tests done\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [2 x [16 x ptr]] [[16 x ptr] [ptr @ws_echo_server, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @wss_echo_server, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [2 x ptr] [ptr @ws_echo_server, ptr @wss_echo_server]
@__aiml_fptable_versions = global [2 x i32] [i32 1, i32 1]
@__aiml_fname_ws_echo_server = private constant [15 x i8] c"ws_echo_server\00"
@__aiml_fname_wss_echo_server = private constant [16 x i8] c"wss_echo_server\00"
@__aiml_func_names = constant [2 x ptr] [ptr @__aiml_fname_ws_echo_server, ptr @__aiml_fname_wss_echo_server]
@__aiml_func_count = constant i32 2
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %srv = alloca ptr, align 8
  store ptr null, ptr %srv, align 8
  %0 = call ptr @aiml_ws_server(ptr @0, i32 19890)
  store ptr %0, ptr %srv, align 8
  %fd = alloca i32, align 4
  store i32 0, ptr %fd, align 4
  %srv_val = load ptr, ptr %srv, align 8
  %1 = call i32 @aiml_ws_fd(ptr %srv_val)
  store i32 %1, ptr %fd, align 4
  %fd_val = load i32, ptr %fd, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %fd_val)
  %3 = call ptr @__aiml_str_concat(ptr @1, ptr %2)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %srv_val1 = load ptr, ptr %srv, align 8
  %4 = call i32 @aiml_ws_close(ptr %srv_val1)
  %print_call2 = call i32 (ptr, ...) @printf(ptr @4, ptr @3)
  %t = alloca %thread_handle, align 8
  store %thread_handle zeroinitializer, ptr %t, align 8
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %5 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_ws_echo_server, ptr null)
  store ptr %handle_raw, ptr %t, align 8
  call void @time_sleep_ms(i32 150)
  %c = alloca ptr, align 8
  store ptr null, ptr %c, align 8
  %6 = call ptr @aiml_ws_connect(ptr @9, i32 19891, ptr @10)
  store ptr %6, ptr %c, align 8
  %c_val = load ptr, ptr %c, align 8
  %7 = call i32 @aiml_ws_send(ptr %c_val, ptr @11)
  %resp = alloca ptr, align 8
  store ptr null, ptr %resp, align 8
  %c_val4 = load ptr, ptr %c, align 8
  %8 = call ptr @aiml_ws_recv(ptr %c_val4)
  store ptr %8, ptr %resp, align 8
  %resp_val = load ptr, ptr %resp, align 8
  %9 = call ptr @__aiml_str_concat(ptr @12, ptr %resp_val)
  %print_null_check5 = icmp eq ptr %9, null
  %print_safe_str6 = select i1 %print_null_check5, ptr @print_null_str.3, ptr %9
  %print_call7 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str6)
  %c_val8 = load ptr, ptr %c, align 8
  %10 = call i32 @aiml_ws_close(ptr %c_val8)
  %handle_ptr = load ptr, ptr %t, align 8
  %detached_ptr9 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr9, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

entry3:                                           ; No predecessors!

join_detached_throw:                              ; preds = %entry
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %entry
  %joined_ptr10 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr10, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc11 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc11, align 8
  call void @__cxa_throw(ptr %exc_alloc11, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %11 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr10, align 1
  %t2 = alloca %thread_handle, align 8
  store %thread_handle zeroinitializer, ptr %t2, align 8
  %handle_raw13 = call ptr @malloc(i64 16)
  %joined_ptr14 = getelementptr inbounds %thread_handle, ptr %handle_raw13, i32 0, i32 1
  store i8 0, ptr %joined_ptr14, align 1
  %detached_ptr15 = getelementptr inbounds %thread_handle, ptr %handle_raw13, i32 0, i32 2
  store i8 0, ptr %detached_ptr15, align 1
  %tid_gep16 = getelementptr inbounds %thread_handle, ptr %handle_raw13, i32 0, i32 0
  %12 = call i32 @pthread_create(ptr %tid_gep16, ptr null, ptr @__aiml_spawn_wss_echo_server, ptr null)
  store ptr %handle_raw13, ptr %t2, align 8
  call void @time_sleep_ms(i32 200)
  %c2 = alloca ptr, align 8
  store ptr null, ptr %c2, align 8
  %13 = call ptr @aiml_wss_connect(ptr @20, i32 19892, ptr @21)
  store ptr %13, ptr %c2, align 8
  %c2_val = load ptr, ptr %c2, align 8
  %14 = call i32 @aiml_ws_send(ptr %c2_val, ptr @22)
  %resp2 = alloca ptr, align 8
  store ptr null, ptr %resp2, align 8
  %c2_val17 = load ptr, ptr %c2, align 8
  %15 = call ptr @aiml_ws_recv(ptr %c2_val17)
  store ptr %15, ptr %resp2, align 8
  %resp2_val = load ptr, ptr %resp2, align 8
  %16 = call ptr @__aiml_str_concat(ptr @23, ptr %resp2_val)
  %print_null_check18 = icmp eq ptr %16, null
  %print_safe_str19 = select i1 %print_null_check18, ptr @print_null_str.5, ptr %16
  %print_call20 = call i32 (ptr, ...) @printf(ptr @24, ptr %print_safe_str19)
  %c2_val21 = load ptr, ptr %c2, align 8
  %17 = call i32 @aiml_ws_close(ptr %c2_val21)
  %handle_ptr22 = load ptr, ptr %t2, align 8
  %detached_ptr23 = getelementptr inbounds %thread_handle, ptr %handle_ptr22, i32 0, i32 2
  %detached24 = load i8, ptr %detached_ptr23, align 1
  %join_detached_throw_cmp25 = icmp ne i8 %detached24, 0
  br i1 %join_detached_throw_cmp25, label %join_detached_throw26, label %join_check_joined27

entry12:                                          ; No predecessors!

join_detached_throw26:                            ; preds = %join_do
  %exc_alloc28 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc28, align 8
  call void @__cxa_throw(ptr %exc_alloc28, ptr null, ptr null)
  unreachable

join_check_joined27:                              ; preds = %join_do
  %joined_ptr29 = getelementptr inbounds %thread_handle, ptr %handle_ptr22, i32 0, i32 1
  %joined30 = load i8, ptr %joined_ptr29, align 1
  %join_already_throw_cmp31 = icmp ne i8 %joined30, 0
  br i1 %join_already_throw_cmp31, label %join_already_throw32, label %join_do33

join_already_throw32:                             ; preds = %join_check_joined27
  %exc_alloc34 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc34, align 8
  call void @__cxa_throw(ptr %exc_alloc34, ptr null, ptr null)
  unreachable

join_do33:                                        ; preds = %join_check_joined27
  %tid_ptr35 = getelementptr inbounds %thread_handle, ptr %handle_ptr22, i32 0, i32 0
  %tid36 = load i64, ptr %tid_ptr35, align 8
  %18 = call i32 @pthread_join(i64 %tid36, ptr null)
  store i8 1, ptr %joined_ptr29, align 1
  %print_call37 = call i32 (ptr, ...) @printf(ptr @26, ptr @25)
  ret void
}

declare ptr @aiml_ws_server(ptr, i32)

declare i32 @aiml_ws_fd(ptr)

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

declare i32 @aiml_ws_close(ptr)

define void @ws_echo_server() personality ptr @__gxx_personality_v0 {
entry:
  %srv = alloca ptr, align 8
  store ptr null, ptr %srv, align 8
  %0 = call ptr @aiml_ws_server(ptr @5, i32 19891)
  store ptr %0, ptr %srv, align 8
  %client = alloca ptr, align 8
  store ptr null, ptr %client, align 8
  %srv_val = load ptr, ptr %srv, align 8
  %1 = call ptr @aiml_ws_accept(ptr %srv_val)
  store ptr %1, ptr %client, align 8
  %msg = alloca ptr, align 8
  store ptr null, ptr %msg, align 8
  %client_val = load ptr, ptr %client, align 8
  %2 = call ptr @aiml_ws_recv(ptr %client_val)
  store ptr %2, ptr %msg, align 8
  %msg_val = load ptr, ptr %msg, align 8
  %3 = call ptr @__aiml_str_concat(ptr @6, ptr %msg_val)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.2, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str)
  %client_val1 = load ptr, ptr %client, align 8
  %4 = call i32 @aiml_ws_send(ptr %client_val1, ptr @8)
  %client_val2 = load ptr, ptr %client, align 8
  %5 = call i32 @aiml_ws_close(ptr %client_val2)
  %srv_val3 = load ptr, ptr %srv, align 8
  %6 = call i32 @aiml_ws_close(ptr %srv_val3)
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @aiml_ws_accept(ptr)

declare ptr @aiml_ws_recv(ptr)

declare i32 @aiml_ws_send(ptr, ptr)

define internal ptr @__aiml_spawn_ws_echo_server(ptr %0) {
entry:
  call void @ws_echo_server()
  ret ptr null
}

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

declare void @time_sleep_ms(i32)

declare ptr @aiml_ws_connect(ptr, i32, ptr)

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

define void @wss_echo_server() personality ptr @__gxx_personality_v0 {
entry:
  %cert = alloca ptr, align 8
  store ptr null, ptr %cert, align 8
  %path_interned = call ptr @aiml_path_intern(ptr @14)
  store ptr %path_interned, ptr %cert, align 8
  %key = alloca ptr, align 8
  store ptr null, ptr %key, align 8
  %path_interned1 = call ptr @aiml_path_intern(ptr @15)
  store ptr %path_interned1, ptr %key, align 8
  %srv = alloca ptr, align 8
  store ptr null, ptr %srv, align 8
  %cert_val = load ptr, ptr %cert, align 8
  %key_val = load ptr, ptr %key, align 8
  %0 = call ptr @aiml_wss_server(ptr @16, i32 19892, ptr %cert_val, ptr %key_val)
  store ptr %0, ptr %srv, align 8
  %client = alloca ptr, align 8
  store ptr null, ptr %client, align 8
  %srv_val = load ptr, ptr %srv, align 8
  %1 = call ptr @aiml_ws_accept(ptr %srv_val)
  store ptr %1, ptr %client, align 8
  %msg = alloca ptr, align 8
  store ptr null, ptr %msg, align 8
  %client_val = load ptr, ptr %client, align 8
  %2 = call ptr @aiml_ws_recv(ptr %client_val)
  store ptr %2, ptr %msg, align 8
  %msg_val = load ptr, ptr %msg, align 8
  %3 = call ptr @__aiml_str_concat(ptr @17, ptr %msg_val)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.4, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str)
  %client_val2 = load ptr, ptr %client, align 8
  %4 = call i32 @aiml_ws_send(ptr %client_val2, ptr @19)
  %client_val3 = load ptr, ptr %client, align 8
  %5 = call i32 @aiml_ws_close(ptr %client_val3)
  %srv_val4 = load ptr, ptr %srv, align 8
  %6 = call i32 @aiml_ws_close(ptr %srv_val4)
  ret void
}

declare ptr @aiml_path_intern(ptr)

declare ptr @aiml_wss_server(ptr, i32, ptr, ptr)

define internal ptr @__aiml_spawn_wss_echo_server(ptr %0) {
entry:
  call void @wss_echo_server()
  ret ptr null
}

declare ptr @aiml_wss_connect(ptr, i32, ptr)

attributes #0 = { noinline }
