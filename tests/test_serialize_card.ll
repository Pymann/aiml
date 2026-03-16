; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [32 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Card = type { ptr, i32, i32, %map_string_i32, ptr, %list_i32, %list_ActiveAbility, i32, i32, i32, i32, i32, i32, i32, i32, i64 }
%map_string_i32 = type { i32, i32, ptr, ptr, ptr }
%list_i32 = type { i32, i32, ptr }
%list_ActiveAbility = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%ActiveAbility = type { ptr, %list_i32, %"list_list<i32>", %"list_list<i32>" }
%"list_list<i32>" = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_ActiveAbility = private unnamed_addr constant [10 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 1, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 5, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 6, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 8, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 7, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 9, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_BaseCard = private unnamed_addr constant [21 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 7, i32 1, i32 0, i32 88, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 8, i32 9, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 10, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 11, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 12, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 16, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 17, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 19, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 18, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 20, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_Card = private unnamed_addr constant [30 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 16, i32 1, i32 0, i32 128, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 17, i32 18, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 19, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 20, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 88, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 92, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 96, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 100, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 104, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 108, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 112, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 116, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 3, i32 0, i32 0, i32 120, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 21, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 25, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 26, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 28, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 27, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 29, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@0 = private unnamed_addr constant [11 x i8] c"TEST START\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [232 x i8] c"{\22name\22:\22Test\22,\22card_type\22:1,\22mkp\22:1,\22mana_cost\22:{},\22description\22:\22test\22,\22passive_abilities\22:[],\22active_abilities\22:[],\22lp\22:1,\22attack\22:1,\22defense\22:1,\22initiative\22:1,\22moral\22:1,\22movement\22:1,\22spell_type\22:0,\22equip_cost\22:0,\22created_at\22:0}\00", align 1
@fk_name = private unnamed_addr constant [5 x i8] c"name\00", align 1
@fk_card_type = private unnamed_addr constant [10 x i8] c"card_type\00", align 1
@fk_mkp = private unnamed_addr constant [4 x i8] c"mkp\00", align 1
@fk_mana_cost = private unnamed_addr constant [10 x i8] c"mana_cost\00", align 1
@fk_description = private unnamed_addr constant [12 x i8] c"description\00", align 1
@fk_passive_abilities = private unnamed_addr constant [18 x i8] c"passive_abilities\00", align 1
@fk_active_abilities = private unnamed_addr constant [17 x i8] c"active_abilities\00", align 1
@fk_cost_text = private unnamed_addr constant [10 x i8] c"cost_text\00", align 1
@fk_activation_cost = private unnamed_addr constant [16 x i8] c"activation_cost\00", align 1
@fk_trigger = private unnamed_addr constant [8 x i8] c"trigger\00", align 1
@fk_effects = private unnamed_addr constant [8 x i8] c"effects\00", align 1
@fk_lp = private unnamed_addr constant [3 x i8] c"lp\00", align 1
@fk_attack = private unnamed_addr constant [7 x i8] c"attack\00", align 1
@fk_defense = private unnamed_addr constant [8 x i8] c"defense\00", align 1
@fk_initiative = private unnamed_addr constant [11 x i8] c"initiative\00", align 1
@fk_moral = private unnamed_addr constant [6 x i8] c"moral\00", align 1
@fk_movement = private unnamed_addr constant [9 x i8] c"movement\00", align 1
@fk_spell_type = private unnamed_addr constant [11 x i8] c"spell_type\00", align 1
@fk_equip_cost = private unnamed_addr constant [11 x i8] c"equip_cost\00", align 1
@fk_created_at = private unnamed_addr constant [11 x i8] c"created_at\00", align 1
@3 = private unnamed_addr constant [9 x i8] c"c1.name=\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@4 = private unnamed_addr constant [7 x i8] c" type=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@5 = private unnamed_addr constant [5 x i8] c" lp=\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [346 x i8] c"{\22name\22:\22Dragon\22,\22card_type\22:1,\22mkp\22:3,\22mana_cost\22:{\22fire\22:2,\22earth\22:1},\22description\22:\22Big dragon\22,\22passive_abilities\22:[1,10],\22active_abilities\22:[{\22cost_text\22:\222 Fire\22,\22activation_cost\22:[1,2],\22trigger\22:[],\22effects\22:[[10,5,9,1,3]]}],\22lp\22:5,\22attack\22:3,\22defense\22:2,\22initiative\22:4,\22moral\22:3,\22movement\22:2,\22spell_type\22:0,\22equip_cost\22:0,\22created_at\22:0}\00", align 1
@8 = private unnamed_addr constant [9 x i8] c"c2.name=\00", align 1
@9 = private unnamed_addr constant [6 x i8] c" mkp=\00", align 1
@10 = private unnamed_addr constant [5 x i8] c" lp=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [26 x i8] c"c2.passive_abilities.len=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [25 x i8] c"c2.active_abilities.len=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@tn = private unnamed_addr constant [5 x i8] c"Card\00", align 1
@fk_name.5 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@fk_card_type.6 = private unnamed_addr constant [10 x i8] c"card_type\00", align 1
@fk_mkp.7 = private unnamed_addr constant [4 x i8] c"mkp\00", align 1
@fk_mana_cost.8 = private unnamed_addr constant [10 x i8] c"mana_cost\00", align 1
@fk_description.9 = private unnamed_addr constant [12 x i8] c"description\00", align 1
@fk_passive_abilities.10 = private unnamed_addr constant [18 x i8] c"passive_abilities\00", align 1
@fk_active_abilities.11 = private unnamed_addr constant [17 x i8] c"active_abilities\00", align 1
@fk_cost_text.12 = private unnamed_addr constant [10 x i8] c"cost_text\00", align 1
@fk_activation_cost.13 = private unnamed_addr constant [16 x i8] c"activation_cost\00", align 1
@fk_trigger.14 = private unnamed_addr constant [8 x i8] c"trigger\00", align 1
@fk_effects.15 = private unnamed_addr constant [8 x i8] c"effects\00", align 1
@fk_lp.16 = private unnamed_addr constant [3 x i8] c"lp\00", align 1
@fk_attack.17 = private unnamed_addr constant [7 x i8] c"attack\00", align 1
@fk_defense.18 = private unnamed_addr constant [8 x i8] c"defense\00", align 1
@fk_initiative.19 = private unnamed_addr constant [11 x i8] c"initiative\00", align 1
@fk_moral.20 = private unnamed_addr constant [6 x i8] c"moral\00", align 1
@fk_movement.21 = private unnamed_addr constant [9 x i8] c"movement\00", align 1
@fk_spell_type.22 = private unnamed_addr constant [11 x i8] c"spell_type\00", align 1
@fk_equip_cost.23 = private unnamed_addr constant [11 x i8] c"equip_cost\00", align 1
@fk_created_at.24 = private unnamed_addr constant [11 x i8] c"created_at\00", align 1
@16 = private unnamed_addr constant [12 x i8] c"serialized=\00", align 1
@print_null_str.25 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [9 x i8] c"ALL PASS\00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @main]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 1
@__aiml_sname_ActiveAbility = private constant [14 x i8] c"ActiveAbility\00"
@__aiml_sfield_ActiveAbility_cost_text = private constant [10 x i8] c"cost_text\00"
@__aiml_sfield_ActiveAbility_activation_cost = private constant [16 x i8] c"activation_cost\00"
@__aiml_sfield_ActiveAbility_trigger = private constant [8 x i8] c"trigger\00"
@__aiml_sfield_ActiveAbility_effects = private constant [8 x i8] c"effects\00"
@__aiml_sname_BaseCard = private constant [9 x i8] c"BaseCard\00"
@__aiml_sfield_BaseCard_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_BaseCard_card_type = private constant [10 x i8] c"card_type\00"
@__aiml_sfield_BaseCard_mkp = private constant [4 x i8] c"mkp\00"
@__aiml_sfield_BaseCard_mana_cost = private constant [10 x i8] c"mana_cost\00"
@__aiml_sfield_BaseCard_description = private constant [12 x i8] c"description\00"
@__aiml_sfield_BaseCard_passive_abilities = private constant [18 x i8] c"passive_abilities\00"
@__aiml_sfield_BaseCard_active_abilities = private constant [17 x i8] c"active_abilities\00"
@__aiml_sname_Card = private constant [5 x i8] c"Card\00"
@__aiml_sfield_Card_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_Card_card_type = private constant [10 x i8] c"card_type\00"
@__aiml_sfield_Card_mkp = private constant [4 x i8] c"mkp\00"
@__aiml_sfield_Card_mana_cost = private constant [10 x i8] c"mana_cost\00"
@__aiml_sfield_Card_description = private constant [12 x i8] c"description\00"
@__aiml_sfield_Card_passive_abilities = private constant [18 x i8] c"passive_abilities\00"
@__aiml_sfield_Card_active_abilities = private constant [17 x i8] c"active_abilities\00"
@__aiml_sfield_Card_lp = private constant [3 x i8] c"lp\00"
@__aiml_sfield_Card_attack = private constant [7 x i8] c"attack\00"
@__aiml_sfield_Card_defense = private constant [8 x i8] c"defense\00"
@__aiml_sfield_Card_initiative = private constant [11 x i8] c"initiative\00"
@__aiml_sfield_Card_moral = private constant [6 x i8] c"moral\00"
@__aiml_sfield_Card_movement = private constant [9 x i8] c"movement\00"
@__aiml_sfield_Card_spell_type = private constant [11 x i8] c"spell_type\00"
@__aiml_sfield_Card_equip_cost = private constant [11 x i8] c"equip_cost\00"
@__aiml_sfield_Card_created_at = private constant [11 x i8] c"created_at\00"
@__aiml_schemas = global [3 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_ActiveAbility, i32 1, i32 4, i32 56, i32 64, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_cost_text }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_activation_cost }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_trigger }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_effects }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_BaseCard, i32 1, i32 7, i32 88, i32 128, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_BaseCard_name }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_description }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_active_abilities }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Card, i32 1, i32 16, i32 128, i32 128, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Card_name }, %AimlFieldInfo { ptr @__aiml_sfield_Card_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_description }, %AimlFieldInfo { ptr @__aiml_sfield_Card_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_active_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_lp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_attack }, %AimlFieldInfo { ptr @__aiml_sfield_Card_defense }, %AimlFieldInfo { ptr @__aiml_sfield_Card_initiative }, %AimlFieldInfo { ptr @__aiml_sfield_Card_moral }, %AimlFieldInfo { ptr @__aiml_sfield_Card_movement }, %AimlFieldInfo { ptr @__aiml_sfield_Card_spell_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_equip_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_created_at }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 3

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0)
  %j1 = alloca ptr, align 8
  store ptr null, ptr %j1, align 8
  store ptr @2, ptr %j1, align 8
  %c1 = alloca %Card, align 8
  store %Card zeroinitializer, ptr %c1, align 8
  %j1_val = load ptr, ptr %j1, align 8
  %deser_r = call ptr @__aiml_deser_Card(ptr %j1_val)
  %0 = call ptr @memcpy(ptr %c1, ptr %deser_r, i64 128)
  %prop_ptr = getelementptr inbounds %Card, ptr %c1, i32 0, i32 0
  %name_val = load ptr, ptr %prop_ptr, align 8
  %1 = call ptr @__aiml_str_concat(ptr @3, ptr %name_val)
  %2 = call ptr @__aiml_str_concat(ptr %1, ptr @4)
  %prop_ptr1 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 1
  %card_type_val = load i32, ptr %prop_ptr1, align 4
  %3 = call ptr @__aiml_int_to_string(i32 %card_type_val)
  %4 = call ptr @__aiml_str_concat(ptr %2, ptr %3)
  %5 = call ptr @__aiml_str_concat(ptr %4, ptr @5)
  %prop_ptr2 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 7
  %lp_val = load i32, ptr %prop_ptr2, align 4
  %6 = call ptr @__aiml_int_to_string(i32 %lp_val)
  %7 = call ptr @__aiml_str_concat(ptr %5, ptr %6)
  %print_null_check = icmp eq ptr %7, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.1, ptr %7
  %print_call3 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str)
  %j2 = alloca ptr, align 8
  store ptr null, ptr %j2, align 8
  store ptr @7, ptr %j2, align 8
  %c2 = alloca %Card, align 8
  store %Card zeroinitializer, ptr %c2, align 8
  %j2_val = load ptr, ptr %j2, align 8
  %deser_r4 = call ptr @__aiml_deser_Card(ptr %j2_val)
  %8 = call ptr @memcpy(ptr %c2, ptr %deser_r4, i64 128)
  %prop_ptr5 = getelementptr inbounds %Card, ptr %c2, i32 0, i32 0
  %name_val6 = load ptr, ptr %prop_ptr5, align 8
  %9 = call ptr @__aiml_str_concat(ptr @8, ptr %name_val6)
  %10 = call ptr @__aiml_str_concat(ptr %9, ptr @9)
  %prop_ptr7 = getelementptr inbounds %Card, ptr %c2, i32 0, i32 2
  %mkp_val = load i32, ptr %prop_ptr7, align 4
  %11 = call ptr @__aiml_int_to_string(i32 %mkp_val)
  %12 = call ptr @__aiml_str_concat(ptr %10, ptr %11)
  %13 = call ptr @__aiml_str_concat(ptr %12, ptr @10)
  %prop_ptr8 = getelementptr inbounds %Card, ptr %c2, i32 0, i32 7
  %lp_val9 = load i32, ptr %prop_ptr8, align 4
  %14 = call ptr @__aiml_int_to_string(i32 %lp_val9)
  %15 = call ptr @__aiml_str_concat(ptr %13, ptr %14)
  %print_null_check10 = icmp eq ptr %15, null
  %print_safe_str11 = select i1 %print_null_check10, ptr @print_null_str.2, ptr %15
  %print_call12 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str11)
  %chain_gep = getelementptr inbounds %Card, ptr %c2, i32 0, i32 5
  %sizePtr = getelementptr inbounds %GenericList, ptr %chain_gep, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr, align 4
  %16 = call ptr @__aiml_int_to_string(i32 %sizeVal)
  %17 = call ptr @__aiml_str_concat(ptr @12, ptr %16)
  %print_null_check13 = icmp eq ptr %17, null
  %print_safe_str14 = select i1 %print_null_check13, ptr @print_null_str.3, ptr %17
  %print_call15 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str14)
  %chain_gep16 = getelementptr inbounds %Card, ptr %c2, i32 0, i32 6
  %sizePtr17 = getelementptr inbounds %GenericList, ptr %chain_gep16, i32 0, i32 0
  %sizeVal18 = load i32, ptr %sizePtr17, align 4
  %18 = call ptr @__aiml_int_to_string(i32 %sizeVal18)
  %19 = call ptr @__aiml_str_concat(ptr @14, ptr %18)
  %print_null_check19 = icmp eq ptr %19, null
  %print_safe_str20 = select i1 %print_null_check19, ptr @print_null_str.4, ptr %19
  %print_call21 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str20)
  %ser = alloca ptr, align 8
  store ptr null, ptr %ser, align 8
  %ser_r = call ptr @__aiml_ser_Card(ptr %c2)
  store ptr %ser_r, ptr %ser, align 8
  %ser_val = load ptr, ptr %ser, align 8
  %20 = call ptr @__aiml_str_concat(ptr @16, ptr %ser_val)
  %print_null_check22 = icmp eq ptr %20, null
  %print_safe_str23 = select i1 %print_null_check22, ptr @print_null_str.25, ptr %20
  %print_call24 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str23)
  %print_call25 = call i32 (ptr, ...) @printf(ptr @19, ptr @18)
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare i32 @printf(ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_deser_Card(ptr %json) #0 {
entry:
  %r = call ptr @aiml_jr_parse(ptr %json)
  %hp = call ptr @malloc(i64 128)
  call void @llvm.memset.p0.i64(ptr align 8 %hp, i8 0, i64 128, i1 false)
  %fp_name = getelementptr inbounds %Card, ptr %hp, i32 0, i32 0
  %sv = call ptr @aiml_jr_str(ptr %r, ptr @fk_name)
  store ptr %sv, ptr %fp_name, align 8
  %fp_card_type = getelementptr inbounds %Card, ptr %hp, i32 0, i32 1
  %sv1 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_card_type)
  store i32 %sv1, ptr %fp_card_type, align 4
  %fp_mkp = getelementptr inbounds %Card, ptr %hp, i32 0, i32 2
  %sv2 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_mkp)
  store i32 %sv2, ptr %fp_mkp, align 4
  %fp_mana_cost = getelementptr inbounds %Card, ptr %hp, i32 0, i32 3
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 1
  store i32 8, ptr %capPtr, align 4
  %0 = call ptr @calloc(i64 8, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 2
  store ptr %0, ptr %occPtr, align 8
  %1 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  store ptr %1, ptr %keysPtr, align 8
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 8))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  store ptr %2, ptr %valsPtr, align 8
  %cnt = call i32 @aiml_jr_map_count(ptr %r, ptr @fk_mana_cost)
  br label %mana_cost_dmc

