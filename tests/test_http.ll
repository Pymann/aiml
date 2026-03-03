; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%thread_handle = type { i64, i8, i8 }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [47 x i8] c"https://example.com:8443/api/v1?page=2&q=hello\00", align 1
@1 = private unnamed_addr constant [15 x i8] c"url.protocol: \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [11 x i8] c"url.host: \00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [11 x i8] c"url.path: \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [11 x i8] c"url.port: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [15 x i8] c"url.is_https: \00", align 1
@10 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@11 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [5 x i8] c"page\00", align 1
@14 = private unnamed_addr constant [26 x i8] c"url.get_param.get(page): \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [2 x i8] c"q\00", align 1
@17 = private unnamed_addr constant [23 x i8] c"url.get_param.get(q): \00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [16 x i8] c"url.to_string: \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [23 x i8] c"http://httpbin.org/get\00", align 1
@22 = private unnamed_addr constant [29 x i8] c"GET httpbin.org/get status: \00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [10 x i8] c"resp.ok: \00", align 1
@25 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@26 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@28 = private unnamed_addr constant [35 x i8] c"resp.body len > 0: (body received)\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [9 x i8] c"server: \00", align 1
@31 = private unnamed_addr constant [2 x i8] c" \00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [16 x i8] c"hello from aiml\00", align 1
@34 = private unnamed_addr constant [13 x i8] c"Content-Type\00", align 1
@35 = private unnamed_addr constant [11 x i8] c"text/plain\00", align 1
@36 = private unnamed_addr constant [10 x i8] c"127.0.0.1\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"GET\00", align 1
@38 = private unnamed_addr constant [2 x i8] c"/\00", align 1
@39 = private unnamed_addr constant [24 x i8] c"http://127.0.0.1:18080/\00", align 1
@40 = private unnamed_addr constant [22 x i8] c"loopback GET status: \00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [16 x i8] c"loopback body: \00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@44 = private unnamed_addr constant [19 x i8] c"http loopback done\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [4 x [16 x ptr]] [[16 x ptr] [ptr @run_http_client, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @handle_root, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @http_server_thread, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @http_client_thread, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [4 x ptr] [ptr @run_http_client, ptr @handle_root, ptr @http_server_thread, ptr @http_client_thread]
@__aiml_fptable_versions = global [4 x i32] [i32 1, i32 1, i32 1, i32 1]
@__aiml_fname_run_http_client = private constant [16 x i8] c"run_http_client\00"
@__aiml_fname_handle_root = private constant [12 x i8] c"handle_root\00"
@__aiml_fname_http_server_thread = private constant [19 x i8] c"http_server_thread\00"
@__aiml_fname_http_client_thread = private constant [19 x i8] c"http_client_thread\00"
@__aiml_func_names = constant [4 x ptr] [ptr @__aiml_fname_run_http_client, ptr @__aiml_fname_handle_root, ptr @__aiml_fname_http_server_thread, ptr @__aiml_fname_http_client_thread]
@__aiml_func_count = constant i32 4
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %u = alloca ptr, align 8
  store ptr null, ptr %u, align 8
  %0 = call ptr @aiml_url_parse(ptr @0)
  store ptr %0, ptr %u, align 8
  %proto = alloca ptr, align 8
  store ptr null, ptr %proto, align 8
  %1 = call ptr @aiml_url_protocol(ptr %u)
  store ptr %1, ptr %proto, align 8
  %proto_val = load ptr, ptr %proto, align 8
  %2 = call ptr @__aiml_str_concat(ptr @1, ptr %proto_val)
  %print_null_check = icmp eq ptr %2, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %2
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %host = alloca ptr, align 8
  store ptr null, ptr %host, align 8
  %3 = call ptr @aiml_url_host(ptr %u)
  store ptr %3, ptr %host, align 8
  %host_val = load ptr, ptr %host, align 8
  %4 = call ptr @__aiml_str_concat(ptr @3, ptr %host_val)
  %print_null_check1 = icmp eq ptr %4, null
  %print_safe_str2 = select i1 %print_null_check1, ptr @print_null_str.1, ptr %4
  %print_call3 = call i32 (ptr, ...) @printf(ptr @4, ptr %print_safe_str2)
  %upath = alloca ptr, align 8
  store ptr null, ptr %upath, align 8
  %5 = call ptr @aiml_url_path(ptr %u)
  store ptr %5, ptr %upath, align 8
  %upath_val = load ptr, ptr %upath, align 8
  %6 = call ptr @__aiml_str_concat(ptr @5, ptr %upath_val)
  %print_null_check4 = icmp eq ptr %6, null
  %print_safe_str5 = select i1 %print_null_check4, ptr @print_null_str.2, ptr %6
  %print_call6 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str5)
  %port = alloca i32, align 4
  store i32 0, ptr %port, align 4
  %7 = call i32 @aiml_url_port(ptr %u)
  store i32 %7, ptr %port, align 4
  %port_val = load i32, ptr %port, align 4
  %8 = call ptr @__aiml_int_to_string(i32 %port_val)
  %9 = call ptr @__aiml_str_concat(ptr @7, ptr %8)
  %print_null_check7 = icmp eq ptr %9, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.3, ptr %9
  %print_call9 = call i32 (ptr, ...) @printf(ptr @8, ptr %print_safe_str8)
  %is_s = alloca i8, align 1
  store i8 0, ptr %is_s, align 1
  %10 = call i8 @aiml_url_is_https(ptr %u)
  store i8 %10, ptr %is_s, align 1
  %is_s_val = load i8, ptr %is_s, align 1
  %11 = icmp ne i8 %is_s_val, 0
  %12 = select i1 %11, ptr @10, ptr @11
  %13 = call ptr @__aiml_str_concat(ptr @9, ptr %12)
  %print_null_check10 = icmp eq ptr %13, null
  %print_safe_str11 = select i1 %print_null_check10, ptr @print_null_str.4, ptr %13
  %print_call12 = call i32 (ptr, ...) @printf(ptr @12, ptr %print_safe_str11)
  %page = alloca ptr, align 8
  store ptr null, ptr %page, align 8
  %chain_base = load ptr, ptr %u, align 8
  %14 = call ptr @aiml_url_get_param(ptr %chain_base)
  %15 = call ptr @aiml_http_map_get(ptr %14, ptr @13)
  store ptr %15, ptr %page, align 8
  %page_val = load ptr, ptr %page, align 8
  %16 = call ptr @__aiml_str_concat(ptr @14, ptr %page_val)
  %print_null_check13 = icmp eq ptr %16, null
  %print_safe_str14 = select i1 %print_null_check13, ptr @print_null_str.5, ptr %16
  %print_call15 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str14)
  %q = alloca ptr, align 8
  store ptr null, ptr %q, align 8
  %chain_base16 = load ptr, ptr %u, align 8
  %17 = call ptr @aiml_url_get_param(ptr %chain_base16)
  %18 = call ptr @aiml_http_map_get(ptr %17, ptr @16)
  store ptr %18, ptr %q, align 8
  %q_val = load ptr, ptr %q, align 8
  %19 = call ptr @__aiml_str_concat(ptr @17, ptr %q_val)
  %print_null_check17 = icmp eq ptr %19, null
  %print_safe_str18 = select i1 %print_null_check17, ptr @print_null_str.6, ptr %19
  %print_call19 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str18)
  %rebuilt = alloca ptr, align 8
  store ptr null, ptr %rebuilt, align 8
  %20 = call ptr @aiml_url_to_string(ptr %u)
  store ptr %20, ptr %rebuilt, align 8
  %rebuilt_val = load ptr, ptr %rebuilt, align 8
  %21 = call ptr @__aiml_str_concat(ptr @19, ptr %rebuilt_val)
  %print_null_check20 = icmp eq ptr %21, null
  %print_safe_str21 = select i1 %print_null_check20, ptr @print_null_str.7, ptr %21
  %print_call22 = call i32 (ptr, ...) @printf(ptr @20, ptr %print_safe_str21)
  %srv_th = alloca %thread_handle, align 8
  store %thread_handle zeroinitializer, ptr %srv_th, align 8
  %handle_raw = call ptr @malloc(i64 16)
  %joined_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 1
  store i8 0, ptr %joined_ptr, align 1
  %detached_ptr = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 2
  store i8 0, ptr %detached_ptr, align 1
  %tid_gep = getelementptr inbounds %thread_handle, ptr %handle_raw, i32 0, i32 0
  %22 = call i32 @pthread_create(ptr %tid_gep, ptr null, ptr @__aiml_spawn_http_server_thread, ptr null)
  store ptr %handle_raw, ptr %srv_th, align 8
  %cli_th = alloca %thread_handle, align 8
  store %thread_handle zeroinitializer, ptr %cli_th, align 8
  %handle_raw27 = call ptr @malloc(i64 16)
  %joined_ptr28 = getelementptr inbounds %thread_handle, ptr %handle_raw27, i32 0, i32 1
  store i8 0, ptr %joined_ptr28, align 1
  %detached_ptr29 = getelementptr inbounds %thread_handle, ptr %handle_raw27, i32 0, i32 2
  store i8 0, ptr %detached_ptr29, align 1
  %tid_gep30 = getelementptr inbounds %thread_handle, ptr %handle_raw27, i32 0, i32 0
  %23 = call i32 @pthread_create(ptr %tid_gep30, ptr null, ptr @__aiml_spawn_http_client_thread, ptr null)
  store ptr %handle_raw27, ptr %cli_th, align 8
  %handle_ptr = load ptr, ptr %cli_th, align 8
  %detached_ptr31 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 2
  %detached = load i8, ptr %detached_ptr31, align 1
  %join_detached_throw_cmp = icmp ne i8 %detached, 0
  br i1 %join_detached_throw_cmp, label %join_detached_throw, label %join_check_joined

