; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_gltf_node = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%list_gltf_mesh = type { i32, i32, ptr }
%list_gltf_material = type { i32, i32, ptr }
%buffer = type { i32, i32, i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [22 x i8] c"/nonexistent_file.glb\00", align 1
@1 = private unnamed_addr constant [20 x i8] c"Phase A: bad.valid=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [11 x i8] c"bad.error=\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [11 x i8] c"Phase A OK\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [15 x i8] c"tests/test.glb\00", align 1
@8 = private unnamed_addr constant [22 x i8] c"Phase B: scene.valid=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [25 x i8] c"Phase B OK: scene loaded\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [20 x i8] c"Phase C: nodes.len=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [21 x i8] c"Phase C: meshes.len=\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [24 x i8] c"Phase C: materials.len=\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [11 x i8] c"Phase C OK\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [20 x i8] c"Phase D: node.name=\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [23 x i8] c"Phase D: translation=(\00", align 1
@fmt_f32 = private unnamed_addr constant [5 x i8] c"%.6g\00", align 1
@23 = private unnamed_addr constant [2 x i8] c",\00", align 1
@24 = private unnamed_addr constant [2 x i8] c",\00", align 1
@25 = private unnamed_addr constant [2 x i8] c")\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [21 x i8] c"Phase D: rotation_w=\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@29 = private unnamed_addr constant [17 x i8] c"Phase D: scale=(\00", align 1
@30 = private unnamed_addr constant [2 x i8] c",\00", align 1
@31 = private unnamed_addr constant [2 x i8] c",\00", align 1
@32 = private unnamed_addr constant [2 x i8] c")\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@34 = private unnamed_addr constant [11 x i8] c"Phase D OK\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [20 x i8] c"Phase E: mesh.name=\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@38 = private unnamed_addr constant [26 x i8] c"Phase E: primitive_count=\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [23 x i8] c"Phase E: vertex_count=\00", align 1
@41 = private unnamed_addr constant [15 x i8] c", index_count=\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@42 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@43 = private unnamed_addr constant [11 x i8] c"Phase E OK\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [24 x i8] c"Phase F: positions.len=\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@46 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@47 = private unnamed_addr constant [22 x i8] c"Phase F: indices.len=\00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@49 = private unnamed_addr constant [23 x i8] c"Phase F: positions[0]=\00", align 1
@print_null_str.20 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@50 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@51 = private unnamed_addr constant [21 x i8] c"Phase F: indices[0]=\00", align 1
@print_null_str.21 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@53 = private unnamed_addr constant [11 x i8] c"Phase F OK\00", align 1
@print_null_str.22 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@54 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@55 = private unnamed_addr constant [19 x i8] c"Phase G: mat.name=\00", align 1
@print_null_str.23 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@57 = private unnamed_addr constant [22 x i8] c"Phase G: base_color=(\00", align 1
@58 = private unnamed_addr constant [2 x i8] c",\00", align 1
@59 = private unnamed_addr constant [2 x i8] c",\00", align 1
@60 = private unnamed_addr constant [2 x i8] c",\00", align 1
@61 = private unnamed_addr constant [2 x i8] c")\00", align 1
@print_null_str.24 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@62 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@63 = private unnamed_addr constant [19 x i8] c"Phase G: metallic=\00", align 1
@64 = private unnamed_addr constant [13 x i8] c", roughness=\00", align 1
@print_null_str.25 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@65 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@66 = private unnamed_addr constant [21 x i8] c"Phase G: alpha_mode=\00", align 1
@67 = private unnamed_addr constant [16 x i8] c", double_sided=\00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@68 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@69 = private unnamed_addr constant [11 x i8] c"Phase G OK\00", align 1
@print_null_str.27 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@70 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@71 = private unnamed_addr constant [22 x i8] c"All gltf tests passed\00", align 1
@print_null_str.28 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@72 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %bad = alloca ptr, align 8
  store ptr null, ptr %bad, align 8
  %0 = call ptr @aiml_gltf_load(ptr @0)
  store ptr %0, ptr %bad, align 8
  %bad_ok = alloca i32, align 4
  store i32 0, ptr %bad_ok, align 4
  %h_base = load ptr, ptr %bad, align 8
  %1 = call i32 @aiml_gltf_scene_valid(ptr %h_base)
  store i32 %1, ptr %bad_ok, align 4
  %bad_ok_val = load i32, ptr %bad_ok, align 4
  %2 = call ptr @__aiml_int_to_string(i32 %bad_ok_val)
  %3 = call ptr @__aiml_str_concat(ptr @1, ptr %2)
  %print_null_check = icmp eq ptr %3, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %3
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %bad_err = alloca ptr, align 8
  store ptr null, ptr %bad_err, align 8
  %h_base1 = load ptr, ptr %bad, align 8
  %4 = call ptr @aiml_gltf_scene_error(ptr %h_base1)
  store ptr %4, ptr %bad_err, align 8
  %bad_err_val = load ptr, ptr %bad_err, align 8
  %5 = call ptr @__aiml_str_concat(ptr @3, ptr %bad_err_val)
  %print_null_check2 = icmp eq ptr %5, null
  %print_safe_str3 = select i1 %print_null_check2, ptr @print_null_str.1, ptr %5
  %print_call4 = call i32 (ptr, ...) @printf(ptr @4, ptr %print_safe_str3)
  %h_base5 = load ptr, ptr %bad, align 8
  call void @aiml_gltf_scene_free(ptr %h_base5)
  %print_call6 = call i32 (ptr, ...) @printf(ptr @6, ptr @5)
  %p = alloca ptr, align 8
  store ptr null, ptr %p, align 8
  %path_init = call ptr @aiml_path_intern(ptr @7)
  store ptr %path_init, ptr %p, align 8
  %scene = alloca ptr, align 8
  store ptr null, ptr %scene, align 8
  %p_val = load ptr, ptr %p, align 8
  %6 = call ptr @aiml_gltf_load(ptr %p_val)
  store ptr %6, ptr %scene, align 8
  %ok = alloca i32, align 4
  store i32 0, ptr %ok, align 4
  %h_base7 = load ptr, ptr %scene, align 8
  %7 = call i32 @aiml_gltf_scene_valid(ptr %h_base7)
  store i32 %7, ptr %ok, align 4
  %ok_val = load i32, ptr %ok, align 4
  %8 = call ptr @__aiml_int_to_string(i32 %ok_val)
  %9 = call ptr @__aiml_str_concat(ptr @8, ptr %8)
  %print_null_check8 = icmp eq ptr %9, null
  %print_safe_str9 = select i1 %print_null_check8, ptr @print_null_str.3, ptr %9
  %print_call10 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str9)
  %print_call11 = call i32 (ptr, ...) @printf(ptr @11, ptr @10)
  %nodes = alloca %list_gltf_node, align 8
  store %list_gltf_node zeroinitializer, ptr %nodes, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %nodes, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %nodes, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %10 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %nodes, i32 0, i32 2
  store ptr %10, ptr %dataPtr, align 8
  %h_base12 = load ptr, ptr %scene, align 8
  %gltf_list = alloca %GenericList, align 8
  %sizePtr13 = getelementptr inbounds %GenericList, ptr %gltf_list, i32 0, i32 0
  store i32 0, ptr %sizePtr13, align 4
  %capPtr14 = getelementptr inbounds %GenericList, ptr %gltf_list, i32 0, i32 1
  store i32 8, ptr %capPtr14, align 4
  %11 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr15 = getelementptr inbounds %GenericList, ptr %gltf_list, i32 0, i32 2
  store ptr %11, ptr %dataPtr15, align 8
  call void @aiml_gltf_scene_nodes_fill(ptr %h_base12, ptr %gltf_list)
  %12 = call ptr @memcpy(ptr %nodes, ptr %gltf_list, i64 16)
  %nc = alloca i32, align 4
  store i32 0, ptr %nc, align 4
  %sizePtr16 = getelementptr inbounds %GenericList, ptr %nodes, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr16, align 4
  store i32 %sizeVal, ptr %nc, align 4
  %nc_val = load i32, ptr %nc, align 4
  %13 = call ptr @__aiml_int_to_string(i32 %nc_val)
  %14 = call ptr @__aiml_str_concat(ptr @12, ptr %13)
  %print_null_check17 = icmp eq ptr %14, null
  %print_safe_str18 = select i1 %print_null_check17, ptr @print_null_str.5, ptr %14
  %print_call19 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str18)
  %meshes = alloca %list_gltf_mesh, align 8
  store %list_gltf_mesh zeroinitializer, ptr %meshes, align 8
  %sizePtr20 = getelementptr inbounds %GenericList, ptr %meshes, i32 0, i32 0
  store i32 0, ptr %sizePtr20, align 4
  %capPtr21 = getelementptr inbounds %GenericList, ptr %meshes, i32 0, i32 1
  store i32 4, ptr %capPtr21, align 4
  %15 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr22 = getelementptr inbounds %GenericList, ptr %meshes, i32 0, i32 2
  store ptr %15, ptr %dataPtr22, align 8
  %h_base23 = load ptr, ptr %scene, align 8
  %gltf_list24 = alloca %GenericList, align 8
  %sizePtr25 = getelementptr inbounds %GenericList, ptr %gltf_list24, i32 0, i32 0
  store i32 0, ptr %sizePtr25, align 4
  %capPtr26 = getelementptr inbounds %GenericList, ptr %gltf_list24, i32 0, i32 1
  store i32 8, ptr %capPtr26, align 4
  %16 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr27 = getelementptr inbounds %GenericList, ptr %gltf_list24, i32 0, i32 2
  store ptr %16, ptr %dataPtr27, align 8
  call void @aiml_gltf_scene_meshes_fill(ptr %h_base23, ptr %gltf_list24)
  %17 = call ptr @memcpy(ptr %meshes, ptr %gltf_list24, i64 16)
  %mc = alloca i32, align 4
  store i32 0, ptr %mc, align 4
  %sizePtr28 = getelementptr inbounds %GenericList, ptr %meshes, i32 0, i32 0
  %sizeVal29 = load i32, ptr %sizePtr28, align 4
  store i32 %sizeVal29, ptr %mc, align 4
  %mc_val = load i32, ptr %mc, align 4
  %18 = call ptr @__aiml_int_to_string(i32 %mc_val)
  %19 = call ptr @__aiml_str_concat(ptr @14, ptr %18)
  %print_null_check30 = icmp eq ptr %19, null
  %print_safe_str31 = select i1 %print_null_check30, ptr @print_null_str.6, ptr %19
  %print_call32 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str31)
  %mats = alloca %list_gltf_material, align 8
  store %list_gltf_material zeroinitializer, ptr %mats, align 8
  %sizePtr33 = getelementptr inbounds %GenericList, ptr %mats, i32 0, i32 0
  store i32 0, ptr %sizePtr33, align 4
  %capPtr34 = getelementptr inbounds %GenericList, ptr %mats, i32 0, i32 1
  store i32 4, ptr %capPtr34, align 4
  %20 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr35 = getelementptr inbounds %GenericList, ptr %mats, i32 0, i32 2
  store ptr %20, ptr %dataPtr35, align 8
  %h_base36 = load ptr, ptr %scene, align 8
  %gltf_list37 = alloca %GenericList, align 8
  %sizePtr38 = getelementptr inbounds %GenericList, ptr %gltf_list37, i32 0, i32 0
  store i32 0, ptr %sizePtr38, align 4
  %capPtr39 = getelementptr inbounds %GenericList, ptr %gltf_list37, i32 0, i32 1
  store i32 8, ptr %capPtr39, align 4
  %21 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %dataPtr40 = getelementptr inbounds %GenericList, ptr %gltf_list37, i32 0, i32 2
  store ptr %21, ptr %dataPtr40, align 8
  call void @aiml_gltf_scene_materials_fill(ptr %h_base36, ptr %gltf_list37)
  %22 = call ptr @memcpy(ptr %mats, ptr %gltf_list37, i64 16)
  %matc = alloca i32, align 4
  store i32 0, ptr %matc, align 4
  %sizePtr41 = getelementptr inbounds %GenericList, ptr %mats, i32 0, i32 0
  %sizeVal42 = load i32, ptr %sizePtr41, align 4
  store i32 %sizeVal42, ptr %matc, align 4
  %matc_val = load i32, ptr %matc, align 4
  %23 = call ptr @__aiml_int_to_string(i32 %matc_val)
  %24 = call ptr @__aiml_str_concat(ptr @16, ptr %23)
  %print_null_check43 = icmp eq ptr %24, null
  %print_safe_str44 = select i1 %print_null_check43, ptr @print_null_str.7, ptr %24
  %print_call45 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str44)
  %print_call46 = call i32 (ptr, ...) @printf(ptr @19, ptr @18)
  %node = alloca ptr, align 8
  store ptr null, ptr %node, align 8
  %dataPtr47 = getelementptr inbounds %GenericList, ptr %nodes, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr47, align 8
  %25 = getelementptr inbounds ptr, ptr %dataBase, i32 0
  %getVal = load ptr, ptr %25, align 8
  store ptr %getVal, ptr %node, align 8
  %node_name = alloca ptr, align 8
  store ptr null, ptr %node_name, align 8
  %h_base48 = load ptr, ptr %node, align 8
  %26 = call ptr @aiml_gltf_node_name(ptr %h_base48)
  store ptr %26, ptr %node_name, align 8
  %node_name_val = load ptr, ptr %node_name, align 8
  %27 = call ptr @__aiml_str_concat(ptr @20, ptr %node_name_val)
  %print_null_check49 = icmp eq ptr %27, null
  %print_safe_str50 = select i1 %print_null_check49, ptr @print_null_str.9, ptr %27
  %print_call51 = call i32 (ptr, ...) @printf(ptr @21, ptr %print_safe_str50)
  %tx = alloca float, align 4
  store float 0.000000e+00, ptr %tx, align 4
  %h_base52 = load ptr, ptr %node, align 8
  %28 = call float @aiml_gltf_node_translation_x(ptr %h_base52)
  store float %28, ptr %tx, align 4
  %ty = alloca float, align 4
  store float 0.000000e+00, ptr %ty, align 4
  %h_base53 = load ptr, ptr %node, align 8
  %29 = call float @aiml_gltf_node_translation_y(ptr %h_base53)
  store float %29, ptr %ty, align 4
  %tz = alloca float, align 4
  store float 0.000000e+00, ptr %tz, align 4
  %h_base54 = load ptr, ptr %node, align 8
  %30 = call float @aiml_gltf_node_translation_z(ptr %h_base54)
  store float %30, ptr %tz, align 4
  %tx_val = load float, ptr %tx, align 4
  %31 = call ptr @__aiml_f32_to_string(float %tx_val)
  %32 = call ptr @__aiml_str_concat(ptr @22, ptr %31)
  %33 = call ptr @__aiml_str_concat(ptr %32, ptr @23)
  %ty_val = load float, ptr %ty, align 4
  %34 = call ptr @__aiml_f32_to_string(float %ty_val)
  %35 = call ptr @__aiml_str_concat(ptr %33, ptr %34)
  %36 = call ptr @__aiml_str_concat(ptr %35, ptr @24)
  %tz_val = load float, ptr %tz, align 4
  %37 = call ptr @__aiml_f32_to_string(float %tz_val)
  %38 = call ptr @__aiml_str_concat(ptr %36, ptr %37)
  %39 = call ptr @__aiml_str_concat(ptr %38, ptr @25)
  %print_null_check55 = icmp eq ptr %39, null
  %print_safe_str56 = select i1 %print_null_check55, ptr @print_null_str.10, ptr %39
  %print_call57 = call i32 (ptr, ...) @printf(ptr @26, ptr %print_safe_str56)
  %rw = alloca float, align 4
  store float 0.000000e+00, ptr %rw, align 4
  %h_base58 = load ptr, ptr %node, align 8
  %40 = call float @aiml_gltf_node_rotation_w(ptr %h_base58)
  store float %40, ptr %rw, align 4
  %rw_val = load float, ptr %rw, align 4
  %41 = call ptr @__aiml_f32_to_string(float %rw_val)
  %42 = call ptr @__aiml_str_concat(ptr @27, ptr %41)
  %print_null_check59 = icmp eq ptr %42, null
  %print_safe_str60 = select i1 %print_null_check59, ptr @print_null_str.11, ptr %42
  %print_call61 = call i32 (ptr, ...) @printf(ptr @28, ptr %print_safe_str60)
  %sx = alloca float, align 4
  store float 0.000000e+00, ptr %sx, align 4
  %h_base62 = load ptr, ptr %node, align 8
  %43 = call float @aiml_gltf_node_scale_x(ptr %h_base62)
  store float %43, ptr %sx, align 4
  %sy = alloca float, align 4
  store float 0.000000e+00, ptr %sy, align 4
  %h_base63 = load ptr, ptr %node, align 8
  %44 = call float @aiml_gltf_node_scale_y(ptr %h_base63)
  store float %44, ptr %sy, align 4
  %sz = alloca float, align 4
  store float 0.000000e+00, ptr %sz, align 4
  %h_base64 = load ptr, ptr %node, align 8
  %45 = call float @aiml_gltf_node_scale_z(ptr %h_base64)
  store float %45, ptr %sz, align 4
  %sx_val = load float, ptr %sx, align 4
  %46 = call ptr @__aiml_f32_to_string(float %sx_val)
  %47 = call ptr @__aiml_str_concat(ptr @29, ptr %46)
  %48 = call ptr @__aiml_str_concat(ptr %47, ptr @30)
  %sy_val = load float, ptr %sy, align 4
  %49 = call ptr @__aiml_f32_to_string(float %sy_val)
  %50 = call ptr @__aiml_str_concat(ptr %48, ptr %49)
  %51 = call ptr @__aiml_str_concat(ptr %50, ptr @31)
  %sz_val = load float, ptr %sz, align 4
  %52 = call ptr @__aiml_f32_to_string(float %sz_val)
  %53 = call ptr @__aiml_str_concat(ptr %51, ptr %52)
  %54 = call ptr @__aiml_str_concat(ptr %53, ptr @32)
  %print_null_check65 = icmp eq ptr %54, null
  %print_safe_str66 = select i1 %print_null_check65, ptr @print_null_str.12, ptr %54
  %print_call67 = call i32 (ptr, ...) @printf(ptr @33, ptr %print_safe_str66)
  %print_call68 = call i32 (ptr, ...) @printf(ptr @35, ptr @34)
  %mesh = alloca ptr, align 8
  store ptr null, ptr %mesh, align 8
  %dataPtr69 = getelementptr inbounds %GenericList, ptr %meshes, i32 0, i32 2
  %dataBase70 = load ptr, ptr %dataPtr69, align 8
  %55 = getelementptr inbounds ptr, ptr %dataBase70, i32 0
  %getVal71 = load ptr, ptr %55, align 8
  store ptr %getVal71, ptr %mesh, align 8
  %mesh_name = alloca ptr, align 8
  store ptr null, ptr %mesh_name, align 8
  %h_base72 = load ptr, ptr %mesh, align 8
  %56 = call ptr @aiml_gltf_mesh_name(ptr %h_base72)
  store ptr %56, ptr %mesh_name, align 8
  %mesh_name_val = load ptr, ptr %mesh_name, align 8
  %57 = call ptr @__aiml_str_concat(ptr @36, ptr %mesh_name_val)
  %print_null_check73 = icmp eq ptr %57, null
  %print_safe_str74 = select i1 %print_null_check73, ptr @print_null_str.14, ptr %57
  %print_call75 = call i32 (ptr, ...) @printf(ptr @37, ptr %print_safe_str74)
  %pc = alloca i32, align 4
  store i32 0, ptr %pc, align 4
  %h_base76 = load ptr, ptr %mesh, align 8
  %58 = call i32 @aiml_gltf_mesh_primitive_count(ptr %h_base76)
  store i32 %58, ptr %pc, align 4
  %pc_val = load i32, ptr %pc, align 4
  %59 = call ptr @__aiml_int_to_string(i32 %pc_val)
  %60 = call ptr @__aiml_str_concat(ptr @38, ptr %59)
  %print_null_check77 = icmp eq ptr %60, null
  %print_safe_str78 = select i1 %print_null_check77, ptr @print_null_str.15, ptr %60
  %print_call79 = call i32 (ptr, ...) @printf(ptr @39, ptr %print_safe_str78)
  %prim = alloca ptr, align 8
  store ptr null, ptr %prim, align 8
  %h_base80 = load ptr, ptr %mesh, align 8
  %61 = call ptr @aiml_gltf_mesh_primitive(ptr %h_base80, i32 0)
  store ptr %61, ptr %prim, align 8
  %vc = alloca i32, align 4
  store i32 0, ptr %vc, align 4
  %h_base81 = load ptr, ptr %prim, align 8
  %62 = call i32 @aiml_gltf_prim_vertex_count(ptr %h_base81)
  store i32 %62, ptr %vc, align 4
  %ic = alloca i32, align 4
  store i32 0, ptr %ic, align 4
  %h_base82 = load ptr, ptr %prim, align 8
  %63 = call i32 @aiml_gltf_prim_index_count(ptr %h_base82)
  store i32 %63, ptr %ic, align 4
  %vc_val = load i32, ptr %vc, align 4
  %64 = call ptr @__aiml_int_to_string(i32 %vc_val)
  %65 = call ptr @__aiml_str_concat(ptr @40, ptr %64)
  %66 = call ptr @__aiml_str_concat(ptr %65, ptr @41)
  %ic_val = load i32, ptr %ic, align 4
  %67 = call ptr @__aiml_int_to_string(i32 %ic_val)
  %68 = call ptr @__aiml_str_concat(ptr %66, ptr %67)
  %print_null_check83 = icmp eq ptr %68, null
  %print_safe_str84 = select i1 %print_null_check83, ptr @print_null_str.16, ptr %68
  %print_call85 = call i32 (ptr, ...) @printf(ptr @42, ptr %print_safe_str84)
  %print_call86 = call i32 (ptr, ...) @printf(ptr @44, ptr @43)
  %positions = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %positions, align 8
  %h_base87 = load ptr, ptr %prim, align 8
  %69 = call ptr @aiml_gltf_prim_positions(ptr %h_base87)
  %gltf_buf = alloca %buffer, align 8
  %70 = call ptr @memcpy(ptr %gltf_buf, ptr %69, i64 24)
  %71 = call ptr @memcpy(ptr %positions, ptr %gltf_buf, i64 24)
  %pos_bytes = alloca i32, align 4
  store i32 0, ptr %pos_bytes, align 4
  %sz_ptr = getelementptr inbounds %buffer, ptr %positions, i32 0, i32 0
  %buf_len = load i32, ptr %sz_ptr, align 4
  store i32 %buf_len, ptr %pos_bytes, align 4
  %pos_bytes_val = load i32, ptr %pos_bytes, align 4
  %72 = call ptr @__aiml_int_to_string(i32 %pos_bytes_val)
  %73 = call ptr @__aiml_str_concat(ptr @45, ptr %72)
  %print_null_check88 = icmp eq ptr %73, null
  %print_safe_str89 = select i1 %print_null_check88, ptr @print_null_str.18, ptr %73
  %print_call90 = call i32 (ptr, ...) @printf(ptr @46, ptr %print_safe_str89)
  %indices = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %indices, align 8
  %h_base91 = load ptr, ptr %prim, align 8
  %74 = call ptr @aiml_gltf_prim_indices(ptr %h_base91)
  %gltf_buf92 = alloca %buffer, align 8
  %75 = call ptr @memcpy(ptr %gltf_buf92, ptr %74, i64 24)
  %76 = call ptr @memcpy(ptr %indices, ptr %gltf_buf92, i64 24)
  %idx_bytes = alloca i32, align 4
  store i32 0, ptr %idx_bytes, align 4
  %sz_ptr93 = getelementptr inbounds %buffer, ptr %indices, i32 0, i32 0
  %buf_len94 = load i32, ptr %sz_ptr93, align 4
  store i32 %buf_len94, ptr %idx_bytes, align 4
  %idx_bytes_val = load i32, ptr %idx_bytes, align 4
  %77 = call ptr @__aiml_int_to_string(i32 %idx_bytes_val)
  %78 = call ptr @__aiml_str_concat(ptr @47, ptr %77)
  %print_null_check95 = icmp eq ptr %78, null
  %print_safe_str96 = select i1 %print_null_check95, ptr @print_null_str.19, ptr %78
  %print_call97 = call i32 (ptr, ...) @printf(ptr @48, ptr %print_safe_str96)
  %x0 = alloca float, align 4
  store float 0.000000e+00, ptr %x0, align 4
  %data_fld = getelementptr inbounds %buffer, ptr %positions, i32 0, i32 4
  %data_ptr = load ptr, ptr %data_fld, align 8
  %elem_ptr = getelementptr inbounds i8, ptr %data_ptr, i64 0
  %elem_val = load float, ptr %elem_ptr, align 4
  store float %elem_val, ptr %x0, align 4
  %x0_val = load float, ptr %x0, align 4
  %79 = call ptr @__aiml_f32_to_string(float %x0_val)
  %80 = call ptr @__aiml_str_concat(ptr @49, ptr %79)
  %print_null_check98 = icmp eq ptr %80, null
  %print_safe_str99 = select i1 %print_null_check98, ptr @print_null_str.20, ptr %80
  %print_call100 = call i32 (ptr, ...) @printf(ptr @50, ptr %print_safe_str99)
  %i0 = alloca i32, align 4
  store i32 0, ptr %i0, align 4
  %data_fld101 = getelementptr inbounds %buffer, ptr %indices, i32 0, i32 4
  %data_ptr102 = load ptr, ptr %data_fld101, align 8
  %elem_ptr103 = getelementptr inbounds i8, ptr %data_ptr102, i64 0
  %elem_val104 = load i32, ptr %elem_ptr103, align 4
  store i32 %elem_val104, ptr %i0, align 4
  %i0_val = load i32, ptr %i0, align 4
  %81 = call ptr @__aiml_int_to_string(i32 %i0_val)
  %82 = call ptr @__aiml_str_concat(ptr @51, ptr %81)
  %print_null_check105 = icmp eq ptr %82, null
  %print_safe_str106 = select i1 %print_null_check105, ptr @print_null_str.21, ptr %82
  %print_call107 = call i32 (ptr, ...) @printf(ptr @52, ptr %print_safe_str106)
  %print_call108 = call i32 (ptr, ...) @printf(ptr @54, ptr @53)
  %mat = alloca ptr, align 8
  store ptr null, ptr %mat, align 8
  %h_base109 = load ptr, ptr %prim, align 8
  %83 = call ptr @aiml_gltf_prim_material(ptr %h_base109)
  store ptr %83, ptr %mat, align 8
  %mat_name = alloca ptr, align 8
  store ptr null, ptr %mat_name, align 8
  %h_base110 = load ptr, ptr %mat, align 8
  %84 = call ptr @aiml_gltf_mat_name(ptr %h_base110)
  store ptr %84, ptr %mat_name, align 8
  %mat_name_val = load ptr, ptr %mat_name, align 8
  %85 = call ptr @__aiml_str_concat(ptr @55, ptr %mat_name_val)
  %print_null_check111 = icmp eq ptr %85, null
  %print_safe_str112 = select i1 %print_null_check111, ptr @print_null_str.23, ptr %85
  %print_call113 = call i32 (ptr, ...) @printf(ptr @56, ptr %print_safe_str112)
  %r = alloca float, align 4
  store float 0.000000e+00, ptr %r, align 4
  %h_base114 = load ptr, ptr %mat, align 8
  %86 = call float @aiml_gltf_mat_base_color_r(ptr %h_base114)
  store float %86, ptr %r, align 4
  %g = alloca float, align 4
  store float 0.000000e+00, ptr %g, align 4
  %h_base115 = load ptr, ptr %mat, align 8
  %87 = call float @aiml_gltf_mat_base_color_g(ptr %h_base115)
  store float %87, ptr %g, align 4
  %b = alloca float, align 4
  store float 0.000000e+00, ptr %b, align 4
  %h_base116 = load ptr, ptr %mat, align 8
  %88 = call float @aiml_gltf_mat_base_color_b(ptr %h_base116)
  store float %88, ptr %b, align 4
  %a = alloca float, align 4
  store float 0.000000e+00, ptr %a, align 4
  %h_base117 = load ptr, ptr %mat, align 8
  %89 = call float @aiml_gltf_mat_base_color_a(ptr %h_base117)
  store float %89, ptr %a, align 4
  %r_val = load float, ptr %r, align 4
  %90 = call ptr @__aiml_f32_to_string(float %r_val)
  %91 = call ptr @__aiml_str_concat(ptr @57, ptr %90)
  %92 = call ptr @__aiml_str_concat(ptr %91, ptr @58)
  %g_val = load float, ptr %g, align 4
  %93 = call ptr @__aiml_f32_to_string(float %g_val)
  %94 = call ptr @__aiml_str_concat(ptr %92, ptr %93)
  %95 = call ptr @__aiml_str_concat(ptr %94, ptr @59)
  %b_val = load float, ptr %b, align 4
  %96 = call ptr @__aiml_f32_to_string(float %b_val)
  %97 = call ptr @__aiml_str_concat(ptr %95, ptr %96)
  %98 = call ptr @__aiml_str_concat(ptr %97, ptr @60)
  %a_val = load float, ptr %a, align 4
  %99 = call ptr @__aiml_f32_to_string(float %a_val)
  %100 = call ptr @__aiml_str_concat(ptr %98, ptr %99)
  %101 = call ptr @__aiml_str_concat(ptr %100, ptr @61)
  %print_null_check118 = icmp eq ptr %101, null
  %print_safe_str119 = select i1 %print_null_check118, ptr @print_null_str.24, ptr %101
  %print_call120 = call i32 (ptr, ...) @printf(ptr @62, ptr %print_safe_str119)
  %met = alloca float, align 4
  store float 0.000000e+00, ptr %met, align 4
  %h_base121 = load ptr, ptr %mat, align 8
  %102 = call float @aiml_gltf_mat_metallic(ptr %h_base121)
  store float %102, ptr %met, align 4
  %rough = alloca float, align 4
  store float 0.000000e+00, ptr %rough, align 4
  %h_base122 = load ptr, ptr %mat, align 8
  %103 = call float @aiml_gltf_mat_roughness(ptr %h_base122)
  store float %103, ptr %rough, align 4
  %met_val = load float, ptr %met, align 4
  %104 = call ptr @__aiml_f32_to_string(float %met_val)
  %105 = call ptr @__aiml_str_concat(ptr @63, ptr %104)
  %106 = call ptr @__aiml_str_concat(ptr %105, ptr @64)
  %rough_val = load float, ptr %rough, align 4
  %107 = call ptr @__aiml_f32_to_string(float %rough_val)
  %108 = call ptr @__aiml_str_concat(ptr %106, ptr %107)
  %print_null_check123 = icmp eq ptr %108, null
  %print_safe_str124 = select i1 %print_null_check123, ptr @print_null_str.25, ptr %108
  %print_call125 = call i32 (ptr, ...) @printf(ptr @65, ptr %print_safe_str124)
  %alpha = alloca ptr, align 8
  store ptr null, ptr %alpha, align 8
  %h_base126 = load ptr, ptr %mat, align 8
  %109 = call ptr @aiml_gltf_mat_alpha_mode(ptr %h_base126)
  store ptr %109, ptr %alpha, align 8
  %ds = alloca i32, align 4
  store i32 0, ptr %ds, align 4
  %h_base127 = load ptr, ptr %mat, align 8
  %110 = call i32 @aiml_gltf_mat_double_sided(ptr %h_base127)
  store i32 %110, ptr %ds, align 4
  %alpha_val = load ptr, ptr %alpha, align 8
  %111 = call ptr @__aiml_str_concat(ptr @66, ptr %alpha_val)
  %112 = call ptr @__aiml_str_concat(ptr %111, ptr @67)
  %ds_val = load i32, ptr %ds, align 4
  %113 = call ptr @__aiml_int_to_string(i32 %ds_val)
  %114 = call ptr @__aiml_str_concat(ptr %112, ptr %113)
  %print_null_check128 = icmp eq ptr %114, null
  %print_safe_str129 = select i1 %print_null_check128, ptr @print_null_str.26, ptr %114
  %print_call130 = call i32 (ptr, ...) @printf(ptr @68, ptr %print_safe_str129)
  %print_call131 = call i32 (ptr, ...) @printf(ptr @70, ptr @69)
  %h_base132 = load ptr, ptr %scene, align 8
  call void @aiml_gltf_scene_free(ptr %h_base132)
  %print_call133 = call i32 (ptr, ...) @printf(ptr @72, ptr @71)
  ret void
}

