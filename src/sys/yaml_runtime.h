#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void* aiml_yaml_parse(const char* source);
void* aiml_yaml_parse_file(const char* path);
char* aiml_yaml_error(void* document);
void aiml_yaml_free(void* document);
void* aiml_yaml_root(void* document);
int32_t aiml_yaml_kind(void* node);
int32_t aiml_yaml_size(void* node);
char* aiml_yaml_scalar(void* node);
int32_t aiml_yaml_line(void* node);
int32_t aiml_yaml_column(void* node);
char* aiml_yaml_dump(void* node);
void* aiml_yaml_item(void* node, int32_t index);
char* aiml_yaml_key(void* node, int32_t index);
void* aiml_yaml_value(void* node, int32_t index);
void* aiml_yaml_get(void* node, const char* key);

#ifdef __cplusplus
/* spec/60: fehlertoleranter Lexer fuer Hervorhebung; fuellt list<i32> mit Tripeln. */
int32_t aiml_yaml_tokens(const char* text, void* out_list);

}
#endif