entry23:                                          ; No predecessors!

entry24:                                          ; No predecessors!

entry25:                                          ; No predecessors!

entry26:                                          ; No predecessors!

join_detached_throw:                              ; preds = %entry
  %exc_alloc = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc, align 8
  call void @__cxa_throw(ptr %exc_alloc, ptr null, ptr null)
  unreachable

join_check_joined:                                ; preds = %entry
  %joined_ptr32 = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 1
  %joined = load i8, ptr %joined_ptr32, align 1
  %join_already_throw_cmp = icmp ne i8 %joined, 0
  br i1 %join_already_throw_cmp, label %join_already_throw, label %join_do

join_already_throw:                               ; preds = %join_check_joined
  %exc_alloc33 = call ptr @__cxa_allocate_exception(i64 8)
  store ptr null, ptr %exc_alloc33, align 8
  call void @__cxa_throw(ptr %exc_alloc33, ptr null, ptr null)
  unreachable

join_do:                                          ; preds = %join_check_joined
  %tid_ptr = getelementptr inbounds %thread_handle, ptr %handle_ptr, i32 0, i32 0
  %tid = load i64, ptr %tid_ptr, align 8
  %24 = call i32 @pthread_join(i64 %tid, ptr null)
  store i8 1, ptr %joined_ptr32, align 1
  %print_call34 = call i32 (ptr, ...) @printf(ptr @45, ptr @44)
  ret void
}

