#ifndef AIML_PIPELINE_EXPAND_H
#define AIML_PIPELINE_EXPAND_H

// spec/62 §13.5f — rewrite the declared pipeline form into the composed
// one, BEFORE compiling.  That way there is only one executor
// (F62-23) and not two that drift apart (§16 item 4).
#include <string>

namespace aiml {

// Does the source contain a `- pipeline:` declaration?  Only then is it
// rewritten — every other file runs unchanged through the usual
// path, so that the rewrite touches no file that is none of its business.
bool sourceHasPipelineDecl(const std::string& text);

// Rewrites.  `out` gets the expanded source text.  Returns false
// if something had to be rejected (the diagnostic is then in the log) or there is nothing
// to rewrite.
bool expandPipelineDeclarations(const std::string& text, const std::string& where,
                                std::string& out);

} // namespace aiml
#endif