mana_cost_dmc:                                    ; preds = %put_end, %entry
  %dmi = phi i32 [ 0, %entry ], [ %ni, %put_end ]
  %3 = icmp slt i32 %dmi, %cnt
  br i1 %3, label %mana_cost_dmb, label %mana_cost_dme

mana_cost_dmb:                                    ; preds = %mana_cost_dmc
  %k = call ptr @aiml_jr_map_key(ptr %r, ptr @fk_mana_cost, i32 %dmi)
  %v = call i32 @aiml_jr_map_val_i32(ptr %r, ptr @fk_mana_cost, i32 %dmi)
  %sizePtr3 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 0
  %capPtr4 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 1
  %size = load i32, ptr %sizePtr3, align 4
  %capacity = load i32, ptr %capPtr4, align 4
  %4 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  %keysBase = load ptr, ptr %4, align 8
  %5 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  %valsBase = load ptr, ptr %5, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

mana_cost_dme:                                    ; preds = %mana_cost_dmc
  %fp_description = getelementptr inbounds %Card, ptr %hp, i32 0, i32 4
  %sv5 = call ptr @aiml_jr_str(ptr %r, ptr @fk_description)
  store ptr %sv5, ptr %fp_description, align 8
  %fp_passive_abilities = getelementptr inbounds %Card, ptr %hp, i32 0, i32 5
  %sizePtr6 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 0
  store i32 0, ptr %sizePtr6, align 4
  %capPtr7 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 1
  store i32 4, ptr %capPtr7, align 4
  %6 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 2
  store ptr %6, ptr %dataPtr, align 8
  %cnt8 = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_passive_abilities)
  br label %passive_abilities_dlc