declare ptr @aiml_url_parse(ptr)

declare ptr @aiml_url_protocol(ptr)

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

declare ptr @aiml_url_host(ptr)

declare ptr @aiml_url_path(ptr)

declare i32 @aiml_url_port(ptr)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare i8 @aiml_url_is_https(ptr)

declare ptr @aiml_url_get_param(ptr)

declare ptr @aiml_http_map_get(ptr, ptr)

declare ptr @aiml_url_to_string(ptr)

define i32 @run_http_client() personality ptr @__gxx_personality_v0 {
entry:
  %req_url = alloca ptr, align 8
  store ptr null, ptr %req_url, align 8
  %0 = call ptr @aiml_url_parse(ptr @21)
  store ptr %0, ptr %req_url, align 8
  %resp = alloca ptr, align 8
  store ptr null, ptr %resp, align 8
  %req_url_val = load ptr, ptr %req_url, align 8
  %1 = call ptr @aiml_http_get(ptr %req_url_val)
  store ptr %1, ptr %resp, align 8
  %st = alloca i32, align 4
  store i32 0, ptr %st, align 4
  %2 = call i32 @aiml_http_resp_status(ptr %resp)
  store i32 %2, ptr %st, align 4
  %st_val = load i32, ptr %st, align 4
  %3 = call ptr @__aiml_int_to_string(i32 %st_val)
  %4 = call ptr @__aiml_str_concat(ptr @22, ptr %3)
  %print_null_check = icmp eq ptr %4, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.8, ptr %4
  %print_call = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str)
  %ok = alloca i8, align 1
  store i8 0, ptr %ok, align 1
  %5 = call i8 @aiml_http_resp_ok(ptr %resp)
  store i8 %5, ptr %ok, align 1
  %ok_val = load i8, ptr %ok, align 1
  %6 = icmp ne i8 %ok_val, 0
  %7 = select i1 %6, ptr @25, ptr @26
  %8 = call ptr @__aiml_str_concat(ptr @24, ptr %7)
  %print_null_check1 = icmp eq ptr %8, null
  %print_safe_str2 = select i1 %print_null_check1, ptr @print_null_str.9, ptr %8
  %print_call3 = call i32 (ptr, ...) @printf(ptr @27, ptr %print_safe_str2)
  %body = alloca ptr, align 8
  store ptr null, ptr %body, align 8
  %9 = call ptr @aiml_http_resp_body(ptr %resp)
  store ptr %9, ptr %body, align 8
  %print_call4 = call i32 (ptr, ...) @printf(ptr @29, ptr @28)
  call void @aiml_http_resp_free(ptr %resp)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare ptr @aiml_http_get(ptr)

