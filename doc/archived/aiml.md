If I am interested in developing programming languages. Can you help me with that?

Gemini said
That is a fascinating – if also deep – rabbit hole you want to jump into. Developing a programming language (language design & compiler construction) is often considered the "supreme discipline" of computer science.

But don't worry, you don't have to be a genius to get started. You just need structure. Here is an overview of what this process usually looks like:

1. The architecture: how text becomes logic
Before a computer understands what print("Hello") means, the code has to pass through several processing stages. This is often called the compiler pipeline.

Lexer (scanner): breaks the source text into small building blocks, so-called tokens (e.g. keywords, numbers, operators).

Parser: checks whether the tokens are in a grammatically correct order and builds an abstract syntax tree (AST) from them.

Semantic analysis: checks the logic (e.g.: "may these two variables be added?").

Code generation: translates the tree either directly into machine code, into bytecode (like Java), or sends it to an interpreter that executes it immediately.

2. The most important concepts for getting started
So as not to sink into chaos, you should familiarise yourself with these terms:

Grammar (EBNF): you have to define the rules of your language formally. What does a loop look like? How is a variable declared?

Abstract syntax tree (AST): the heart of it all. Almost every language is represented internally as a tree structure.

Static vs. dynamic typing: do you have to define the type of a variable beforehand (int x = 5), or does the language do that for you?

3. Your "roadmap" to success
If you really want to build your own language, I recommend this path:

The classic: read "Crafting Interpreters" by Robert Nystrom. It is probably the best modern book on this topic. It takes you step by step from a scripting language to a full-fledged bytecode compiler.

The tools:

LLVM: if you want to build an extremely high-performance language (like Swift or Rust), this is the framework of choice for the "backend".

ANTLR: a powerful tool that helps you generate parsers automatically from a grammar file.

The language for the compiler: use a language that handles tree structures well. Rust is extremely popular for this at the moment, but C++ or Python (for the first interpreter) are great too.

A small reality check (with a wink)
Designing a language is 10 % "inventing cool syntax" and 90 % "wrestling with error messages and edge cases". It is an incredibly satisfying experience when your own compiler calculates a simple addition for the first time.

Where would you like to start?

Shall I explain how to write a very simple lexer in Python or Rust, or are you more interested in the theory behind the syntax grammar?

You said
I want to develop a language that can be written and read by AI as easily or as performantly as possible

Gemini said
That is an absolutely modern and exciting approach! While classic languages are optimised for the human brain (readability, abstraction), an "AI-native" language has completely different requirements.

AI models (LLMs) "think" in token probabilities and logical patterns. If the language is to be easy for an AI to write and performant, we have to tailor the architecture to the strengths and weaknesses of transformers.

Here are the strategic cornerstones for such a language:

1. Token efficiency (the "vocabulary")
AIs process text in tokens. The fewer tokens needed for a complex operation, the faster the AI generates the code and the less space it takes up in the context window.

Avoid redundancy: long keywords such as function or procedure are "expensive". Symbols or short, unambiguous keywords are more efficient.

Unambiguity: use a syntax that is easy for tokenizers (such as BPE) to break down. Avoid character sequences that change their meaning drastically depending on context.

2. Strict predictability (no "magic")
AIs tend to hallucinate when the syntax is too flexible or "magical" (as in Ruby or Perl).

Explicit before implicit: the language should have no hidden side effects.

Strong typing: if the AI is forced to define types, it (and the compiler) recognises logic errors immediately. A static type system acts like a guard rail for the model.

3. Structure: S-expressions or YAML-like?
There are two directions that work particularly well for AIs:

Approach	Advantage for the AI	Disadvantage
Lisp style (S-expressions)	Parentheses ((add 1 2)) map trees perfectly. AIs can follow tree structures excellently.	Often hard to read for humans ("parenthesis hell").
Indentation-based (Python style)	Uses the spatial attention of the AI.	Indentation errors often lead to hard-to-find bugs.
JSON/manifest style	Extremely structured, every element has a fixed place.	Very verbose (many tokens).
4. The "self-correction" feature
A language for AIs should be built so that the compiler generates error messages that can be returned directly as a prompt.