put_loop:                                         ; preds = %put_next, %mana_cost_dmb
  %7 = load i32, ptr %i_put, align 4
  %8 = icmp slt i32 %7, %size
  br i1 %8, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %9 = getelementptr inbounds ptr, ptr %keysBase, i32 %7
  %10 = load ptr, ptr %9, align 8
  %11 = call i32 @strcmp(ptr %k, ptr %10)
  %str_eq = icmp eq i32 %11, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %12 = getelementptr inbounds i32, ptr %valsBase, i32 %7
  store i32 %v, ptr %12, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %13 = add i32 %7, 1
  store i32 %13, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %14 = icmp eq i32 %size, %capacity
  br i1 %14, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %ni = add i32 %dmi, 1
  br label %mana_cost_dmc

put_grow:                                         ; preds = %put_not_found
  %15 = mul i32 %capacity, 2
  store i32 %15, ptr %capPtr4, align 4
  %16 = zext i32 %15 to i64
  %17 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %16
  %18 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  %19 = load ptr, ptr %18, align 8
  %20 = call ptr @realloc(ptr %19, i64 %17)
  store ptr %20, ptr %18, align 8
  %21 = zext i32 %15 to i64
  %22 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %21
  %23 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  %24 = load ptr, ptr %23, align 8
  %25 = call ptr @realloc(ptr %24, i64 %22)
  store ptr %25, ptr %23, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %26 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  %27 = load ptr, ptr %26, align 8
  %28 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  %29 = load ptr, ptr %28, align 8
  %30 = getelementptr inbounds ptr, ptr %27, i32 %size
  store ptr %k, ptr %30, align 8
  %31 = getelementptr inbounds i32, ptr %29, i32 %size
  store i32 %v, ptr %31, align 4
  %32 = add i32 %size, 1
  store i32 %32, ptr %sizePtr3, align 4
  br label %put_end