declare i32 @aiml_http_resp_status(ptr)

declare i8 @aiml_http_resp_ok(ptr)

declare ptr @aiml_http_resp_body(ptr)

declare void @aiml_http_resp_free(ptr)

define void @handle_root(ptr %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %req = alloca ptr, align 8
  store ptr %0, ptr %req, align 8
  %resp = alloca ptr, align 8
  store ptr %1, ptr %resp, align 8
  %m = alloca ptr, align 8
  store ptr null, ptr %m, align 8
  %2 = call ptr @aiml_http_req_method(ptr %req)
  store ptr %2, ptr %m, align 8
  %p = alloca ptr, align 8
  store ptr null, ptr %p, align 8
  %3 = call ptr @aiml_http_req_path(ptr %req)
  store ptr %3, ptr %p, align 8
  %m_val = load ptr, ptr %m, align 8
  %4 = call ptr @__aiml_str_concat(ptr @30, ptr %m_val)
  %5 = call ptr @__aiml_str_concat(ptr %4, ptr @31)
  %p_val = load ptr, ptr %p, align 8
  %6 = call ptr @__aiml_str_concat(ptr %5, ptr %p_val)
  %print_null_check = icmp eq ptr %6, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.11, ptr %6
  %print_call = call i32 (ptr, ...) @printf(ptr @32, ptr %print_safe_str)
  %bs_load = load ptr, ptr %resp, align 8
  call void @aiml_http_resp_set_status(ptr %bs_load, i32 200)
  %bs_load1 = load ptr, ptr %resp, align 8
  call void @aiml_http_resp_set_body(ptr %bs_load1, ptr @33)
  %chain_base = load ptr, ptr %resp, align 8
  %7 = call ptr @aiml_http_resp_get_headers(ptr %chain_base)
  call void @aiml_http_map_put(ptr %7, ptr @34, ptr @35)
  call void @aiml_http_resp_send(ptr %resp)
  ret void
}

