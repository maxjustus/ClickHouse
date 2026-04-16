#include <Processors/Merges/FinalLayerChain.h>

#include <Processors/Port.h>

#include <algorithm>

namespace DB
{

FinalLayerChain::FinalLayerChain(SharedHeader header, std::vector<PendingLayer> pending)
    : IProcessor({header}, {header})
    , pending_layers(std::move(pending))
{
    std::reverse(pending_layers.begin(), pending_layers.end());
}

IProcessor::Status FinalLayerChain::prepare()
{
    auto & out = outputs.front();

    if (out.isFinished())
    {
        for (auto & in : inputs)
            in.close();
        return Status::Finished;
    }

    if (!out.canPush())
        return Status::PortFull;

    /// Only the most recently appended input is active; earlier inputs have
    /// already finished in earlier rounds of this loop.
    auto & active = inputs.back();

    if (active.isFinished())
    {
        if (!pending_layers.empty())
        {
            expand_next = std::move(pending_layers.back());
            pending_layers.pop_back();
            return Status::ExpandPipeline;
        }

        out.finish();
        return Status::Finished;
    }

    active.setNeeded();
    if (active.hasData())
    {
        out.push(active.pull());
        return Status::PortFull;
    }

    return Status::NeedData;
}

Processors FinalLayerChain::expandPipeline()
{
    /// Mirrors the MergeSortingTransform / DelayedSource expandPipeline pattern:
    /// append a fresh input, connect it to the activated layer's terminal output,
    /// hand the layer's processors to the executor.
    inputs.emplace_back(outputs.front().getHeader(), this);
    connect(*expand_next.terminal_out, inputs.back());
    inputs.back().setNeeded();
    return std::move(expand_next.processors);
}

}