passive_abilities_dlc:                            ; preds = %cont, %mana_cost_dme
  %dli = phi i32 [ 0, %mana_cost_dme ], [ %ni15, %cont ]
  %33 = icmp slt i32 %dli, %cnt8
  br i1 %33, label %passive_abilities_dlb, label %passive_abilities_dle

passive_abilities_dlb:                            ; preds = %passive_abilities_dlc
  %ae = call i32 @aiml_jr_arr_i32(ptr %r, ptr @fk_passive_abilities, i32 %dli)
  %sizePtr9 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 0
  %capPtr10 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 1
  %size11 = load i32, ptr %sizePtr9, align 4
  %capacity12 = load i32, ptr %capPtr10, align 4
  %34 = icmp eq i32 %size11, %capacity12
  br i1 %34, label %grow, label %cont

passive_abilities_dle:                            ; preds = %passive_abilities_dlc
  %fp_active_abilities = getelementptr inbounds %Card, ptr %hp, i32 0, i32 6
  %sizePtr16 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 0
  store i32 0, ptr %sizePtr16, align 4
  %capPtr17 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 1
  store i32 4, ptr %capPtr17, align 4
  %35 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%ActiveAbility, ptr null, i32 1) to i64), i64 4))
  %dataPtr18 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 2
  store ptr %35, ptr %dataPtr18, align 8
  %cnt19 = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_active_abilities)
  br label %active_abilities_dlc

grow:                                             ; preds = %passive_abilities_dlb
  %36 = mul i32 %capacity12, 2
  store i32 %36, ptr %capPtr10, align 4
  %dataPtr13 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 2
  %37 = load ptr, ptr %dataPtr13, align 8
  %38 = zext i32 %36 to i64
  %39 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %38
  %40 = call ptr @realloc(ptr %37, i64 %39)
  store ptr %40, ptr %dataPtr13, align 8
  br label %cont

cont:                                             ; preds = %grow, %passive_abilities_dlb
  %dataPtr14 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr14, align 8
  %41 = getelementptr inbounds i32, ptr %dataBase, i32 %size11
  store i32 %ae, ptr %41, align 4
  %42 = add i32 %size11, 1
  store i32 %42, ptr %sizePtr9, align 4
  %ni15 = add i32 %dli, 1
  br label %passive_abilities_dlc

active_abilities_dlc:                             ; preds = %active_abilities_srdone, %passive_abilities_dle
  %dli20 = phi i32 [ 0, %passive_abilities_dle ], [ %ni108, %active_abilities_srdone ]
  %43 = icmp slt i32 %dli20, %cnt19
  br i1 %43, label %active_abilities_dlb, label %active_abilities_dle

active_abilities_dlb:                             ; preds = %active_abilities_dlc
  %sr = call ptr @aiml_jr_arr_sub(ptr %r, ptr @fk_active_abilities, i32 %dli20)
  %44 = icmp ne ptr %sr, null
  br i1 %44, label %active_abilities_srok, label %active_abilities_srdone

active_abilities_dle:                             ; preds = %active_abilities_dlc
  %fp_lp = getelementptr inbounds %Card, ptr %hp, i32 0, i32 7
  %sv109 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_lp)
  store i32 %sv109, ptr %fp_lp, align 4
  %fp_attack = getelementptr inbounds %Card, ptr %hp, i32 0, i32 8
  %sv110 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_attack)
  store i32 %sv110, ptr %fp_attack, align 4
  %fp_defense = getelementptr inbounds %Card, ptr %hp, i32 0, i32 9
  %sv111 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_defense)
  store i32 %sv111, ptr %fp_defense, align 4
  %fp_initiative = getelementptr inbounds %Card, ptr %hp, i32 0, i32 10
  %sv112 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_initiative)
  store i32 %sv112, ptr %fp_initiative, align 4
  %fp_moral = getelementptr inbounds %Card, ptr %hp, i32 0, i32 11
  %sv113 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_moral)
  store i32 %sv113, ptr %fp_moral, align 4
  %fp_movement = getelementptr inbounds %Card, ptr %hp, i32 0, i32 12
  %sv114 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_movement)
  store i32 %sv114, ptr %fp_movement, align 4
  %fp_spell_type = getelementptr inbounds %Card, ptr %hp, i32 0, i32 13
  %sv115 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_spell_type)
  store i32 %sv115, ptr %fp_spell_type, align 4
  %fp_equip_cost = getelementptr inbounds %Card, ptr %hp, i32 0, i32 14
  %sv116 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_equip_cost)
  store i32 %sv116, ptr %fp_equip_cost, align 4
  %fp_created_at = getelementptr inbounds %Card, ptr %hp, i32 0, i32 15
  %sv117 = call i64 @aiml_jr_i64(ptr %r, ptr @fk_created_at)
  store i64 %sv117, ptr %fp_created_at, align 8
  call void @aiml_jr_free(ptr %r)
  ret ptr %hp

active_abilities_srok:                            ; preds = %active_abilities_dlb
  %stmp = alloca %ActiveAbility, align 8
  call void @llvm.memset.p0.i64(ptr align 8 %stmp, i8 0, i64 56, i1 false)
  %fp_cost_text = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 0
  %sv21 = call ptr @aiml_jr_str(ptr %sr, ptr @fk_cost_text)
  store ptr %sv21, ptr %fp_cost_text, align 8
  %fp_activation_cost = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 1
  %sizePtr22 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 0
  store i32 0, ptr %sizePtr22, align 4
  %capPtr23 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 1
  store i32 4, ptr %capPtr23, align 4
  %45 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr24 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  store ptr %45, ptr %dataPtr24, align 8
  %cnt25 = call i32 @aiml_jr_arr_len(ptr %sr, ptr @fk_activation_cost)
  br label %activation_cost_dlc

active_abilities_srdone:                          ; preds = %cont104, %active_abilities_dlb
  %ni108 = add i32 %dli20, 1
  br label %active_abilities_dlc

activation_cost_dlc:                              ; preds = %cont33, %active_abilities_srok
  %dli26 = phi i32 [ 0, %active_abilities_srok ], [ %ni37, %cont33 ]
  %46 = icmp slt i32 %dli26, %cnt25
  br i1 %46, label %activation_cost_dlb, label %activation_cost_dle

activation_cost_dlb:                              ; preds = %activation_cost_dlc
  %ae27 = call i32 @aiml_jr_arr_i32(ptr %sr, ptr @fk_activation_cost, i32 %dli26)
  %sizePtr28 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 0
  %capPtr29 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 1
  %size30 = load i32, ptr %sizePtr28, align 4
  %capacity31 = load i32, ptr %capPtr29, align 4
  %47 = icmp eq i32 %size30, %capacity31
  br i1 %47, label %grow32, label %cont33