declare ptr @aiml_http_req_method(ptr)

declare ptr @aiml_http_req_path(ptr)

declare void @aiml_http_resp_set_status(ptr, i32)

declare void @aiml_http_resp_set_body(ptr, ptr)

declare ptr @aiml_http_resp_get_headers(ptr)

declare void @aiml_http_map_put(ptr, ptr, ptr)

declare void @aiml_http_resp_send(ptr)

define void @http_server_thread() personality ptr @__gxx_personality_v0 {
entry:
  %srv = alloca ptr, align 8
  store ptr null, ptr %srv, align 8
  %0 = call ptr @aiml_http_server(ptr @36, i32 18080)
  store ptr %0, ptr %srv, align 8
  call void @aiml_http_server_route(ptr %srv, ptr @37, ptr @38, ptr @handle_root)
  call void @aiml_http_server_start(ptr %srv)
  ret void
}

declare ptr @aiml_http_server(ptr, i32)

declare void @aiml_http_server_route(ptr, ptr, ptr, ptr)

declare void @aiml_http_server_start(ptr)

define void @http_client_thread() personality ptr @__gxx_personality_v0 {
entry:
  call void @time_sleep_ms(i32 200)
  %curl = alloca ptr, align 8
  store ptr null, ptr %curl, align 8
  %0 = call ptr @aiml_url_parse(ptr @39)
  store ptr %0, ptr %curl, align 8
  %resp = alloca ptr, align 8
  store ptr null, ptr %resp, align 8
  %curl_val = load ptr, ptr %curl, align 8
  %1 = call ptr @aiml_http_get(ptr %curl_val)
  store ptr %1, ptr %resp, align 8
  %st = alloca i32, align 4
  store i32 0, ptr %st, align 4
  %2 = call i32 @aiml_http_resp_status(ptr %resp)
  store i32 %2, ptr %st, align 4
  %st_val = load i32, ptr %st, align 4
  %3 = call ptr @__aiml_int_to_string(i32 %st_val)
  %4 = call ptr @__aiml_str_concat(ptr @40, ptr %3)
  %print_null_check = icmp eq ptr %4, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.12, ptr %4
  %print_call = call i32 (ptr, ...) @printf(ptr @41, ptr %print_safe_str)
  %rb = alloca ptr, align 8
  store ptr null, ptr %rb, align 8
  %5 = call ptr @aiml_http_resp_body(ptr %resp)
  store ptr %5, ptr %rb, align 8
  %rb_val = load ptr, ptr %rb, align 8
  %6 = call ptr @__aiml_str_concat(ptr @42, ptr %rb_val)
  %print_null_check1 = icmp eq ptr %6, null
  %print_safe_str2 = select i1 %print_null_check1, ptr @print_null_str.13, ptr %6
  %print_call3 = call i32 (ptr, ...) @printf(ptr @43, ptr %print_safe_str2)
  call void @aiml_http_resp_free(ptr %resp)
  ret void
}

declare void @time_sleep_ms(i32)

define internal ptr @__aiml_spawn_http_server_thread(ptr %0) {
entry:
  call void @http_server_thread()
  ret ptr null
}

declare i32 @pthread_create(ptr, ptr, ptr, ptr)

define internal ptr @__aiml_spawn_http_client_thread(ptr %0) {
entry:
  call void @http_client_thread()
  ret ptr null
}

declare ptr @__cxa_allocate_exception(i64)

declare void @__cxa_throw(ptr, ptr, ptr)

declare i32 @pthread_join(i64, ptr)

attributes #0 = { noinline }
