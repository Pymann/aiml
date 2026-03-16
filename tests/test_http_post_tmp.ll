; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [40 x i8] c"http://127.0.0.1:8080/api/auth/register\00", align 1
@1 = private unnamed_addr constant [5 x i8] c"POST\00", align 1
@2 = private unnamed_addr constant [56 x i8] c"{\22username\22:\22test2\22,\22password\22:\22pw2\22,\22email\22:\22t2@t.de\22}\00", align 1
@3 = private unnamed_addr constant [13 x i8] c"Content-Type\00", align 1
@4 = private unnamed_addr constant [17 x i8] c"application/json\00", align 1
@5 = private unnamed_addr constant [7 x i8] c"body: \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [9 x i8] c"status: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %u = alloca ptr, align 8
  store ptr null, ptr %u, align 8
  %0 = call ptr @aiml_url_parse(ptr @0)
  store ptr %0, ptr %u, align 8
  %req = alloca ptr, align 8
  store ptr null, ptr %req, align 8
  %u_val = load ptr, ptr %u, align 8
  %1 = call ptr @aiml_http_new_request(ptr @1, ptr %u_val)
  store ptr %1, ptr %req, align 8
  %bs_load = load ptr, ptr %req, align 8
  call void @aiml_http_client_set_body(ptr %bs_load, ptr @2)
  %h_base = load ptr, ptr %req, align 8
  %2 = call ptr @aiml_http_client_get_headers(ptr %h_base)
  call void @aiml_http_map_put(ptr %2, ptr @3, ptr @4)
  %resp = alloca ptr, align 8
  store ptr null, ptr %resp, align 8
  %h_base1 = load ptr, ptr %req, align 8
  %3 = call ptr @aiml_http_client_send(ptr %h_base1)
  store ptr %3, ptr %resp, align 8
  %b = alloca ptr, align 8
  store ptr null, ptr %b, align 8
  %h_base2 = load ptr, ptr %resp, align 8
  %4 = call ptr @aiml_http_resp_body(ptr %h_base2)
  store ptr %4, ptr %b, align 8
  %b_val = load ptr, ptr %b, align 8
  %5 = call ptr @__aiml_str_concat(ptr @5, ptr %b_val)
  %print_null_check = icmp eq ptr %5, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %5
  %print_call = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str)
  %st = alloca i32, align 4
  store i32 0, ptr %st, align 4
  %h_base3 = load ptr, ptr %resp, align 8
  %6 = call i32 @aiml_http_resp_status(ptr %h_base3)
  store i32 %6, ptr %st, align 4
  %st_val = load i32, ptr %st, align 4
  %7 = call ptr @__aiml_int_to_string(i32 %st_val)
  %8 = call ptr @__aiml_str_concat(ptr @7, ptr %7)
  %print_null_check4 = icmp eq ptr %8, null
  %print_safe_str5 = select i1 %print_null_check4, ptr @print_null_str.1, ptr %8
  %print_call6 = call i32 (ptr, ...) @printf(ptr @8, ptr %print_safe_str5)
  ret void
}

declare ptr @aiml_url_parse(ptr)

declare ptr @aiml_http_new_request(ptr, ptr)

declare void @aiml_http_client_set_body(ptr, ptr)

declare ptr @aiml_http_client_get_headers(ptr)

declare void @aiml_http_map_put(ptr, ptr, ptr)

declare ptr @aiml_http_client_send(ptr)

declare ptr @aiml_http_resp_body(ptr)

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

declare i32 @aiml_http_resp_status(ptr)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

attributes #0 = { noinline }