activation_cost_dle:                              ; preds = %activation_cost_dlc
  %fp_trigger = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 2
  %sizePtr38 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 0
  store i32 0, ptr %sizePtr38, align 4
  %capPtr39 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 1
  store i32 4, ptr %capPtr39, align 4
  %48 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr40 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  store ptr %48, ptr %dataPtr40, align 8
  %cnt41 = call i32 @aiml_jr_arr_len(ptr %sr, ptr @fk_trigger)
  br label %trigger_dlc

grow32:                                           ; preds = %activation_cost_dlb
  %49 = mul i32 %capacity31, 2
  store i32 %49, ptr %capPtr29, align 4
  %dataPtr34 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  %50 = load ptr, ptr %dataPtr34, align 8
  %51 = zext i32 %49 to i64
  %52 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %51
  %53 = call ptr @realloc(ptr %50, i64 %52)
  store ptr %53, ptr %dataPtr34, align 8
  br label %cont33

cont33:                                           ; preds = %grow32, %activation_cost_dlb
  %dataPtr35 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  %dataBase36 = load ptr, ptr %dataPtr35, align 8
  %54 = getelementptr inbounds i32, ptr %dataBase36, i32 %size30
  store i32 %ae27, ptr %54, align 4
  %55 = add i32 %size30, 1
  store i32 %55, ptr %sizePtr28, align 4
  %ni37 = add i32 %dli26, 1
  br label %activation_cost_dlc

trigger_dlc:                                      ; preds = %cont60, %activation_cost_dle
  %dli42 = phi i32 [ 0, %activation_cost_dle ], [ %ni64, %cont60 ]
  %56 = icmp slt i32 %dli42, %cnt41
  br i1 %56, label %trigger_dlb, label %trigger_dle

trigger_dlb:                                      ; preds = %trigger_dlc
  %ila = alloca %list_i32, align 8
  %sizePtr43 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  store i32 0, ptr %sizePtr43, align 4
  %capPtr44 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  store i32 4, ptr %capPtr44, align 4
  %57 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr45 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  store ptr %57, ptr %dataPtr45, align 8
  %isr = call ptr @aiml_jr_arr_sub(ptr %sr, ptr @fk_trigger, i32 %dli42)
  %58 = icmp ne ptr %isr, null
  br i1 %58, label %trigger_isrok, label %trigger_isrdone

trigger_dle:                                      ; preds = %trigger_dlc
  %fp_effects = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 3
  %sizePtr65 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 0
  store i32 0, ptr %sizePtr65, align 4
  %capPtr66 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 1
  store i32 4, ptr %capPtr66, align 4
  %59 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr67 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  store ptr %59, ptr %dataPtr67, align 8
  %cnt68 = call i32 @aiml_jr_arr_len(ptr %sr, ptr @fk_effects)
  br label %effects_dlc

trigger_isrok:                                    ; preds = %trigger_dlb
  %icnt = call i32 @aiml_jr_val_len(ptr %isr)
  br label %trigger_ilc

trigger_isrdone:                                  ; preds = %trigger_ile, %trigger_dlb
  %ilval = load %list_i32, ptr %ila, align 8
  %sizePtr55 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 0
  %capPtr56 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 1
  %size57 = load i32, ptr %sizePtr55, align 4
  %capacity58 = load i32, ptr %capPtr56, align 4
  %60 = icmp eq i32 %size57, %capacity58
  br i1 %60, label %grow59, label %cont60

trigger_ilc:                                      ; preds = %cont51, %trigger_isrok
  %iidx = phi i32 [ 0, %trigger_isrok ], [ %ini, %cont51 ]
  %61 = icmp slt i32 %iidx, %icnt
  br i1 %61, label %trigger_ilb, label %trigger_ile

trigger_ilb:                                      ; preds = %trigger_ilc
  %ve = call i32 @aiml_jr_val_i32(ptr %isr, i32 %iidx)
  %sizePtr46 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  %capPtr47 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  %size48 = load i32, ptr %sizePtr46, align 4
  %capacity49 = load i32, ptr %capPtr47, align 4
  %62 = icmp eq i32 %size48, %capacity49
  br i1 %62, label %grow50, label %cont51

trigger_ile:                                      ; preds = %trigger_ilc
  call void @aiml_jr_free(ptr %isr)
  br label %trigger_isrdone

grow50:                                           ; preds = %trigger_ilb
  %63 = mul i32 %capacity49, 2
  store i32 %63, ptr %capPtr47, align 4
  %dataPtr52 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %64 = load ptr, ptr %dataPtr52, align 8
  %65 = zext i32 %63 to i64
  %66 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %65
  %67 = call ptr @realloc(ptr %64, i64 %66)
  store ptr %67, ptr %dataPtr52, align 8
  br label %cont51

cont51:                                           ; preds = %grow50, %trigger_ilb
  %dataPtr53 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %dataBase54 = load ptr, ptr %dataPtr53, align 8
  %68 = getelementptr inbounds i32, ptr %dataBase54, i32 %size48
  store i32 %ve, ptr %68, align 4
  %69 = add i32 %size48, 1
  store i32 %69, ptr %sizePtr46, align 4
  %ini = add i32 %iidx, 1
  br label %trigger_ilc

grow59:                                           ; preds = %trigger_isrdone
  %70 = mul i32 %capacity58, 2
  store i32 %70, ptr %capPtr56, align 4
  %dataPtr61 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  %71 = load ptr, ptr %dataPtr61, align 8
  %72 = zext i32 %70 to i64
  %73 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), %72
  %74 = call ptr @realloc(ptr %71, i64 %73)
  store ptr %74, ptr %dataPtr61, align 8
  br label %cont60

cont60:                                           ; preds = %grow59, %trigger_isrdone
  %dataPtr62 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  %dataBase63 = load ptr, ptr %dataPtr62, align 8
  %75 = getelementptr inbounds %list_i32, ptr %dataBase63, i32 %size57
  store %list_i32 %ilval, ptr %75, align 8
  %76 = add i32 %size57, 1
  store i32 %76, ptr %sizePtr55, align 4
  %ni64 = add i32 %dli42, 1
  br label %trigger_dlc

effects_dlc:                                      ; preds = %cont94, %trigger_dle
  %dli69 = phi i32 [ 0, %trigger_dle ], [ %ni98, %cont94 ]
  %77 = icmp slt i32 %dli69, %cnt68
  br i1 %77, label %effects_dlb, label %effects_dle

effects_dlb:                                      ; preds = %effects_dlc
  %ila70 = alloca %list_i32, align 8
  %sizePtr71 = getelementptr inbounds %GenericList, ptr %ila70, i32 0, i32 0
  store i32 0, ptr %sizePtr71, align 4
  %capPtr72 = getelementptr inbounds %GenericList, ptr %ila70, i32 0, i32 1
  store i32 4, ptr %capPtr72, align 4
  %78 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr73 = getelementptr inbounds %GenericList, ptr %ila70, i32 0, i32 2
  store ptr %78, ptr %dataPtr73, align 8
  %isr74 = call ptr @aiml_jr_arr_sub(ptr %sr, ptr @fk_effects, i32 %dli69)
  %79 = icmp ne ptr %isr74, null
  br i1 %79, label %effects_isrok, label %effects_isrdone