declare ptr @aiml_gltf_load(ptr)

declare i32 @aiml_gltf_scene_valid(ptr)

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

declare ptr @aiml_gltf_scene_error(ptr)

declare void @aiml_gltf_scene_free(ptr)

declare ptr @aiml_path_intern(ptr)

declare void @aiml_gltf_scene_nodes_fill(ptr, ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare void @aiml_gltf_scene_meshes_fill(ptr, ptr)

declare void @aiml_gltf_scene_materials_fill(ptr, ptr)

declare ptr @aiml_gltf_node_name(ptr)

declare float @aiml_gltf_node_translation_x(ptr)

declare float @aiml_gltf_node_translation_y(ptr)

declare float @aiml_gltf_node_translation_z(ptr)

; Function Attrs: noinline
define internal ptr @__aiml_f32_to_string(float %0) #0 {
entry:
  %1 = call ptr @malloc(i64 32)
  %2 = fpext float %0 to double
  %3 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_f32, double %2)
  ret ptr %1
}

declare float @aiml_gltf_node_rotation_w(ptr)

declare float @aiml_gltf_node_scale_x(ptr)

declare float @aiml_gltf_node_scale_y(ptr)

declare float @aiml_gltf_node_scale_z(ptr)

declare ptr @aiml_gltf_mesh_name(ptr)

declare i32 @aiml_gltf_mesh_primitive_count(ptr)

declare ptr @aiml_gltf_mesh_primitive(ptr, i32)

declare i32 @aiml_gltf_prim_vertex_count(ptr)

declare i32 @aiml_gltf_prim_index_count(ptr)

declare ptr @aiml_gltf_prim_positions(ptr)

declare ptr @aiml_gltf_prim_indices(ptr)

declare ptr @aiml_gltf_prim_material(ptr)

declare ptr @aiml_gltf_mat_name(ptr)

declare float @aiml_gltf_mat_base_color_r(ptr)

declare float @aiml_gltf_mat_base_color_g(ptr)

declare float @aiml_gltf_mat_base_color_b(ptr)

declare float @aiml_gltf_mat_base_color_a(ptr)

declare float @aiml_gltf_mat_metallic(ptr)

declare float @aiml_gltf_mat_roughness(ptr)

declare ptr @aiml_gltf_mat_alpha_mode(ptr)

declare i32 @aiml_gltf_mat_double_sided(ptr)

attributes #0 = { noinline }