Example: instead of Error: Syntax Error at Line 5, your compiler should say:
Error: Variable 'x' expected Type: Int but got String. Suggestion: Use cast() or change declaration.

5. Metadata integration
AIs work better when they know the context. Your language could enforce "intent blocks":

Plaintext
intent "Compute the factorial of a number"
func fac(n: i32) -> i32 {
    ...
}
This helps the AI check the code against its own intention during generation.

How do we start?
To build a language that is specifically "pleasant" for AIs, we could design a prototype.

Would you like us to design a short code example of what such an "AI-optimised" syntax could look like? (We could, for example, define a syntax that needs extremely few tokens but is logically rock-solid.)

You said
Maybe one skips the lexer step completely and develops a token language that is really just a json file?

Gemini said
That is an extremely clever "shortcut"! You would thereby practically skip the classic frontend phase of the compiler (lexing & parsing of free text) and work directly at the level of the abstract syntax tree (AST).

If your language is really just a structured file (like JSON), you use an already existing format as your "syntax".

Why JSON is a brilliant (and a difficult) approach for AIs
The advantages:
No parser stress: you don't have to write your own lexer. Every programming language has json.decode(). You can concentrate on the logic (the interpreter or compiler) immediately.

Schema validation: you can define a JSON schema. An AI can be given this schema in its context and then knows exactly which fields are allowed. That reduces syntax errors to almost zero.

Understanding of structure: AIs are extremely good at generating hierarchical data.

The problem: the "token overhead"
AIs pay (in money and compute time) per token. JSON is "expensive":