effects_dle:                                      ; preds = %effects_dlc
  %sval = load %ActiveAbility, ptr %stmp, align 8
  %sizePtr99 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 0
  %capPtr100 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 1
  %size101 = load i32, ptr %sizePtr99, align 4
  %capacity102 = load i32, ptr %capPtr100, align 4
  %80 = icmp eq i32 %size101, %capacity102
  br i1 %80, label %grow103, label %cont104

effects_isrok:                                    ; preds = %effects_dlb
  %icnt75 = call i32 @aiml_jr_val_len(ptr %isr74)
  br label %effects_ilc

effects_isrdone:                                  ; preds = %effects_ile, %effects_dlb
  %ilval88 = load %list_i32, ptr %ila70, align 8
  %sizePtr89 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 0
  %capPtr90 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 1
  %size91 = load i32, ptr %sizePtr89, align 4
  %capacity92 = load i32, ptr %capPtr90, align 4
  %81 = icmp eq i32 %size91, %capacity92
  br i1 %81, label %grow93, label %cont94

effects_ilc:                                      ; preds = %cont83, %effects_isrok
  %iidx76 = phi i32 [ 0, %effects_isrok ], [ %ini87, %cont83 ]
  %82 = icmp slt i32 %iidx76, %icnt75
  br i1 %82, label %effects_ilb, label %effects_ile

effects_ilb:                                      ; preds = %effects_ilc
  %ve77 = call i32 @aiml_jr_val_i32(ptr %isr74, i32 %iidx76)
  %sizePtr78 = getelementptr inbounds %GenericList, ptr %ila70, i32 0, i32 0
  %capPtr79 = getelementptr inbounds %GenericList, ptr %ila70, i32 0, i32 1
  %size80 = load i32, ptr %sizePtr78, align 4
  %capacity81 = load i32, ptr %capPtr79, align 4
  %83 = icmp eq i32 %size80, %capacity81
  br i1 %83, label %grow82, label %cont83

effects_ile:                                      ; preds = %effects_ilc
  call void @aiml_jr_free(ptr %isr74)
  br label %effects_isrdone

grow82:                                           ; preds = %effects_ilb
  %84 = mul i32 %capacity81, 2
  store i32 %84, ptr %capPtr79, align 4
  %dataPtr84 = getelementptr inbounds %GenericList, ptr %ila70, i32 0, i32 2
  %85 = load ptr, ptr %dataPtr84, align 8
  %86 = zext i32 %84 to i64
  %87 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %86
  %88 = call ptr @realloc(ptr %85, i64 %87)
  store ptr %88, ptr %dataPtr84, align 8
  br label %cont83

cont83:                                           ; preds = %grow82, %effects_ilb
  %dataPtr85 = getelementptr inbounds %GenericList, ptr %ila70, i32 0, i32 2
  %dataBase86 = load ptr, ptr %dataPtr85, align 8
  %89 = getelementptr inbounds i32, ptr %dataBase86, i32 %size80
  store i32 %ve77, ptr %89, align 4
  %90 = add i32 %size80, 1
  store i32 %90, ptr %sizePtr78, align 4
  %ini87 = add i32 %iidx76, 1
  br label %effects_ilc

grow93:                                           ; preds = %effects_isrdone
  %91 = mul i32 %capacity92, 2
  store i32 %91, ptr %capPtr90, align 4
  %dataPtr95 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  %92 = load ptr, ptr %dataPtr95, align 8
  %93 = zext i32 %91 to i64
  %94 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), %93
  %95 = call ptr @realloc(ptr %92, i64 %94)
  store ptr %95, ptr %dataPtr95, align 8
  br label %cont94

cont94:                                           ; preds = %grow93, %effects_isrdone
  %dataPtr96 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  %dataBase97 = load ptr, ptr %dataPtr96, align 8
  %96 = getelementptr inbounds %list_i32, ptr %dataBase97, i32 %size91
  store %list_i32 %ilval88, ptr %96, align 8
  %97 = add i32 %size91, 1
  store i32 %97, ptr %sizePtr89, align 4
  %ni98 = add i32 %dli69, 1
  br label %effects_dlc

grow103:                                          ; preds = %effects_dle
  %98 = mul i32 %capacity102, 2
  store i32 %98, ptr %capPtr100, align 4
  %dataPtr105 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 2
  %99 = load ptr, ptr %dataPtr105, align 8
  %100 = zext i32 %98 to i64
  %101 = mul i64 ptrtoint (ptr getelementptr inbounds (%ActiveAbility, ptr null, i32 1) to i64), %100
  %102 = call ptr @realloc(ptr %99, i64 %101)
  store ptr %102, ptr %dataPtr105, align 8
  br label %cont104

cont104:                                          ; preds = %grow103, %effects_dle
  %dataPtr106 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 2
  %dataBase107 = load ptr, ptr %dataPtr106, align 8
  %103 = getelementptr inbounds %ActiveAbility, ptr %dataBase107, i32 %size101
  store %ActiveAbility %sval, ptr %103, align 8
  %104 = add i32 %size101, 1
  store i32 %104, ptr %sizePtr99, align 4
  call void @aiml_jr_free(ptr %sr)
  br label %active_abilities_srdone
}

declare ptr @aiml_jr_parse(ptr)

declare ptr @malloc(i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #1

declare ptr @aiml_jr_str(ptr, ptr)

declare i32 @aiml_jr_i32(ptr, ptr)

declare ptr @calloc(i64, i64)

declare i32 @aiml_jr_map_count(ptr, ptr)

declare ptr @aiml_jr_map_key(ptr, ptr, i32)

declare i32 @aiml_jr_map_val_i32(ptr, ptr, i32)

declare i32 @strcmp(ptr, ptr)

declare ptr @realloc(ptr, i64)

declare i32 @aiml_jr_arr_len(ptr, ptr)

declare i32 @aiml_jr_arr_i32(ptr, ptr, i32)

declare ptr @aiml_jr_arr_sub(ptr, ptr, i32)

declare i32 @aiml_jr_val_len(ptr)

declare i32 @aiml_jr_val_i32(ptr, i32)

declare void @aiml_jr_free(ptr)

declare i64 @aiml_jr_i64(ptr, ptr)

declare ptr @memcpy(ptr, ptr, i64)

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

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

; Function Attrs: noinline
define internal ptr @__aiml_ser_Card(ptr %sp) #0 {
entry:
  %w = call ptr @aiml_jw_new()
  call void @aiml_jw_set_type(ptr %w, ptr @tn)
  %fp = getelementptr inbounds %Card, ptr %sp, i32 0, i32 0
  %sv = load ptr, ptr %fp, align 8
  call void @aiml_jw_set_str(ptr %w, ptr @fk_name.5, ptr %sv)
  %fp1 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 1
  %v = load i32, ptr %fp1, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_card_type.6, i32 %v)
  %fp2 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 2
  %v3 = load i32, ptr %fp2, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_mkp.7, i32 %v3)
  %mfp = getelementptr inbounds %Card, ptr %sp, i32 0, i32 3
  %mcg = getelementptr inbounds %map_string_i32, ptr %mfp, i32 0, i32 1
  %mcap = load i32, ptr %mcg, align 4
  %mog = getelementptr inbounds %map_string_i32, ptr %mfp, i32 0, i32 2
  %mocc = load ptr, ptr %mog, align 8
  %mkg = getelementptr inbounds %map_string_i32, ptr %mfp, i32 0, i32 3
  %mkeys = load ptr, ptr %mkg, align 8
  %mvg = getelementptr inbounds %map_string_i32, ptr %mfp, i32 0, i32 4
  %mvals = load ptr, ptr %mvg, align 8
  call void @aiml_jw_obj_begin(ptr %w, ptr @fk_mana_cost.8)
  br label %mana_cost_mlc

