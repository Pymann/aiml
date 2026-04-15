#include "ai_runtime.h"
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <algorithm>

// ── embedding_lookup: 1 memcpy replaces dim × seek/read/push ──
extern "C" void aiml_ai_embedding_lookup(const uint8_t* flat_data, int32_t token_id,
                                          int32_t dim, float* result) {
    const float* src = reinterpret_cast<const float*>(flat_data + (int64_t)token_id * dim * 4);
    std::memcpy(result, src, dim * sizeof(float));
}

// ── embedding_update: pointer arithmetic replaces dim × seek/read/write ──
extern "C" void aiml_ai_embedding_update(uint8_t* flat_data, int32_t token_id,
                                          const float* grad_data, float lr, int32_t dim) {
    float* base = reinterpret_cast<float*>(flat_data + (int64_t)token_id * dim * 4);
    for (int32_t d = 0; d < dim; d++)
        base[d] -= lr * grad_data[d];
}

// ── dot product ──
extern "C" float aiml_ai_dot(const float* a, const float* b, int32_t dim) {
    float sum = 0.0f;
    for (int32_t d = 0; d < dim; d++)
        sum += a[d] * b[d];
    return sum;
}

// ── encode_dense: mean pooling of token embeddings ──
extern "C" void aiml_ai_encode_dense(const int32_t* token_ids, int32_t n_tokens,
                                      const uint8_t* emb_flat, int32_t dim,
                                      float* result) {
    std::memset(result, 0, dim * sizeof(float));
    if (n_tokens <= 0) return;
    for (int32_t i = 0; i < n_tokens; i++) {
        const float* emb = reinterpret_cast<const float*>(emb_flat + (int64_t)token_ids[i] * dim * 4);
        for (int32_t d = 0; d < dim; d++)
            result[d] += emb[d];
    }
    float inv = 1.0f / n_tokens;
    for (int32_t d = 0; d < dim; d++)
        result[d] *= inv;
}

// ── encode_dense_weighted: U-shaped positional weighting ──
extern "C" void aiml_ai_encode_dense_weighted(const int32_t* token_ids, int32_t n_tokens,
                                               const uint8_t* emb_flat, int32_t dim,
                                               float* result) {
    std::memset(result, 0, dim * sizeof(float));
    if (n_tokens <= 0) return;
    float weight_sum = 0.0f;
    for (int32_t i = 0; i < n_tokens; i++) {
        float pos = (float)i / (float)n_tokens;
        float w = 1.0f + 0.5f * std::fabs(2.0f * pos - 1.0f);
        const float* emb = reinterpret_cast<const float*>(emb_flat + (int64_t)token_ids[i] * dim * 4);
        for (int32_t d = 0; d < dim; d++)
            result[d] += emb[d] * w;
        weight_sum += w;
    }
    float inv = 1.0f / weight_sum;
    for (int32_t d = 0; d < dim; d++)
        result[d] *= inv;
}

// ── triplet_loss: max(0, ||q-pos||² - ||q-neg||² + margin) ──
extern "C" float aiml_ai_triplet_loss(const float* q, const float* pos, const float* neg,
                                       float margin, int32_t dim) {
    float dist_pos = 0.0f, dist_neg = 0.0f;
    for (int32_t d = 0; d < dim; d++) {
        float dp = q[d] - pos[d];
        float dn = q[d] - neg[d];
        dist_pos += dp * dp;
        dist_neg += dn * dn;
    }
    float loss = dist_pos - dist_neg + margin;
    return loss > 0.0f ? loss : 0.0f;
}

// ── triplet_grad: gradient w.r.t. query embedding ──
// grad_q[d] = 2*(q[d]-pos[d]) - 2*(q[d]-neg[d]) = 2*(neg[d]-pos[d]) when loss > 0
extern "C" void aiml_ai_triplet_grad(const float* q, const float* pos, const float* neg,
                                      float margin, int32_t dim, float* grad_out) {
    // Check if loss > 0 first
    float dist_pos = 0.0f, dist_neg = 0.0f;
    for (int32_t d = 0; d < dim; d++) {
        float dp = q[d] - pos[d];
        float dn = q[d] - neg[d];
        dist_pos += dp * dp;
        dist_neg += dn * dn;
    }
    float loss = dist_pos - dist_neg + margin;
    if (loss <= 0.0f) {
        std::memset(grad_out, 0, dim * sizeof(float));
        return;
    }
    for (int32_t d = 0; d < dim; d++)
        grad_out[d] = 2.0f * (neg[d] - pos[d]);
}