{, ", :, ,, } are all separate tokens.

A simple command in JSON needs much more space than in a compact syntax.

Feature	Classic code	JSON-based code
Addition	x = 1 + 2	{"op": "assign", "var": "x", "val": {"op": "add", "args": [1, 2]}}
Tokens	~5 tokens	~15-20 tokens
The alternative: "S-expressions" (the Lisp way)
If you want the structure of JSON but the efficiency of a real language, you often end up with S-expressions. That is essentially "JSON without the ballast".

Instead of:
{"fn": "print", "args": ["hello"]}
You write:
(print "hello")

AIs love this, because the parentheses map the tree (AST) perfectly, but hardly any unnecessary characters are used.

What your "AI-native" JSON compiler could look like
If you stick with the JSON approach, your workflow would be:

Definition: you define what operations are called (e.g. "type": "loop", "type": "call").

The runner: you write a program (in Python, Rust or Go) that reads this JSON.

Execution:

Interpreter route: your program walks through the JSON tree step by step and executes the commands directly.

Transpiler route: your program reads the JSON and writes finished C++ or Python code from it.

An example of AI-optimised JSON logic:

JSON
{
  "program": "math_test",
  "execute": [
    ["var", "pi", 3.1415],
    ["call", "print", ["pi"]],
    ["if", [">", "pi", 3], [
      ["call", "print", ["Pi is big"]]
    ]]
  ]
}
Here I used arrays [] instead of objects {} to save tokens. That is still perfectly readable for an AI.

My proposal for your next experiment:
We could design a small specification together.

What do you think if we define a list of the 5 most important commands (variable, loop, if, function, print) and I show you what the corresponding Python interpreter looks like that executes this "JSON program"?

You said
Do you know pkl from apple or toml, can one save tokens with them?

Gemini said
That is a very good thought! You are looking for the "sweet spot" between structure (for the AI logic) and brevity (to save tokens).

Let's look at Pkl and TOML compared to JSON, specifically in the context of "programming by AI".

1. TOML (Tom's Obvious Minimal Language)
TOML was designed to be extremely readable for humans.

Token savings: yes, massive compared to JSON! You save almost all quotation marks on keys and the curly braces.

The problem: TOML is designed "flat". As soon as you have deep nesting (which happens constantly with code logic such as if conditions in loops in functions), TOML becomes very unwieldy and uses up more tokens again through the constant table headers ([node.child.subchild]).

2. Pkl (Apple)
Pkl is actually almost a programming language of its own for configurations.

Token savings: very high. It looks almost like clean Swift or Kotlin code.

Advantage for AI: Pkl is type-safe. You can give the AI a Pkl template, and it cannot insert wrong data types.

Disadvantage: Pkl is complex. If you take Pkl as a basis, you are essentially building a language on top of a language.

The token comparison: a simple piece of logic
Let's imagine we want to set a variable and check a condition: if (x > 10) { print(x) }

Format	Code example	Token efficiency	Assessment
JSON	{"op":"if","cond":{">":["x",10]},"then":{"op":"print","args":["x"]}}	🔴 Low (many " { :)	Too much "noise".
TOML	
[if] 


 cond = [">", "x", 10] 


 then = { op="print", args=["x"] }

🟡 Medium	Better, but the syntax mix is confusing.
Pkl	
x = 10 


 ifXGreater = (x > 10) 


 action = "print"

🟢 High	Very clean, almost like real code.
YAML	
if: 


   cond: [>, x, 10] 


   then: [print, x]

🟢🟢 Very high	The winner for AIs. No braces, minimal characters.
Why YAML (or a YAML clone) would be ideal for your project
If you want to skip the lexer step, YAML is often the best choice for AIs:

No braces/quotation marks: saves an extreme number of tokens.

Structure through indentation: AIs (like GPT-4 or Claude) understand indentation perfectly.

Native parsers: in Python you can simply use PyYAML and immediately have a finished abstract syntax tree (AST).

A concrete proposal for your language
Let's call it "AILang". It could be based on YAML. A program would look like this:

YAML
# AILang example
- set: [counter, 0]
- while: 
    cond: [<, counter, 10]
    do:
      - call: [print, "Number: ", counter]
      - set: [counter, [+, counter, 1]]
The advantage:

You don't have to write a lexer.

You read the file with yaml.safe_load().

You get a Python list of dicts that you simply work through from top to bottom.

My next step for you:
I can write you a tiny Python script that reads exactly such a YAML structure and "executes" it. That way you would have your first working interpreter for a token-saving AI language within 5 minutes.

Shall I create such a "YAML interpreter prototype" for you?

You said
We call it AIML

Gemini said
AIML – that sounds like a clear statement! A small "fun fact" up front: there is already a language called Artificial Intelligence Markup Language (from the 90s, based on XML for chatbots). But your approach is more modern: AI-native, token-saving and logically direct.

Let's bring AIML (2.0) to life right away. Since we skip the lexer and use YAML as the carrier format, we can build the interpreter immediately.

Here is a prototype in Python that shows how simply your new language "AIML" can work.

The AIML interpreter (prototype)
This script reads a YAML document as code and executes it. It supports variables, simple maths, conditions and loops.

Python
import yaml

class AIML_Interpreter:
    def __init__(self):
        self.variables = {}

    def evaluate(self, expr):
        """Computes values or fetches variables."""
        if isinstance(expr, list):
            op = expr[0]
            if op == '+': return self.evaluate(expr[1]) + self.evaluate(expr[2])
            if op == '-': return self.evaluate(expr[1]) - self.evaluate(expr[2])
            if op == '<': return self.evaluate(expr[1]) < self.evaluate(expr[2])
            if op == '>': return self.evaluate(expr[1]) > self.evaluate(expr[2])
            if op == '==': return self.evaluate(expr[1]) == self.evaluate(expr[2])
        
        # If it is a string, check whether it is a variable
        if isinstance(expr, str) and expr in self.variables:
            return self.variables[expr]
        return expr

    def run(self, code):
        """Walks through the list of AIML commands."""
        if not code: return

        for cmd in code:
            if 'set' in cmd:
                name, val = cmd['set']
                self.variables[name] = self.evaluate(val)
            
            elif 'print' in cmd:
                args = cmd['print'] if isinstance(cmd['print'], list) else [cmd['print']]
                print("AIML Output:", *(self.evaluate(a) for a in args))
            
            elif 'while' in cmd:
                condition = cmd['while']['cond']
                body = cmd['while']['do']
                while self.evaluate(condition):
                    self.run(body)
            
            elif 'if' in cmd:
                if self.evaluate(cmd['if']['cond']):
                    self.run(cmd['if']['then'])

# --- TEST CODE IN AIML (YAML FORMAT) ---
aiml_code = """
- set: [counter, 1]
- set: [limit, 5]
- print: ["Starting AIML program...", "Limit is:", limit]
- while:
    cond: ['<', counter, limit]
    do:
      - print: ["Pass no.:", counter]
      - set: [counter, ['+', counter, 1]]
- print: ["Done!"]
"""

# Execution
interpreter = AIML_Interpreter()
interpreter.run(yaml.safe_load(aiml_code))
Why this is brilliant for AIs:
Structured data: an AI does not have to fear forgotten semicolons or curly braces. If the YAML format is right, the code runs.

Token efficiency: compare - set: [x, 10] with let x = 10;. In the token world, the compact list format almost always wins.

Extensibility: want a new command? You simply add a new elif 'your_command' in cmd: in the interpreter.

What AIML is still missing:
Since we are building for an AI, we could build in a feature that normal languages do not have: probabilistic paths.

Imagine the AI could say in the code: "Take path A 80% of the time and path B 20% of the time", in order to test different solution approaches.

Shall I show you how we build in a chance function with which the AI can work directly with probabilities in the code?

You said
Explain these paths in more detail?

Gemini said
This is the point where AIML really differs from classic languages like C++ or Python. In a conventional language, everything is deterministic: if (x > 10) is either true or false. There is no "maybe".

AIs, on the other hand, work natively with probabilities. If we build "probabilistic paths" into AIML, we give the AI the ability to express uncertainty or variance directly in the code.

Here are the three levels at which such paths can be implemented in AIML:

1. The "chance" path (explicit probability)
Imagine an AI writes an agent for customer support. Sometimes the agent should be friendly, sometimes very matter-of-fact.

AIML syntax:

YAML
- chance:
    0.8: # In 80% of cases
      - set: [mood, "friendly"]
      - call: [generate_response, "Hello! How can I help?"]
    0.2: # In 20% of cases
      - set: [mood, "matter-of-fact"]
      - call: [generate_response, "Ready for your request."]
The advantage for the AI: the AI does not have to program complex random logic (random.seed, if random.random() < 0.8). It simply describes the distribution. That saves a huge number of tokens and reduces logic errors.

2. The "semantic" path (AI decision at runtime)
This is the most powerful form. Here it is not a mathematical formula that decides which path is taken, but a mini LLM check in the background of the interpreter.

AIML syntax:

YAML
- select:
    query: "Is the user's mood aggressive?"
    options:
      yes:
        - call: [escalate_to_human, "User is dissatisfied"]
      no:
        - call: [continue_chat]
What happens here? The interpreter sends the query (and the context) to a small, fast model (e.g. GPT-4o-mini). The answer decides the path. In AIML, the AI decision is therefore a native control flow, just like an if check.

3. The "best-of-N" path (self-correction)
Here the language allows the AI to propose several solution paths at the same time. The interpreter executes them and takes the one that works.

AIML syntax:

YAML
- try_variants:
    - - set: [result, [call, "unstable_api_1"]]
    - - set: [result, [call, "unstable_api_2"]]
    validate: "result != null"
The interpreter tries path 1. If it throws an error or does not fulfil the validate condition, it jumps immediately to path 2. For an AI this is ideal, because it "knows" that its first solution might sometimes be wrong.

Implementation in your Python interpreter
To build in the first point (chance), we would only have to extend our interpreter by a few lines:

Python
import random

# ... inside the run method of the AIML_Interpreter:

            elif 'chance' in cmd:
                r = random.random()
                cumulative = 0
                for probability, path in cmd['chance'].items():
                    cumulative += float(probability)
                    if r <= cumulative:
                        self.run(path)
                        break
Why this matters for "saving tokens":
If an AI is to program "A/B testing" or a weighted random decision in Python, it needs about 50-100 tokens for importing random, defining the weights and the if/else logic.

In AIML it only needs the YAML scaffold. That is efficient because the AI has to write less, and performant because the interpreter does the heavy work (rolling the dice or validating).

Which of these paths do you find most exciting for AIML? Shall we take a closer look at the "semantic" path (the AI decision as a language command)?

You said
We should rather write the interpreter in c++, and what do I have to do to compile such a yaml?

Gemini said
C++ is an excellent choice for the foundation of AIML. While Python is great for prototypes, C++ offers the performance needed to execute AI-driven decisions (like the "semantic path") without noticeable overhead.

To "compile" YAML in C++, we have to split the process into two parts: parsing (reading in) and bytecode generation (compiling in the sense of a VM).

1. The tool: yaml-cpp
In C++ one does not usually write a YAML parser oneself. The industry standard is the library yaml-cpp. It turns the YAML file into a tree structure (YAML::Node) that you can then walk through.

2. The road to performance: from tree to bytecode
If you really want to make AIML fast, you should not "read" the YAML structure again and again at runtime. Instead you use a bytecode compiler.

Phase 1 (compile time): the C++ compiler reads the YAML and translates commands such as - set: [x, 10] into compact numeric commands (opcodes).

Phase 2 (runtime): an extremely fast C++ loop (virtual machine) works through this list of numbers.

Example: how AIML becomes bytecode
AIML (YAML)	Internal opcode (C++)	Value / address
- set: [x, 10]	OP_LOAD_CONST	10
OP_STORE_VAR	0 (index for 'x')
- print: [x]	OP_LOAD_VAR	0
OP_CALL_PRINT	-
3. A minimalist C++ skeleton
This is what the starting point for your AIML compiler in C++ would look like (using yaml-cpp):

C++
#include <iostream>
#include <vector>
#include <string>
#include <yaml-cpp/yaml.h>

// Definition of the operations (bytecode)
enum OpCode {
    SET_VAR,
    PRINT_VAR,
    CHANCE_BRANCH
};

struct Instruction {
    OpCode op;
    std::string target;
    int value;
};

class AIML_VM {
    std::vector<Instruction> program;
    std::map<std::string, int> memory;

public:
    void compile(const std::string& filename) {
        YAML::Node config = YAML::LoadFile(filename);
        for (auto const& cmd : config) {
            if (cmd["set"]) {
                program.push_back({SET_VAR, cmd["set"][0].as<std::string>(), cmd["set"][1].as<int>()});
            } else if (cmd["print"]) {
                program.push_back({PRINT_VAR, cmd["print"][0].as<std::string>(), 0});
            }
        }
    }

    void run() {
        for (const auto& inst : program) {
            switch (inst.op) {
                case SET_VAR:
                    memory[inst.target] = inst.value;
                    break;
                case PRINT_VAR:
                    std::cout << "AIML Out: " << memory[inst.target] << std::endl;
                    break;
            }
        }
    }
};
4. What you have to do to compile AIML "for real"
If you want to go beyond "compiling" (i.e. an executable .exe or binary), you have two options:

AOT (ahead-of-time) transpiler: your C++ program reads the YAML and writes a new .cpp file that does exactly what is in the YAML. You then run this .cpp file through g++ or clang. That is maximum performance.

LLVM binding: this is the professional variant. You use the LLVM framework to translate your YAML directly into machine code. That is complex, but then AIML is as fast as C or Rust.

Strategy proposal
For an AI language, the VM approach (bytecode) is usually the best. Why? Because AIs often generate code that is to be executed immediately (interactivity). A heavyweight compile step with g++ would disturb the "chat flow".

Shall I show you how to integrate the "chance path" (probabilities) efficiently into this C++ bytecode system?

You said
I mean llvm

Gemini said
That is the "Champions League". If you use LLVM, you are no longer building a simple scripting language, but a real systems language like Rust, Swift or Julia.

The trick: you do not translate your YAML directly into machine code (that would be extremely complicated), but into LLVM IR (intermediate representation). That is a kind of "portable assembly". LLVM then takes care of optimising this IR for Intel, AMD, ARM or Apple Silicon.

The workflow: from AIML to machine code
Here is the roadmap for how to get AIML flying with LLVM:

1. Build the AST from YAML
You keep using yaml-cpp to read the file. But instead of executing the commands directly (as in the interpreter), you now generate a corresponding LLVM IR instruction for every node in the YAML tree.

2. Initialise the LLVM infrastructure
In C++ you need three core components of LLVM:

llvm::Context: the memory area for all global data.

llvm::Module: the container for your entire program (your .aiml file).

llvm::IRBuilder: your "pen", with which you write the IR instructions.

3. Example: a "set" command in LLVM IR
This is the C++ code that translates the AIML command - set: [x, 42] into LLVM IR:

C++
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

// ... in the compiler context ...
llvm::LLVMContext Context;
llvm::IRBuilder<> Builder(Context);
auto Module = std::make_unique<llvm::Module>("AIML_Module", Context);

// Create a variable 'x' (allocation on the stack)
llvm::AllocaInst* Alloca = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, "x");

// Store the value 42
Builder.CreateStore(Builder.getInt32(42), Alloca);
The generated LLVM IR then looks like this in text form:
%x = alloca i32
store i32 42, i32* %x

Why LLVM is brilliant for an AI language
JIT (just-in-time) compilation: you can read in the AIML code and, while the AI generates it, turn it into real machine code and execute it immediately. It is as fast as C++, but feels interactive like Python.

Optimisation: LLVM has hundreds of built-in optimisations. If the AI writes "sloppy" or redundant YAML code, LLVM smooths it out before it reaches the CPU.

Portability: your compiler works without changes on a Mac M3, a Windows PC or a Linux server.

The biggest challenge with AIML + LLVM
Since LLVM is very strict, you have to take care of types. In Python it did not matter whether x was a number or a string. In LLVM you have to decide at compile time:

Is counter an int32, int64 or a double?

Tip for AIML: you could build a "universal type" system for the AI (similar to JavaScript), where the compiler automatically decides which LLVM type it takes.

The next step for you
To start with LLVM, you have to install the LLVM libraries on your system (e.g. via brew install llvm or apt install llvm-dev).

Shall I create a complete small C++ example project for you (a CMake file + a minimal compiler) that translates a single AIML assignment into real LLVM IR?

You said
I don't quite understand it, so I write the compiler in c++ with the llvm dev lib?

Gemini said
Exactly! Let's draw the picture very clearly. You build a program (the compiler) that reads another program (your YAML/AIML) and translates it into something the processor understands.

Think of it like a factory:

The factory floor (C++): that is the environment you work in. You write your compiler program in C++, because it is fast and has direct access to memory.

The toolbox (LLVM dev lib): LLVM is a huge collection of C++ functions. Instead of learning yourself how to write zeros and ones for an Intel or ARM processor (which would take months), you simply call functions from the LLVM library.

The blueprint (AIML/YAML): that is the file that is "pushed into" the factory at the end.

The process in detail
This is what the data flow looks like when your AIML compiler is finished:

Your C++ program starts.

It uses a library called yaml-cpp to read your my_code.aiml.

Your program walks through the YAML structure (e.g. it sees a - set: [x, 10]).

Now LLVM comes into play: your C++ code says to LLVM: "Hey LLVM, reserve some memory for an integer called 'x' and write a '10' into it."

Technically: you call Builder.CreateStore(...) from the LLVM lib.

LLVM generates the so-called IR code (intermediate representation) from that in the background. That is a kind of universal assembler.

At the end you tell LLVM: "Right, optimise the whole thing and make me an .exe (or a binary) out of it."

Why does one make it so complicated?
One could also write the compiler entirely in C++, without LLVM. But then you would have to write your own code for every processor type (Intel, Apple M1, AMD) for how to store a variable. LLVM takes this insanely hard part off your hands.

In summary, you need for your project:

A C++ compiler (e.g. g++ or clang): to build your own compiler.

The LLVM library (llvm-dev): the functions that generate the machine code for you.

A YAML parser (yaml-cpp): so that your C++ program understands the YAML at all.

How do you start in practice?
You don't have to be an LLVM expert to start. The most important part for you is to understand how to get from a YAML command to an LLVM function call.

Shall I show you a minimal C++ program that shows how to include the LLVM library and translate a single mathematical operation (like 1+2) into machine code? (That would be the "Hello World" of your compiler, so to speak.)

You said
What about graphics support?

Gemini said
This is the moment your language gets "eyes". In an LLVM-based language like AIML, you do not realise graphics support by sorting pixels individually by hand, but by marrying your compiler to a graphics library.

Since you write AIML in C++, you have access to the best graphics tools in the world.

The layer plan: how graphics end up in AIML
You do not have to reinvent the wheel. Your compiler acts as an intermediary between the AI's YAML and a graphics card (GPU).

Layer	Technology	Task
Frontend	AIML (YAML)	The AI writes: - draw_rect: [10, 10, 100, 50]
Logic	Your C++ compiler	Recognises the command and looks for the matching function in the graphics lib.
Bridge	LLVM IR	Generates a function call (call) to an external C library.
Graphics lib	Raylib / SDL2 / OpenGL	Receives the command and tells the GPU: "Draw this rectangle!"
Hardware	GPU	Sends the pixels to the monitor.
The best choice for AIML: Raylib
For a "lightweight" language I strongly recommend Raylib. It is written in C (perfect for LLVM bindings), extremely fast and needs almost no setup code.

This is what an AIML graphics command would look like:

YAML
- window: [800, 600, "AI graphics test"]
- loop:
    - clear_background: [255, 255, 255] # White
    - draw_circle: 
        pos: [400, 300]
        radius: 50
        color: [255, 0, 0] # Red
How do you implement this technically in LLVM?
In your C++ compiler you have to tell LLVM that there is an "external function" that is not defined in AIML itself, but exists somewhere out there in the Raylib world.

Declaration: you declare the function in your LLVM module (e.g. DrawCircle).

Call: when your YAML parser comes across draw_circle, your C++ code generates a Builder.CreateCall(...).

Linking: when you compile the program to completion, you tell the linker: "Attach raylib.lib (or .so) to it."

The "AI speciality": visualisation of data
Since AIML is meant for AIs, native support for shaders or tensor visualisation would be brilliant.

Imagine the AI could say directly in AIML:

YAML
- visualize_tensor: my_neural_network_layer
Your compiler could then automatically generate a shader that brings the AI's activations onto the screen in real time as a heatmap. That would be a unique selling point that no normal language offers!

The way forward
Graphics in your own compiler are a huge sense of achievement – when the first window pops up that your code generated, you feel like a magician.

Shall I show you how to declare such an "external function" (e.g. a simple DrawPoint) for LLVM in C++, so that AIML can call it?




Gemini is an AI and can make mistakes, including about people. Privacy and Gemini