mana_cost_mlc:                                    ; preds = %mana_cost_mse, %entry
  %mli = phi i32 [ 0, %entry ], [ %mni, %mana_cost_mse ]
  %0 = icmp slt i32 %mli, %mcap
  br i1 %0, label %mana_cost_mlb, label %mana_cost_mle

mana_cost_mlb:                                    ; preds = %mana_cost_mlc
  %oe = getelementptr i8, ptr %mocc, i32 %mli
  %oc2 = load i8, ptr %oe, align 1
  %1 = icmp ne i8 %oc2, 0
  br i1 %1, label %mana_cost_mwe, label %mana_cost_mse

mana_cost_mwe:                                    ; preds = %mana_cost_mlb
  %kep = getelementptr ptr, ptr %mkeys, i32 %mli
  %ks = load ptr, ptr %kep, align 8
  %vep = getelementptr i32, ptr %mvals, i32 %mli
  %vv = load i32, ptr %vep, align 4
  call void @aiml_jw_map_i32(ptr %w, ptr %ks, i32 %vv)
  br label %mana_cost_mse

mana_cost_mse:                                    ; preds = %mana_cost_mwe, %mana_cost_mlb
  %mni = add i32 %mli, 1
  br label %mana_cost_mlc

mana_cost_mle:                                    ; preds = %mana_cost_mlc
  call void @aiml_jw_obj_end(ptr %w)
  %fp4 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 4
  %sv5 = load ptr, ptr %fp4, align 8
  call void @aiml_jw_set_str(ptr %w, ptr @fk_description.9, ptr %sv5)
  %lfp = getelementptr inbounds %Card, ptr %sp, i32 0, i32 5
  %lsg = getelementptr inbounds %list_i32, ptr %lfp, i32 0, i32 0
  %lsz = load i32, ptr %lsg, align 4
  %ldg = getelementptr inbounds %list_i32, ptr %lfp, i32 0, i32 2
  %ldp = load ptr, ptr %ldg, align 8
  call void @aiml_jw_arr_begin(ptr %w, ptr @fk_passive_abilities.10)
  br label %passive_abilities_alc

passive_abilities_alc:                            ; preds = %passive_abilities_alb, %mana_cost_mle
  %ali = phi i32 [ 0, %mana_cost_mle ], [ %ani, %passive_abilities_alb ]
  %2 = icmp slt i32 %ali, %lsz
  br i1 %2, label %passive_abilities_alb, label %passive_abilities_ale

passive_abilities_alb:                            ; preds = %passive_abilities_alc
  %aep = getelementptr i32, ptr %ldp, i32 %ali
  %ev = load i32, ptr %aep, align 4
  call void @aiml_jw_arr_i32(ptr %w, i32 %ev)
  %ani = add i32 %ali, 1
  br label %passive_abilities_alc

passive_abilities_ale:                            ; preds = %passive_abilities_alc
  call void @aiml_jw_arr_end(ptr %w)
  %lfp6 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 6
  %lsg7 = getelementptr inbounds %list_ActiveAbility, ptr %lfp6, i32 0, i32 0
  %lsz8 = load i32, ptr %lsg7, align 4
  %ldg9 = getelementptr inbounds %list_ActiveAbility, ptr %lfp6, i32 0, i32 2
  %ldp10 = load ptr, ptr %ldg9, align 8
  call void @aiml_jw_arr_begin(ptr %w, ptr @fk_active_abilities.11)
  br label %active_abilities_alc

active_abilities_alc:                             ; preds = %effects_ale, %passive_abilities_ale
  %ali11 = phi i32 [ 0, %passive_abilities_ale ], [ %ani48, %effects_ale ]
  %3 = icmp slt i32 %ali11, %lsz8
  br i1 %3, label %active_abilities_alb, label %active_abilities_ale

active_abilities_alb:                             ; preds = %active_abilities_alc
  %aep12 = getelementptr %ActiveAbility, ptr %ldp10, i32 %ali11
  call void @aiml_jw_arr_obj_begin(ptr %w)
  %fp13 = getelementptr inbounds %ActiveAbility, ptr %aep12, i32 0, i32 0
  %sv14 = load ptr, ptr %fp13, align 8
  call void @aiml_jw_set_str(ptr %w, ptr @fk_cost_text.12, ptr %sv14)
  %lfp15 = getelementptr inbounds %ActiveAbility, ptr %aep12, i32 0, i32 1
  %lsg16 = getelementptr inbounds %list_i32, ptr %lfp15, i32 0, i32 0
  %lsz17 = load i32, ptr %lsg16, align 4
  %ldg18 = getelementptr inbounds %list_i32, ptr %lfp15, i32 0, i32 2
  %ldp19 = load ptr, ptr %ldg18, align 8
  call void @aiml_jw_arr_begin(ptr %w, ptr @fk_activation_cost.13)
  br label %activation_cost_alc

active_abilities_ale:                             ; preds = %active_abilities_alc
  call void @aiml_jw_arr_end(ptr %w)
  %fp49 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 7
  %v50 = load i32, ptr %fp49, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_lp.16, i32 %v50)
  %fp51 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 8
  %v52 = load i32, ptr %fp51, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_attack.17, i32 %v52)
  %fp53 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 9
  %v54 = load i32, ptr %fp53, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_defense.18, i32 %v54)
  %fp55 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 10
  %v56 = load i32, ptr %fp55, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_initiative.19, i32 %v56)
  %fp57 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 11
  %v58 = load i32, ptr %fp57, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_moral.20, i32 %v58)
  %fp59 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 12
  %v60 = load i32, ptr %fp59, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_movement.21, i32 %v60)
  %fp61 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 13
  %v62 = load i32, ptr %fp61, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_spell_type.22, i32 %v62)
  %fp63 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 14
  %v64 = load i32, ptr %fp63, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_equip_cost.23, i32 %v64)
  %fp65 = getelementptr inbounds %Card, ptr %sp, i32 0, i32 15
  %v66 = load i64, ptr %fp65, align 8
  call void @aiml_jw_set_i64(ptr %w, ptr @fk_created_at.24, i64 %v66)
  %json = call ptr @aiml_jw_done(ptr %w)
  ret ptr %json

