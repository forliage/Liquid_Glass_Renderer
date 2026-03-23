#pragma once
#include "liquid_glass/GlassParams.h"
#include <cstdint>

namespace lg {

struct GlassExecutionPlan {
    bool fast_path = false;
    bool fuse_reflection_specular = false;
    bool use_mixed_precision = false;
    bool use_readonly_cache = false;
    bool use_cuda_graph = false;
};

GlassExecutionPlan resolve_glass_execution_plan(const GlassParams& params);
uint64_t glass_execution_signature(const GlassParams& params, const GlassExecutionPlan& plan);
bool should_rebuild_cuda_graph(uint64_t active_signature, const GlassParams& params, const GlassExecutionPlan& plan, bool graph_valid);

}
