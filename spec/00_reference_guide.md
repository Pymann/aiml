# AIML Spec Preview

<img src="./aiml_logo.png" alt="AIML Logo" width="220" />

This file is the entry point for the `aiml/spec` directory.

## Context

- The individual chapters in this directory are the formal language and runtime spec.
- This file is only a thematic preview.
- The compact cross-cutting reference currently lives here:
  [aiml_reference_guide.md](aiml_reference_guide.md)

## Language

- [01_primitive_types.md](01_primitive_types.md)
- [02_types.md](02_types.md)
- [03_modifiers.md](03_modifiers.md)
- [04_1_precedence.md](04_1_precedence.md)
- [04_math.md](04_math.md)
- [05_logic.md](05_logic.md)
- [06_loops.md](06_loops.md)
- [07_functions.md](07_functions.md)
- [08_entrypoint.md](08_entrypoint.md)
- [09_1_string_expansions.md](09_1_string_expansions.md)
- [09_strings.md](09_strings.md)
- [10_collections.md](10_collections.md)
- [11_buffer.md](11_buffer.md)
- [12_1_function_pointers.md](12_1_function_pointers.md)
- [12_pointers.md](12_pointers.md)
- [13_cast.md](13_cast.md)
- [14_enums.md](14_enums.md)
- [15_models.md](15_models.md)
- [16_classes.md](16_classes.md)
- [17_classes_examples.md](17_classes_examples.md)
- [18_instantiation.md](18_instantiation.md)
- [19_inheritance.md](19_inheritance.md)
- [20_free_operator.md](20_free_operator.md)
- [21_namespaces.md](21_namespaces.md)
- [22_exceptions.md](22_exceptions.md)

## System and Runtime

- [23_system.md](23_system.md)
- [24_time.md](24_time.md)
- [25_config.md](25_config.md)
- [26_concurrency.md](26_concurrency.md)
- [27_channels.md](27_channels.md)
- [28_channel_buffering.md](28_channel_buffering.md)
- [29_1_sys_bundle.md](29_1_sys_bundle.md)
- [29_2_sys_exec.md](29_2_sys_exec.md)
- [29_serialize.md](29_serialize.md)

## Hot Reload, Symbolism, and Runtime Mechanics

- [30_1_fptable.md](30_1_fptable.md)
- [30_2_trampoline_patching.md](30_2_trampoline_patching.md)
- [30_3_symbolism.md](30_3_symbolism.md)
- [30_4_reload_systems.md](30_4_reload_systems.md)
- [30_hot_reload.md](30_hot_reload.md)
- [30_hot_reload_implement.md](30_hot_reload_implement.md)

## Databases and Storage

- [31_db_integration.md](31_db_integration.md)
- [45_heap_serialization.md](45_heap_serialization.md)

## Networking

- [32_1_network_tls.md](32_1_network_tls.md)
- [32_2_network_http_s.md](32_2_network_http_s.md)
- [32_3_network_rest_api.md](32_3_network_rest_api.md)
- [32_4_network_websockets.md](32_4_network_websockets.md)
- [32_5_network_proxy.md](32_5_network_proxy.md)
- [32_network.md](32_network.md)

## Utility, Security, and Interop

- [33_1_hash.md](33_1_hash.md)
- [33_2_compress.md](33_2_compress.md)
- [33_security.md](33_security.md)
- [67_identity.md](67_identity.md) — base64, LDAP, SAML 2.0
- [34_interop.md](34_interop.md)
- [35_ffi.md](35_ffi.md)

## Architecture and Compiler Internals

- [36_architecture.md](36_architecture.md)
- [37_parsing_and_jit_internals.md](37_parsing_and_jit_internals.md)
- [38_ast_parsing_strategy.md](38_ast_parsing_strategy.md)
- [39_overloading.md](39_overloading.md)
- [40_containerization.md](40_containerization.md)
- [49_static_analysis.md](49_static_analysis.md)
- [49_static_off.md](49_static_off.md)
- [70_performance.md](70_performance.md) — what a performance number has to carry to count

## OS, Math, Engine, and Applications

- [41_os_support.md](41_os_support.md)
- [42_path_type.md](42_path_type.md)
- [42_1_globber.md](42_1_globber.md)
- [43_0_math.md](43_0_math.md)
- [43_1_gltf.md](43_1_gltf.md)
- [43_2_graphics.md](43_2_graphics.md)
- [43_3_vulkan_opengl_explained.md](43_3_vulkan_opengl_explained.md)
- [43_4_scientific_visualization.md](43_4_scientific_visualization.md)
- [44_0_input.md](44_0_input.md)
- [44_1_engine.md](44_1_engine.md)
- [44_2_audio.md](44_2_audio.md)
- [44_3_ui.md](44_3_ui.md)
- [44_4_physics.md](44_4_physics.md)
- [44_5_mirror.md](44_5_mirror.md)
- [44_6_prefabs.md](44_6_prefabs.md)
- [44_7_asset_registry.md](44_7_asset_registry.md)
- [46_log.md](46_log.md)
- [47_thread_safety.md](47_thread_safety.md)
- [48_trace.md](48_trace.md)

## Review and Supporting Material

- [SPEC_REVIEW.md](SPEC_REVIEW.md)
- [llvm_help](llvm_help)
- [statistics](statistics)
