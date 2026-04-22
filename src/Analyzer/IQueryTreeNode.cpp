#include <Analyzer/IQueryTreeNode.h>

#include <unordered_map>

#include <Common/SipHash.h>

#include <IO/WriteBuffer.h>
#include <IO/WriteHelpers.h>
#include <IO/Operators.h>

#include <Parsers/ASTWithAlias.h>

#include <boost/functional/hash.hpp>

namespace DB
{

namespace ErrorCodes
{
    extern const int UNSUPPORTED_METHOD;
}

const char * toString(QueryTreeNodeType type)
{
    switch (type)
    {
        case QueryTreeNodeType::IDENTIFIER: return "IDENTIFIER";
        case QueryTreeNodeType::MATCHER: return "MATCHER";
        case QueryTreeNodeType::TRANSFORMER: return "TRANSFORMER";
        case QueryTreeNodeType::LIST: return "LIST";
        case QueryTreeNodeType::CONSTANT: return "CONSTANT";
        case QueryTreeNodeType::FUNCTION: return "FUNCTION";
        case QueryTreeNodeType::COLUMN: return "COLUMN";
        case QueryTreeNodeType::LAMBDA: return "LAMBDA";
        case QueryTreeNodeType::SORT: return "SORT";
        case QueryTreeNodeType::INTERPOLATE: return "INTERPOLATE";
        case QueryTreeNodeType::WINDOW: return "WINDOW";
        case QueryTreeNodeType::TABLE: return "TABLE";
        case QueryTreeNodeType::TABLE_FUNCTION: return "TABLE_FUNCTION";
        case QueryTreeNodeType::QUERY: return "QUERY";
        case QueryTreeNodeType::ARRAY_JOIN: return "ARRAY_JOIN";
        case QueryTreeNodeType::CROSS_JOIN: return "CROSS_JOIN";
        case QueryTreeNodeType::JOIN: return "JOIN";
        case QueryTreeNodeType::UNION: return "UNION";
    }
}

IQueryTreeNode::IQueryTreeNode(size_t children_size, size_t weak_pointers_size)
{
    children.resize(children_size);
    weak_pointers.resize(weak_pointers_size);
}

IQueryTreeNode::IQueryTreeNode(size_t children_size)
{
    children.resize(children_size);
}

namespace
{

using NodePair = std::pair<const IQueryTreeNode *, const IQueryTreeNode *>;

struct NodePairHash
{
    size_t operator()(const NodePair & node_pair) const
    {
        auto hash = std::hash<const IQueryTreeNode *>();

        size_t result = 0;
        boost::hash_combine(result, hash(node_pair.first));
        boost::hash_combine(result, hash(node_pair.second));

        return result;
    }
};

}

bool IQueryTreeNode::isEqual(const IQueryTreeNode & rhs, CompareOptions compare_options) const
{
    if (this == &rhs)
        return true;

    std::vector<NodePair> nodes_to_process;
    std::unordered_set<NodePair, NodePairHash> equals_pairs;

    nodes_to_process.emplace_back(this, &rhs);

    while (!nodes_to_process.empty())
    {
        auto nodes_to_compare = nodes_to_process.back();
        nodes_to_process.pop_back();

        const auto * lhs_node_to_compare = nodes_to_compare.first;
        const auto * rhs_node_to_compare = nodes_to_compare.second;

        assert(lhs_node_to_compare);
        assert(rhs_node_to_compare);

        if (equals_pairs.contains(std::make_pair(lhs_node_to_compare, rhs_node_to_compare)))
            continue;

        if (lhs_node_to_compare == rhs_node_to_compare)
        {
            equals_pairs.emplace(lhs_node_to_compare, rhs_node_to_compare);
            continue;
        }

        if (lhs_node_to_compare->getNodeType() != rhs_node_to_compare->getNodeType() ||
            !lhs_node_to_compare->isEqualImpl(*rhs_node_to_compare, compare_options))
            return false;

        if (compare_options.compare_aliases && lhs_node_to_compare->alias != rhs_node_to_compare->alias)
            return false;

        const auto & lhs_children = lhs_node_to_compare->children;
        const auto & rhs_children = rhs_node_to_compare->children;

        size_t lhs_children_size = lhs_children.size();
        if (lhs_children_size != rhs_children.size())
            return false;

        for (size_t i = 0; i < lhs_children_size; ++i)
        {
            const auto & lhs_child = lhs_children[i];
            const auto & rhs_child = rhs_children[i];

            if (!lhs_child && !rhs_child)
                continue;
            if (lhs_child && !rhs_child)
                return false;
            if (!lhs_child && rhs_child)
                return false;

            nodes_to_process.emplace_back(lhs_child.get(), rhs_child.get());
        }

        const auto & lhs_weak_pointers = lhs_node_to_compare->weak_pointers;
        const auto & rhs_weak_pointers = rhs_node_to_compare->weak_pointers;

        size_t lhs_weak_pointers_size = lhs_weak_pointers.size();

        if (lhs_weak_pointers_size != rhs_weak_pointers.size())
            return false;

        for (size_t i = 0; i < lhs_weak_pointers_size; ++i)
        {
            auto lhs_strong_pointer = lhs_weak_pointers[i].lock();
            auto rhs_strong_pointer = rhs_weak_pointers[i].lock();

            if (!lhs_strong_pointer && !rhs_strong_pointer)
                continue;
            if (lhs_strong_pointer && !rhs_strong_pointer)
                return false;
            if (!lhs_strong_pointer && rhs_strong_pointer)
                return false;

            nodes_to_process.emplace_back(lhs_strong_pointer.get(), rhs_strong_pointer.get());
        }

        equals_pairs.emplace(lhs_node_to_compare, rhs_node_to_compare);
    }

    return true;
}

/// Compute tree hash using a Merkle scheme: each node's hash depends on its
/// content plus the hashes of its children.  Shared subtrees (produced by the
/// alias-result cache) are hashed once and the result is reused via a
/// pointer-keyed memo table, turning an exponential DAG walk into a linear one.
///
/// Uses iterative post-order traversal to avoid stack overflow on deep trees.
IQueryTreeNode::Hash IQueryTreeNode::getTreeHash(CompareOptions compare_options) const
{
    HashMap<const IQueryTreeNode *, Hash> strong_memo;
    HashMap<const IQueryTreeNode *, size_t> weak_node_to_identifier;

    enum class Phase : uint8_t { ENTER, CHILDREN_DONE };

    struct Frame
    {
        const IQueryTreeNode * node;
        bool is_weak;
        Phase phase;
        /// Resolved weak pointer kept alive for the child currently being processed.
        QueryTreeNodePtr held_weak_child;
    };

    std::vector<Frame> stack;
    stack.push_back({this, false, Phase::ENTER, {}});

    /// Collects child hashes for the current parent.  Each time a child
    /// completes, its Hash is pushed here.  When the parent reaches
    /// CHILDREN_DONE it pops exactly the right number of child hashes.
    std::vector<Hash> result_stack;

    while (!stack.empty())
    {
        auto & frame = stack.back();
        const auto * node = frame.node;

        if (frame.phase == Phase::ENTER)
        {
            /// Check memos before descending.
            if (frame.is_weak)
            {
                auto * it = weak_node_to_identifier.find(node);
                if (it)
                {
                    HashState h;
                    h.update(it->getMapped());
                    result_stack.push_back(getSipHash128AsPair(h));
                    stack.pop_back();
                    continue;
                }
                size_t new_id = weak_node_to_identifier.size();
                decltype(weak_node_to_identifier)::LookupResult lookup;
                bool inserted;
                weak_node_to_identifier.emplace(node, lookup, inserted);
                if (inserted)
                    lookup->getMapped() = new_id;
            }
            else
            {
                auto * it = strong_memo.find(node);
                if (it)
                {
                    result_stack.push_back(it->getMapped());
                    stack.pop_back();
                    continue;
                }
            }

            /// Schedule children in reverse order so they execute left-to-right.
            frame.phase = Phase::CHILDREN_DONE;

            for (auto it = node->weak_pointers.rbegin(); it != node->weak_pointers.rend(); ++it)
            {
                auto strong_ptr = it->lock();
                if (!strong_ptr)
                    continue;
                auto * raw = strong_ptr.get();
                stack.push_back({raw, true, Phase::ENTER, std::move(strong_ptr)});
            }

            for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
            {
                if (*it)
                    stack.push_back({it->get(), false, Phase::ENTER, {}});
            }

            continue;
        }

        /// Phase::CHILDREN_DONE — all children have completed and their
        /// hashes are on result_stack.  Pop them and build this node's hash.
        HashState hash_state;
        hash_state.update(static_cast<size_t>(node->getNodeType()));

        if (compare_options.compare_aliases && !node->alias.empty())
        {
            hash_state.update(node->alias.size());
            hash_state.update(node->alias);
        }

        node->updateTreeHashImpl(hash_state, compare_options);

        /// Count non-null strong children.
        size_t num_strong_nonnull = 0;
        for (const auto & child : node->children)
            if (child)
                ++num_strong_nonnull;

        /// Count non-null weak children.
        size_t num_weak_nonnull = 0;
        for (const auto & wp : node->weak_pointers)
            if (!wp.expired())
                ++num_weak_nonnull;

        size_t total_child_hashes = num_strong_nonnull + num_weak_nonnull;

        /// Pop child hashes from result_stack (they're in left-to-right order
        /// at the top of the stack).
        size_t first_child = result_stack.size() - total_child_hashes;

        hash_state.update(node->children.size());
        for (size_t i = 0; i < num_strong_nonnull; ++i)
        {
            hash_state.update(result_stack[first_child + i].low64);
            hash_state.update(result_stack[first_child + i].high64);
        }

        hash_state.update(node->weak_pointers.size());
        for (size_t i = 0; i < num_weak_nonnull; ++i)
        {
            hash_state.update(result_stack[first_child + num_strong_nonnull + i].low64);
            hash_state.update(result_stack[first_child + num_strong_nonnull + i].high64);
        }

        result_stack.resize(first_child);

        Hash result = getSipHash128AsPair(hash_state);

        if (!frame.is_weak)
        {
            decltype(strong_memo)::LookupResult lookup;
            bool inserted;
            strong_memo.emplace(node, lookup, inserted);
            if (inserted)
                lookup->getMapped() = result;
        }

        result_stack.push_back(result);
        stack.pop_back();
    }

    return result_stack.back();
}

QueryTreeNodePtr IQueryTreeNode::clone() const
{
    return cloneAndReplace({});
}

QueryTreeNodePtr IQueryTreeNode::shallowClone() const
{
    auto result = cloneImpl();
    result->children = children;
    result->weak_pointers = weak_pointers;
    result->alias = alias;
    result->original_alias = original_alias;
    result->original_ast = original_ast;
    return result;
}

QueryTreeNodePtr IQueryTreeNode::cloneAndReplace(const ReplacementMap & replacement_map) const
{
    /** Clone tree with this node as root.
      *
      * Algorithm
      * For each node we clone state and also create mapping old pointer to new pointer.
      * For each cloned node we update weak pointers array.
      *
      * After that we can update pointer in weak pointers array using old pointer to new pointer mapping.
      */
    std::unordered_map<const IQueryTreeNode *, QueryTreeNodePtr> old_pointer_to_new_pointer;
    std::vector<QueryTreeNodeWeakPtr *> weak_pointers_to_update_after_clone;

    QueryTreeNodePtr result_cloned_node_place;

    std::vector<std::pair<const IQueryTreeNode *, QueryTreeNodePtr *>> nodes_to_clone;
    nodes_to_clone.emplace_back(this, &result_cloned_node_place);

    while (!nodes_to_clone.empty())
    {
        const auto [node_to_clone, place_for_cloned_node] = nodes_to_clone.back();
        nodes_to_clone.pop_back();

        auto already_cloned_node_it = old_pointer_to_new_pointer.find(node_to_clone);
        if (already_cloned_node_it != old_pointer_to_new_pointer.end())
        {
            *place_for_cloned_node = already_cloned_node_it->second;
            continue;
        }

        auto it = replacement_map.find(node_to_clone);
        auto node_clone = it != replacement_map.end() ? it->second : node_to_clone->cloneImpl();
        *place_for_cloned_node = node_clone;

        old_pointer_to_new_pointer.emplace(node_to_clone, node_clone);

        if (it != replacement_map.end())
            continue;

        node_clone->original_ast = node_to_clone->original_ast;
        node_clone->setAlias(node_to_clone->alias);
        node_clone->children = node_to_clone->children;
        node_clone->weak_pointers = node_to_clone->weak_pointers;

        for (auto & child : node_clone->children)
        {
            if (!child)
                continue;

            nodes_to_clone.emplace_back(child.get(), &child);
        }

        for (auto & weak_pointer : node_clone->weak_pointers)
        {
            weak_pointers_to_update_after_clone.push_back(&weak_pointer);
        }
    }

    /** Ensure all replacement_map entries are in old_pointer_to_new_pointer.
      * When a node is replaced, its children are not traversed and thus not added
      * to old_pointer_to_new_pointer. If those children are also in the replacement_map
      * (e.g., inner column sources of an ARRAY_JOIN being replaced), their entries
      * must be available for weak pointer updates below.
      */
    for (const auto & [old_ptr, new_ptr] : replacement_map)
        old_pointer_to_new_pointer.emplace(old_ptr, new_ptr);

    /** Update weak pointers to new pointers if they were changed during clone.
      * To do this we check old pointer to new pointer map, if weak pointer
      * strong pointer exists as old pointer in map, reinitialize weak pointer with new pointer.
      */
    for (auto & weak_pointer_ptr : weak_pointers_to_update_after_clone)
    {
        assert(weak_pointer_ptr);
        auto strong_pointer = weak_pointer_ptr->lock();
        auto it = old_pointer_to_new_pointer.find(strong_pointer.get());

        /** If node had weak pointer to some other node and this node is not part of cloned subtree do not update weak pointer.
          * It will continue to point to previous location and it is expected.
          *
          * Example: SELECT id FROM test_table;
          * During analysis `id` is resolved as column node and `test_table` is column source.
          * If we clone `id` column, result column node weak source pointer will point to the same `test_table` column source.
          */
        if (it == old_pointer_to_new_pointer.end())
            continue;

        *weak_pointer_ptr = it->second;
    }
    result_cloned_node_place->original_ast = original_ast;

    return result_cloned_node_place;
}

QueryTreeNodePtr IQueryTreeNode::cloneAndReplace(const QueryTreeNodePtr & node_to_replace, QueryTreeNodePtr replacement_node) const
{
    ReplacementMap replacement_map;
    replacement_map.emplace(node_to_replace.get(), std::move(replacement_node));

    return cloneAndReplace(replacement_map);
}

ASTPtr IQueryTreeNode::toAST(const ConvertToASTOptions & options) const
{
    if (options.toAST_cache)
    {
        if (auto * it = options.toAST_cache->find(this))
            return it->getMapped();
    }

    auto converted_node = toASTImpl(options);

    if (auto * /*ast_with_alias*/ _ = dynamic_cast<ASTWithAlias *>(converted_node.get()))
        converted_node->setAlias(alias);

    if (options.toAST_cache)
        (*options.toAST_cache)[this] = converted_node;

    return converted_node;
}

String IQueryTreeNode::formatOriginalASTForErrorMessage() const
{
    if (!original_ast)
        throw Exception(ErrorCodes::UNSUPPORTED_METHOD, "Original AST was not set");

    return original_ast->formatForErrorMessage();
}

String IQueryTreeNode::formatConvertedASTForErrorMessage() const
{
    return toAST()->formatForErrorMessage();
}

String IQueryTreeNode::dumpTree() const
{
    WriteBufferFromOwnString buffer;
    dumpTree(buffer);

    return buffer.str();
}

size_t IQueryTreeNode::FormatState::getNodeId(const IQueryTreeNode * node)
{
    auto [it, _] = node_to_id.emplace(node, node_to_id.size());
    return it->second;
}

void IQueryTreeNode::dumpTree(WriteBuffer & buffer) const
{
    FormatState state;
    dumpTreeImpl(buffer, state, 0);
}

}
