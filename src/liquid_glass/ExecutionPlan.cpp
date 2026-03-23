#include "liquid_glass/ExecutionPlan.h"

namespace lg {

namespace {

uint64_t fnv1a_append(uint64_t hash, const unsigned char* data, size_t size) {
    constexpr uint64_t kOffset = 1469598103934665603ull;
    constexpr uint64_t kPrime = 1099511628211ull;
    if (hash == 0) {
        hash = kOffset;
    }
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= kPrime;
    }
    return hash;
}

}  // namespace

GlassExecutionPlan resolve_glass_execution_plan(const GlassParams& params) {
    GlassExecutionPlan plan{};
    plan.fast_path = params.specular_step > 1 || params.legibility_step > 1;
    plan.fuse_reflection_specular = plan.fast_path;
    plan.use_mixed_precision = plan.fast_path;
    plan.use_readonly_cache = true;
    plan.use_cuda_graph = params.use_cuda_graph != 0;
    return plan;
}

uint64_t glass_execution_signature(const GlassParams& params, const GlassExecutionPlan& plan) {
    uint64_t hash = fnv1a_append(0, reinterpret_cast<const unsigned char*>(&params), sizeof(params));
    const unsigned char flags[] = {
        static_cast<unsigned char>(plan.fast_path),
        static_cast<unsigned char>(plan.fuse_reflection_specular),
        static_cast<unsigned char>(plan.use_mixed_precision),
        static_cast<unsigned char>(plan.use_readonly_cache),
        static_cast<unsigned char>(plan.use_cuda_graph)
    };
    return fnv1a_append(hash, flags, sizeof(flags));
}

bool should_rebuild_cuda_graph(uint64_t active_signature, const GlassParams& params, const GlassExecutionPlan& plan, bool graph_valid) {
    return !graph_valid || active_signature != glass_execution_signature(params, plan);
}

}
