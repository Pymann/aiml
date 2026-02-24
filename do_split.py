import os
import re

os.makedirs('src/core/ast', exist_ok=True)

with open('src/core/ast_builder.h', 'r') as f:
    h_txt = f.read()

with open('src/core/ast_builder.cpp', 'r') as f:
    cpp_txt = f.read()

classes = [
    "RootState", "StatementBuilder", "BlockBuilder", "ClassBuilder", 
    "ModelBuilder", "InterfaceBuilder", "EnumBuilder", "NamespaceBuilder", 
    "ImportBuilder", "ConfigBuilder", "FunctionBuilder", "ControlFlowBuilder", 
    "ExpressionBuilder", "TryBuilder"
]

def to_snake(name):
    # e.g. RootState -> root_state
    s1 = "".join(['_' + c.lower() if c.isupper() else c for c in name]).strip('_')
    return s1

extracted_h_code = {}

for cls in classes:
    start_str = f"class {cls} : public StateHandler {{"
    start_idx = h_txt.find(start_str)
    
    # TryBuilder is currently in cpp_txt!
    if start_idx == -1:
        if cls == "TryBuilder":
             tb_start = cpp_txt.find(start_str)
             if tb_start != -1:
                 search_idx = tb_start + len(start_str) - 1
                 bracket_count = 0
                 end_idx = -1
                 for i in range(search_idx, len(cpp_txt)):
                     if cpp_txt[i] == '{': bracket_count += 1
                     elif cpp_txt[i] == '}': 
                         bracket_count -= 1
                         if bracket_count == 0:
                             end_idx = cpp_txt.find(';', i) + 1
                             break
                 if end_idx != -1:
                     extracted_h_code[cls] = cpp_txt[tb_start:end_idx].strip()
        continue
        
    search_idx = start_idx + len(start_str) - 1 # starts at '{'
    bracket_count = 0
    end_idx = -1
    
    for i in range(search_idx, len(h_txt)):
        if h_txt[i] == '{':
            bracket_count += 1
        elif h_txt[i] == '}':
            bracket_count -= 1
            if bracket_count == 0:
                end_idx = h_txt.find(';', i) + 1
                break
                
    if end_idx == -1:
        print(f"Failed to find end for {cls}")
        continue
        
    comment_start = h_txt.rfind("//", 0, start_idx)
    nl_before_comment = h_txt.find("\n", comment_start, start_idx)
    if comment_start != -1 and nl_before_comment == -1: # Comment immediately precedes
        start_idx = comment_start

    h_code = h_txt[start_idx:end_idx].strip()
    extracted_h_code[cls] = h_code


# Write headers
for cls, h_code in extracted_h_code.items():
    snake = to_snake(cls)
    
    extra_includes = ""
    if cls == "TryBuilder":
        extra_includes = '#include "../../../logger.h"\n#include "block_builder.h"\n'
        
    with open(f'src/core/ast/{snake}.h', 'w') as f:
        f.write(f"""#pragma once
#include <memory>
#include <string>
#include <vector>
#include <stack>
#include "../ast_nodes.h"
{extra_includes}
namespace AIML {{
namespace AST {{

class Builder;
class StateHandler;

{h_code}

}} // namespace AST
}} // namespace AIML
""")

print("Successfully generated independent headers.")

# Parse CPP
cpp_blocks = re.split(r'// --- (.*?) Implementation ---\n', cpp_txt)

cpp_base = cpp_blocks[0]
if "ast_builder.h" in cpp_base:
    h_includes = "\n".join([f'#include "ast/{to_snake(c)}.h"' for c in classes])
    cpp_base = cpp_base.replace('#include "ast_builder.h"', '#include "ast_builder.h"\n' + h_includes)

# Save the base 
with open('src/core/ast_builder.cpp', 'w') as f:
    f.write(cpp_base + "\n} // namespace AST\n} // namespace AIML\n")

# Process other blocks
for i in range(1, len(cpp_blocks), 2):
    title = cpp_blocks[i].strip()
    content = cpp_blocks[i+1]
    
    # TryBuilder implementation is technically fully inline in the header we extracted, so skip it in CPP!
    if title == "TryBuilder":
        continue

    if "Modular Builders" in title:
        filename = "modular_builders.cpp"
        # Modular builders contain NamespaceBuilder, ImportBuilder, ConfigBuilder
    else:
        filename = to_snake(title.split()[0].strip()) + ".cpp"
        
    code = content.replace('} // namespace AST\n} // namespace AIML\n', '').strip()
    
    # Needs to include its own header, logger, ast_builder.h and potentially other headers
    with open(f'src/core/ast/{filename}', 'w') as f:
        # For modular builders, include all 3 headers 
        if "modular" in filename:
             h_inc = '#include "namespace_builder.h"\n#include "import_builder.h"\n#include "config_builder.h"\n'
        else:
             h_inc = f'#include "{filename.replace(".cpp", ".h")}"\n'
             
        # Add extra dependencies
        if "root_state" in filename:
             h_inc += '#include "statement_builder.h"\n#include "control_flow_builder.h"\n#include "try_builder.h"\n#include "class_builder.h"\n#include "model_builder.h"\n#include "interface_builder.h"\n#include "enum_builder.h"\n#include "namespace_builder.h"\n#include "import_builder.h"\n#include "config_builder.h"\n#include "function_builder.h"\n'
        if "statement_builder" in filename:
             h_inc += '#include "expression_builder.h"\n'
        if "block_builder" in filename:
             h_inc += '#include "control_flow_builder.h"\n#include "try_builder.h"\n#include "statement_builder.h"\n'
        if "function_builder" in filename:
             h_inc += '#include "block_builder.h"\n'
        if "control_flow_builder" in filename:
             h_inc += '#include "expression_builder.h"\n#include "block_builder.h"\n'
        if "class_builder" in filename:
             h_inc += '#include "expression_builder.h"\n#include "function_builder.h"\n'
             
        f.write(f"""#include "../../../logger.h"
#include "../ast_builder.h"
{h_inc}
namespace AIML {{
namespace AST {{

{code}

}} // namespace AST
}} // namespace AIML
""")

# Re-write ast_builder.h to remove the extracted classes
new_h = h_txt
for cls_code in extracted_h_code.values():
    new_h = new_h.replace(cls_code, '')

with open('src/core/ast_builder.h', 'w') as f:
    f.write(new_h)

print("Split logic finished and files overwritten.")