// ── sample_logits: temperature-scaled top-k sampling from tensor ──
// tensor_ptr: opaque AimlTensor* (reads .data and .size)
// Returns sampled token index.
struct AimlTensorView { float* data; int* shape; int ndim; int size; };

extern "C" int32_t aiml_ai_sample_logits(void* tensor_ptr, float temperature, int32_t top_k) {
    static bool seeded = false;
    if (!seeded) { std::srand(static_cast<unsigned>(std::time(nullptr))); seeded = true; }

    auto* t = static_cast<AimlTensorView*>(tensor_ptr);
    const float* logits = t->data;
    int32_t vocab_size = t->size;
    if (vocab_size <= 0) return 0;
    if (top_k <= 0 || top_k > vocab_size) top_k = vocab_size;

    // Build index array sorted by logit value (descending)
    std::vector<int32_t> indices(vocab_size);
    for (int32_t i = 0; i < vocab_size; i++) indices[i] = i;
    std::partial_sort(indices.begin(), indices.begin() + top_k, indices.end(),
        [&](int32_t a, int32_t b) { return logits[a] > logits[b]; });

    // Temperature-scaled softmax over top-k
    float max_val = logits[indices[0]];
    std::vector<float> probs(top_k);
    float sum = 0.0f;
    for (int32_t i = 0; i < top_k; i++) {
        probs[i] = std::exp((logits[indices[i]] - max_val) / temperature);
        sum += probs[i];
    }
    for (int32_t i = 0; i < top_k; i++) probs[i] /= sum;

    // Weighted random sample
    float r = (float)rand() / (float)RAND_MAX;
    float cumulative = 0.0f;
    for (int32_t i = 0; i < top_k; i++) {
        cumulative += probs[i];
        if (r <= cumulative) return indices[i];
    }
    return indices[top_k - 1];
}

// ════════════════════════════════════════════════════════════════════
// BPE Training — SentencePiece-style incremental merge algorithm
// ════════════════════════════════════════════════════════════════════

namespace {

// Pack two 32-bit token IDs into one 64-bit key for the pair map
inline uint64_t packPair(int32_t a, int32_t b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
            static_cast<uint64_t>(static_cast<uint32_t>(b));
}

struct BpeWord {
    std::vector<int32_t> tokens;  // Current token ID sequence for this word
    int32_t freq;                 // How often this word appears in corpus
};

} // anon namespace