activation_cost_alc:                              ; preds = %activation_cost_alb, %active_abilities_alb
  %ali20 = phi i32 [ 0, %active_abilities_alb ], [ %ani23, %activation_cost_alb ]
  %4 = icmp slt i32 %ali20, %lsz17
  br i1 %4, label %activation_cost_alb, label %activation_cost_ale

activation_cost_alb:                              ; preds = %activation_cost_alc
  %aep21 = getelementptr i32, ptr %ldp19, i32 %ali20
  %ev22 = load i32, ptr %aep21, align 4
  call void @aiml_jw_arr_i32(ptr %w, i32 %ev22)
  %ani23 = add i32 %ali20, 1
  br label %activation_cost_alc

activation_cost_ale:                              ; preds = %activation_cost_alc
  call void @aiml_jw_arr_end(ptr %w)
  %lfp24 = getelementptr inbounds %ActiveAbility, ptr %aep12, i32 0, i32 2
  %lsg25 = getelementptr inbounds %"list_list<i32>", ptr %lfp24, i32 0, i32 0
  %lsz26 = load i32, ptr %lsg25, align 4
  %ldg27 = getelementptr inbounds %"list_list<i32>", ptr %lfp24, i32 0, i32 2
  %ldp28 = load ptr, ptr %ldg27, align 8
  call void @aiml_jw_arr_begin(ptr %w, ptr @fk_trigger.14)
  br label %trigger_alc

trigger_alc:                                      ; preds = %trigger_ile, %activation_cost_ale
  %ali29 = phi i32 [ 0, %activation_cost_ale ], [ %ani31, %trigger_ile ]
  %5 = icmp slt i32 %ali29, %lsz26
  br i1 %5, label %trigger_alb, label %trigger_ale

trigger_alb:                                      ; preds = %trigger_alc
  %aep30 = getelementptr %list_i32, ptr %ldp28, i32 %ali29
  call void @aiml_jw_arr_arr_begin(ptr %w)
  %isg = getelementptr inbounds %list_i32, ptr %aep30, i32 0, i32 0
  %isz = load i32, ptr %isg, align 4
  %idg = getelementptr inbounds %list_i32, ptr %aep30, i32 0, i32 2
  %idp = load ptr, ptr %idg, align 8
  br label %trigger_ilc

trigger_ale:                                      ; preds = %trigger_alc
  call void @aiml_jw_arr_end(ptr %w)
  %lfp32 = getelementptr inbounds %ActiveAbility, ptr %aep12, i32 0, i32 3
  %lsg33 = getelementptr inbounds %"list_list<i32>", ptr %lfp32, i32 0, i32 0
  %lsz34 = load i32, ptr %lsg33, align 4
  %ldg35 = getelementptr inbounds %"list_list<i32>", ptr %lfp32, i32 0, i32 2
  %ldp36 = load ptr, ptr %ldg35, align 8
  call void @aiml_jw_arr_begin(ptr %w, ptr @fk_effects.15)
  br label %effects_alc

trigger_ilc:                                      ; preds = %trigger_ilb, %trigger_alb
  %iidx = phi i32 [ 0, %trigger_alb ], [ %ini, %trigger_ilb ]
  %6 = icmp slt i32 %iidx, %isz
  br i1 %6, label %trigger_ilb, label %trigger_ile

trigger_ilb:                                      ; preds = %trigger_ilc
  %iep = getelementptr i32, ptr %idp, i32 %iidx
  %iev = load i32, ptr %iep, align 4
  call void @aiml_jw_arr_i32(ptr %w, i32 %iev)
  %ini = add i32 %iidx, 1
  br label %trigger_ilc

trigger_ile:                                      ; preds = %trigger_ilc
  call void @aiml_jw_arr_arr_end(ptr %w)
  %ani31 = add i32 %ali29, 1
  br label %trigger_alc

effects_alc:                                      ; preds = %effects_ile, %trigger_ale
  %ali37 = phi i32 [ 0, %trigger_ale ], [ %ani47, %effects_ile ]
  %7 = icmp slt i32 %ali37, %lsz34
  br i1 %7, label %effects_alb, label %effects_ale

effects_alb:                                      ; preds = %effects_alc
  %aep38 = getelementptr %list_i32, ptr %ldp36, i32 %ali37
  call void @aiml_jw_arr_arr_begin(ptr %w)
  %isg39 = getelementptr inbounds %list_i32, ptr %aep38, i32 0, i32 0
  %isz40 = load i32, ptr %isg39, align 4
  %idg41 = getelementptr inbounds %list_i32, ptr %aep38, i32 0, i32 2
  %idp42 = load ptr, ptr %idg41, align 8
  br label %effects_ilc

effects_ale:                                      ; preds = %effects_alc
  call void @aiml_jw_arr_end(ptr %w)
  call void @aiml_jw_arr_obj_end(ptr %w)
  %ani48 = add i32 %ali11, 1
  br label %active_abilities_alc

effects_ilc:                                      ; preds = %effects_ilb, %effects_alb
  %iidx43 = phi i32 [ 0, %effects_alb ], [ %ini46, %effects_ilb ]
  %8 = icmp slt i32 %iidx43, %isz40
  br i1 %8, label %effects_ilb, label %effects_ile

effects_ilb:                                      ; preds = %effects_ilc
  %iep44 = getelementptr i32, ptr %idp42, i32 %iidx43
  %iev45 = load i32, ptr %iep44, align 4
  call void @aiml_jw_arr_i32(ptr %w, i32 %iev45)
  %ini46 = add i32 %iidx43, 1
  br label %effects_ilc

effects_ile:                                      ; preds = %effects_ilc
  call void @aiml_jw_arr_arr_end(ptr %w)
  %ani47 = add i32 %ali37, 1
  br label %effects_alc
}

declare ptr @aiml_jw_new()

declare void @aiml_jw_set_type(ptr, ptr)

declare void @aiml_jw_set_str(ptr, ptr, ptr)

declare void @aiml_jw_set_i32(ptr, ptr, i32)

declare void @aiml_jw_obj_begin(ptr, ptr)

declare void @aiml_jw_map_i32(ptr, ptr, i32)

declare void @aiml_jw_obj_end(ptr)

declare void @aiml_jw_arr_begin(ptr, ptr)

declare void @aiml_jw_arr_i32(ptr, i32)

declare void @aiml_jw_arr_end(ptr)

declare void @aiml_jw_arr_obj_begin(ptr)

declare void @aiml_jw_arr_arr_begin(ptr)

declare void @aiml_jw_arr_arr_end(ptr)

declare void @aiml_jw_arr_obj_end(ptr)

declare void @aiml_jw_set_i64(ptr, ptr, i64)

declare ptr @aiml_jw_done(ptr)

attributes #0 = { noinline }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: write) }
