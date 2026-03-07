; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%buffer = type { i32, i32, i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@1 = private unnamed_addr constant [10 x i8] c"SHA-256: \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@4 = private unnamed_addr constant [10 x i8] c"SHA-512: \00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@7 = private unnamed_addr constant [17 x i8] c"SHA-256(empty): \00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"abc\00", align 1
@10 = private unnamed_addr constant [16 x i8] c"SHA3-256(abc): \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@13 = private unnamed_addr constant [20 x i8] c"Keccak-256(empty): \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"abc\00", align 1
@16 = private unnamed_addr constant [18 x i8] c"Keccak-256(abc): \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@19 = private unnamed_addr constant [13 x i8] c"RIPEMD-160: \00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@22 = private unnamed_addr constant [20 x i8] c"RIPEMD-160(empty): \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@25 = private unnamed_addr constant [14 x i8] c"BLAKE2b-512: \00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@28 = private unnamed_addr constant [14 x i8] c"BLAKE2s-256: \00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@31 = private unnamed_addr constant [6 x i8] c"MD5: \00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@34 = private unnamed_addr constant [8 x i8] c"CRC32: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [9 x i8] c"mysecret\00", align 1
@37 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@38 = private unnamed_addr constant [14 x i8] c"HMAC-SHA256: \00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [14 x i8] c"HMAC-SHA512: \00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@43 = private unnamed_addr constant [31 x i8] c"sha256_raw returned (not null)\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"abc\00", align 1
@46 = private unnamed_addr constant [34 x i8] c"keccak256_raw returned (not null)\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@47 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@48 = private unnamed_addr constant [12 x i8] c"hello world\00", align 1
@49 = private unnamed_addr constant [34 x i8] c"ripemd160_raw returned (not null)\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@50 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@51 = private unnamed_addr constant [36 x i8] c"hmac_sha256_raw returned (not null)\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@53 = private unnamed_addr constant [16 x i8] c"Phase I: raw OK\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@54 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %h256 = alloca ptr, align 8
  store ptr null, ptr %h256, align 8
  %0 = call ptr @aiml_hash_sha256(ptr @0)
  store ptr %0, ptr %h256, align 8
  %h256_val = load ptr, ptr %h256, align 8
  %1 = call ptr @__aiml_str_concat(ptr @1, ptr %h256_val)
  %print_null_check = icmp eq ptr %1, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %1
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %h512 = alloca ptr, align 8
  store ptr null, ptr %h512, align 8
  %2 = call ptr @aiml_hash_sha512(ptr @3)
  store ptr %2, ptr %h512, align 8
  %h512_val = load ptr, ptr %h512, align 8
  %3 = call ptr @__aiml_str_concat(ptr @4, ptr %h512_val)
  %print_null_check1 = icmp eq ptr %3, null
  %print_safe_str2 = select i1 %print_null_check1, ptr @print_null_str.1, ptr %3
  %print_call3 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str2)
  %hempty = alloca ptr, align 8
  store ptr null, ptr %hempty, align 8
  %4 = call ptr @aiml_hash_sha256(ptr @6)
  store ptr %4, ptr %hempty, align 8
  %hempty_val = load ptr, ptr %hempty, align 8
  %5 = call ptr @__aiml_str_concat(ptr @7, ptr %hempty_val)
  %print_null_check4 = icmp eq ptr %5, null
  %print_safe_str5 = select i1 %print_null_check4, ptr @print_null_str.2, ptr %5
  %print_call6 = call i32 (ptr, ...) @printf(ptr @8, ptr %print_safe_str5)
  %h3 = alloca ptr, align 8
  store ptr null, ptr %h3, align 8
  %6 = call ptr @aiml_hash_sha3_256(ptr @9)
  store ptr %6, ptr %h3, align 8
  %h3_val = load ptr, ptr %h3, align 8
  %7 = call ptr @__aiml_str_concat(ptr @10, ptr %h3_val)
  %print_null_check7 = icmp eq ptr %7, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.3, ptr %7
  %print_call9 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str8)
  %kec = alloca ptr, align 8
  store ptr null, ptr %kec, align 8
  %8 = call ptr @aiml_hash_keccak256(ptr @12)
  store ptr %8, ptr %kec, align 8
  %kec_val = load ptr, ptr %kec, align 8
  %9 = call ptr @__aiml_str_concat(ptr @13, ptr %kec_val)
  %print_null_check10 = icmp eq ptr %9, null
  %print_safe_str11 = select i1 %print_null_check10, ptr @print_null_str.4, ptr %9
  %print_call12 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str11)
  %kec2 = alloca ptr, align 8
  store ptr null, ptr %kec2, align 8
  %10 = call ptr @aiml_hash_keccak256(ptr @15)
  store ptr %10, ptr %kec2, align 8
  %kec2_val = load ptr, ptr %kec2, align 8
  %11 = call ptr @__aiml_str_concat(ptr @16, ptr %kec2_val)
  %print_null_check13 = icmp eq ptr %11, null
  %print_safe_str14 = select i1 %print_null_check13, ptr @print_null_str.5, ptr %11
  %print_call15 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str14)
  %rmd = alloca ptr, align 8
  store ptr null, ptr %rmd, align 8
  %12 = call ptr @aiml_hash_ripemd160(ptr @18)
  store ptr %12, ptr %rmd, align 8
  %rmd_val = load ptr, ptr %rmd, align 8
  %13 = call ptr @__aiml_str_concat(ptr @19, ptr %rmd_val)
  %print_null_check16 = icmp eq ptr %13, null
  %print_safe_str17 = select i1 %print_null_check16, ptr @print_null_str.6, ptr %13
  %print_call18 = call i32 (ptr, ...) @printf(ptr @20, ptr %print_safe_str17)
  %rmd2 = alloca ptr, align 8
  store ptr null, ptr %rmd2, align 8
  %14 = call ptr @aiml_hash_ripemd160(ptr @21)
  store ptr %14, ptr %rmd2, align 8
  %rmd2_val = load ptr, ptr %rmd2, align 8
  %15 = call ptr @__aiml_str_concat(ptr @22, ptr %rmd2_val)
  %print_null_check19 = icmp eq ptr %15, null
  %print_safe_str20 = select i1 %print_null_check19, ptr @print_null_str.7, ptr %15
  %print_call21 = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str20)
  %b2b = alloca ptr, align 8
  store ptr null, ptr %b2b, align 8
  %16 = call ptr @aiml_hash_blake2b(ptr @24)
  store ptr %16, ptr %b2b, align 8
  %b2b_val = load ptr, ptr %b2b, align 8
  %17 = call ptr @__aiml_str_concat(ptr @25, ptr %b2b_val)
  %print_null_check22 = icmp eq ptr %17, null
  %print_safe_str23 = select i1 %print_null_check22, ptr @print_null_str.8, ptr %17
  %print_call24 = call i32 (ptr, ...) @printf(ptr @26, ptr %print_safe_str23)
  %b2s = alloca ptr, align 8
  store ptr null, ptr %b2s, align 8
  %18 = call ptr @aiml_hash_blake2s(ptr @27)
  store ptr %18, ptr %b2s, align 8
  %b2s_val = load ptr, ptr %b2s, align 8
  %19 = call ptr @__aiml_str_concat(ptr @28, ptr %b2s_val)
  %print_null_check25 = icmp eq ptr %19, null
  %print_safe_str26 = select i1 %print_null_check25, ptr @print_null_str.9, ptr %19
  %print_call27 = call i32 (ptr, ...) @printf(ptr @29, ptr %print_safe_str26)
  %m = alloca ptr, align 8
  store ptr null, ptr %m, align 8
  %20 = call ptr @aiml_hash_md5(ptr @30)
  store ptr %20, ptr %m, align 8
  %m_val = load ptr, ptr %m, align 8
  %21 = call ptr @__aiml_str_concat(ptr @31, ptr %m_val)
  %print_null_check28 = icmp eq ptr %21, null
  %print_safe_str29 = select i1 %print_null_check28, ptr @print_null_str.10, ptr %21
  %print_call30 = call i32 (ptr, ...) @printf(ptr @32, ptr %print_safe_str29)
  %crc = alloca i32, align 4
  store i32 0, ptr %crc, align 4
  %22 = call i32 @aiml_hash_crc32(ptr @33)
  store i32 %22, ptr %crc, align 4
  %crc_val = load i32, ptr %crc, align 4
  %23 = call ptr @__aiml_int_to_string(i32 %crc_val)
  %24 = call ptr @__aiml_str_concat(ptr @34, ptr %23)
  %print_null_check31 = icmp eq ptr %24, null
  %print_safe_str32 = select i1 %print_null_check31, ptr @print_null_str.11, ptr %24
  %print_call33 = call i32 (ptr, ...) @printf(ptr @35, ptr %print_safe_str32)
  %secret = alloca ptr, align 8
  store ptr null, ptr %secret, align 8
  store ptr @36, ptr %secret, align 8
  %body = alloca ptr, align 8
  store ptr null, ptr %body, align 8
  store ptr @37, ptr %body, align 8
  %hmac256 = alloca ptr, align 8
  store ptr null, ptr %hmac256, align 8
  %secret_val = load ptr, ptr %secret, align 8
  %body_val = load ptr, ptr %body, align 8
  %25 = call ptr @aiml_hash_hmac_sha256(ptr %secret_val, ptr %body_val)
  store ptr %25, ptr %hmac256, align 8
  %hmac256_val = load ptr, ptr %hmac256, align 8
  %26 = call ptr @__aiml_str_concat(ptr @38, ptr %hmac256_val)
  %print_null_check34 = icmp eq ptr %26, null
  %print_safe_str35 = select i1 %print_null_check34, ptr @print_null_str.12, ptr %26
  %print_call36 = call i32 (ptr, ...) @printf(ptr @39, ptr %print_safe_str35)
  %hmac512 = alloca ptr, align 8
  store ptr null, ptr %hmac512, align 8
  %secret_val37 = load ptr, ptr %secret, align 8
  %body_val38 = load ptr, ptr %body, align 8
  %27 = call ptr @aiml_hash_hmac_sha512(ptr %secret_val37, ptr %body_val38)
  store ptr %27, ptr %hmac512, align 8
  %hmac512_val = load ptr, ptr %hmac512, align 8
  %28 = call ptr @__aiml_str_concat(ptr @40, ptr %hmac512_val)
  %print_null_check39 = icmp eq ptr %28, null
  %print_safe_str40 = select i1 %print_null_check39, ptr @print_null_str.13, ptr %28
  %print_call41 = call i32 (ptr, ...) @printf(ptr @41, ptr %print_safe_str40)
  %raw256 = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %raw256, align 8
  %29 = call ptr @aiml_hash_sha256_raw(ptr @42)
  store ptr %29, ptr %raw256, align 8
  %print_call42 = call i32 (ptr, ...) @printf(ptr @44, ptr @43)
  %rawkec = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %rawkec, align 8
  %30 = call ptr @aiml_hash_keccak256_raw(ptr @45)
  store ptr %30, ptr %rawkec, align 8
  %print_call43 = call i32 (ptr, ...) @printf(ptr @47, ptr @46)
  %rawrmd = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %rawrmd, align 8
  %31 = call ptr @aiml_hash_ripemd160_raw(ptr @48)
  store ptr %31, ptr %rawrmd, align 8
  %print_call44 = call i32 (ptr, ...) @printf(ptr @50, ptr @49)
  %rawhmac = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %rawhmac, align 8
  %secret_val45 = load ptr, ptr %secret, align 8
  %body_val46 = load ptr, ptr %body, align 8
  %32 = call ptr @aiml_hash_hmac_sha256_raw(ptr %secret_val45, ptr %body_val46)
  store ptr %32, ptr %rawhmac, align 8
  %print_call47 = call i32 (ptr, ...) @printf(ptr @52, ptr @51)
  %print_call48 = call i32 (ptr, ...) @printf(ptr @54, ptr @53)
  ret void
}

declare ptr @aiml_hash_sha256(ptr)

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

declare ptr @aiml_hash_sha512(ptr)

declare ptr @aiml_hash_sha3_256(ptr)

declare ptr @aiml_hash_keccak256(ptr)

declare ptr @aiml_hash_ripemd160(ptr)

declare ptr @aiml_hash_blake2b(ptr)

declare ptr @aiml_hash_blake2s(ptr)

declare ptr @aiml_hash_md5(ptr)

declare i32 @aiml_hash_crc32(ptr)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare ptr @aiml_hash_hmac_sha256(ptr, ptr)

declare ptr @aiml_hash_hmac_sha512(ptr, ptr)

declare ptr @aiml_hash_sha256_raw(ptr)

declare ptr @aiml_hash_keccak256_raw(ptr)

declare ptr @aiml_hash_ripemd160_raw(ptr)

declare ptr @aiml_hash_hmac_sha256_raw(ptr, ptr)

attributes #0 = { noinline }