extern "C" AimlBpeMergeResult* aiml_bpe_train_merges(const char** texts, int32_t n_texts,
                                                      int32_t max_merges) {
    auto* result = static_cast<AimlBpeMergeResult*>(std::calloc(1, sizeof(AimlBpeMergeResult)));

    // 1. Split all texts into words, count word frequencies
    std::unordered_map<std::string, int32_t> wordFreq;
    for (int32_t i = 0; i < n_texts; i++) {
        if (!texts[i]) continue;
        std::string text(texts[i]);
        // Lowercase
        for (auto& c : text) c = std::tolower(static_cast<unsigned char>(c));
        // Split on whitespace
        size_t pos = 0;
        while (pos < text.size()) {
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) pos++;
            size_t start = pos;
            while (pos < text.size() && !std::isspace(static_cast<unsigned char>(text[pos]))) pos++;
            if (pos > start) {
                std::string word = text.substr(start, pos - start);
                if (word.size() <= 48)
                    wordFreq[word]++;
            }
        }
    }

    if (wordFreq.empty()) return result;

    // 2. Build vocabulary: each unique character gets an ID
    std::unordered_map<std::string, int32_t> vocab;
    auto ensureToken = [&](const std::string& tok) -> int32_t {
        auto it = vocab.find(tok);
        if (it != vocab.end()) return it->second;
        int32_t id = static_cast<int32_t>(vocab.size());
        vocab[tok] = id;
        return id;
    };

    // Reverse map for output
    std::vector<std::string> idToToken;
    auto ensureTokenFull = [&](const std::string& tok) -> int32_t {
        auto it = vocab.find(tok);
        if (it != vocab.end()) return it->second;
        int32_t id = static_cast<int32_t>(vocab.size());
        vocab[tok] = id;
        if (idToToken.size() <= static_cast<size_t>(id))
            idToToken.resize(id + 1);
        idToToken[id] = tok;
        return id;
    };

    // 3. Initialize: split each word into characters
    std::vector<BpeWord> words;
    words.reserve(wordFreq.size());
    for (auto& [w, freq] : wordFreq) {
        BpeWord bw;
        bw.freq = freq;
        for (size_t i = 0; i < w.size(); i++) {
            std::string ch(1, w[i]);
            bw.tokens.push_back(ensureTokenFull(ch));
        }
        words.push_back(std::move(bw));
    }

    // 4. Count all adjacent pairs
    std::unordered_map<uint64_t, int64_t> pairCounts;
    for (auto& bw : words) {
        for (size_t j = 0; j + 1 < bw.tokens.size(); j++) {
            pairCounts[packPair(bw.tokens[j], bw.tokens[j + 1])] += bw.freq;
        }
    }

    // 5. Iterative merge
    std::vector<std::pair<std::string, std::string>> merges;
    merges.reserve(max_merges);

    for (int32_t m = 0; m < max_merges; m++) {
        // Find best pair
        uint64_t bestPair = 0;
        int64_t bestCount = 1;  // Minimum 2 occurrences required
        for (auto& [pair, count] : pairCounts) {
            if (count > bestCount) {
                bestCount = count;
                bestPair = pair;
            }
        }
        if (bestCount <= 1) break;  // No more useful merges

        int32_t leftId  = static_cast<int32_t>(bestPair >> 32);
        int32_t rightId = static_cast<int32_t>(bestPair & 0xFFFFFFFF);

        // Ensure id→token mapping is complete
        if (idToToken.size() <= static_cast<size_t>(std::max(leftId, rightId)))
            idToToken.resize(std::max(leftId, rightId) + 1);

        std::string leftTok  = idToToken[leftId];
        std::string rightTok = idToToken[rightId];
        std::string merged   = leftTok + rightTok;
        int32_t mergedId     = ensureTokenFull(merged);

        merges.push_back({leftTok, rightTok});

        // Remove the merged pair count
        pairCounts.erase(bestPair);

        // Update all words containing this pair
        for (auto& bw : words) {
            for (size_t j = 0; j + 1 < bw.tokens.size(); ) {
                if (bw.tokens[j] == leftId && bw.tokens[j + 1] == rightId) {
                    // Decrement old neighbor pairs
                    if (j > 0) {
                        uint64_t oldLeft = packPair(bw.tokens[j - 1], leftId);
                        pairCounts[oldLeft] -= bw.freq;
                        if (pairCounts[oldLeft] <= 0) pairCounts.erase(oldLeft);
                    }
                    if (j + 2 < bw.tokens.size()) {
                        uint64_t oldRight = packPair(rightId, bw.tokens[j + 2]);
                        pairCounts[oldRight] -= bw.freq;
                        if (pairCounts[oldRight] <= 0) pairCounts.erase(oldRight);
                    }

                    // Merge: replace tokens[j..j+1] with mergedId
                    bw.tokens[j] = mergedId;
                    bw.tokens.erase(bw.tokens.begin() + j + 1);

                    // Increment new neighbor pairs
                    if (j > 0) {
                        uint64_t newLeft = packPair(bw.tokens[j - 1], mergedId);
                        pairCounts[newLeft] += bw.freq;
                    }
                    if (j + 1 < bw.tokens.size()) {
                        uint64_t newRight = packPair(mergedId, bw.tokens[j + 1]);
                        pairCounts[newRight] += bw.freq;
                    }
                    // Don't increment j — check if the merged token forms a new pair at same position
                } else {
                    j++;
                }
            }
        }
    }

    // 6. Build result
    result->count = static_cast<int32_t>(merges.size());
    if (result->count > 0) {
        result->left  = static_cast<char**>(std::malloc(result->count * sizeof(char*)));
        result->right = static_cast<char**>(std::malloc(result->count * sizeof(char*)));
        for (int32_t i = 0; i < result->count; i++) {
            result->left[i]  = strdup(merges[i].first.c_str());
            result->right[i] = strdup(merges[i].second.c_str());
        }
    }

    return result;
}

extern "C" void aiml_bpe_merge_result_free(AimlBpeMergeResult* r) {
    if (!r) return;
    for (int32_t i = 0; i < r->count; i++) {
        std::free(r->left[i]);
        std::free(r->right[i]);
    }
    std::free(r->left);
    std::free(r->right);
    std::free(r);
}
