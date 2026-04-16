#pragma once

#include <Core/Block_fwd.h>
#include <Processors/IProcessor.h>

namespace DB
{

/// Sequentially activates FINAL merge layers within a single stream bucket.
///
/// Owns one initially-connected input (the bucket's first layer) plus a list of
/// pending layers stored as detached processor sets. When the active input finishes,
/// `prepare` returns `ExpandPipeline` so `expandPipeline` can add the next layer's
/// processors to the execution graph and connect its terminal output to a newly
/// appended input port. Only one layer per bucket is ever resident in the executor
/// at a time, bounding peak memory of the bucket to a single merge's working set
/// regardless of how many layers the bucket contains.
///
/// Correctness: layers in a bucket occupy non-overlapping primary key ranges, so
/// their outputs can be concatenated in any order — no cross-layer merge-sort is
/// needed, and inter-layer ordering doesn't matter for FINAL.
class FinalLayerChain : public IProcessor
{
public:
    struct PendingLayer
    {
        Processors processors;       /// Owns every processor for the layer — keeps them alive until activated.
        OutputPort * terminal_out;   /// Points into `processors`; valid for the layer's lifetime.
    };

    FinalLayerChain(SharedHeader header, std::vector<PendingLayer> pending);

    String getName() const override { return "FinalLayerChain"; }

    Status prepare() override;
    Processors expandPipeline() override;

private:
    std::vector<PendingLayer> pending_layers;   /// Reversed on construction so `back()` is the next to activate.
    PendingLayer expand_next;                   /// Staged between `prepare() == ExpandPipeline` and `expandPipeline()`.
};

}
