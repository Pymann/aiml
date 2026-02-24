; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = internal global i32 0
@__argv = internal global ptr null

declare i32 @my.math.add(i32, i32) personality ptr @__gxx_personality_v0

declare i32 @__gxx_personality_v0(...)

declare i32 @my.math.sub(i32, i32) personality ptr @__gxx_personality_v0

define void @__aiml_init() {
entry:
  ret void

entry1:                                           ; No predecessors!
}

define i32 @__aiml_user_main(i32 %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %argc = alloca i32, align 4
  store i32 %0, ptr %argc, align 4
  %argv = alloca ptr, align 8
  store ptr %1, ptr %argv, align 8
  %res1 = alloca i32, align 4
  store i32 0, ptr %res1, align 4
  %res2 = alloca i32, align 4
  store i32 0, ptr %res2, align 4
  %2 = call i32 @my.math.add(i32 10, i32 5)
  store i32 %2, ptr %res1, align 4
  %3 = call i32 @my.math.sub(i32 10, i32 5)
  store i32 %3, ptr %res2, align 4
  ret i32 0
}

define i32 @main(i32 %argc, ptr %argv) {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  call void @__aiml_init()
  %0 = call i32 @__aiml_user_main(i32 %argc, ptr %argv)
  ret i32 %0
}

declare i32 @my.math.add.1(i32, i32) personality ptr @__gxx_personality_v0

declare i32 @my.math.sub.2(i32, i32) personality ptr @__gxx_personality_v0

define void @__aiml_init.3() {
entry:
  ret void

entry1:                                           ; No predecessors!
}

define i32 @main.4(i32 %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %argc = alloca i32, align 4
  store i32 %0, ptr %argc, align 4
  %argv = alloca ptr, align 8
  store ptr %1, ptr %argv, align 8
  %res1 = alloca i32, align 4
  store i32 0, ptr %res1, align 4
  %res2 = alloca i32, align 4
  store i32 0, ptr %res2, align 4
  %2 = call i32 @my.math.add.1(i32 10, i32 5)
  store i32 %2, ptr %res1, align 4
  %3 = call i32 @my.math.sub.2(i32 10, i32 5)
  store i32 %3, ptr %res2, align 4
  ret i32 0
}
