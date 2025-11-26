#include <Analyzer/createUniqueAliasesIfNecessary.h>

#include <charconv>

#include <Analyzer/ArrayJoinNode.h>
#include <Analyzer/ColumnNode.h>
#include <Analyzer/FunctionNode.h>
#include <Analyzer/InDepthQueryTreeVisitor.h>
#include <Analyzer/IQueryTreeNode.h>
#include <Analyzer/LambdaNode.h>
#include <Analyzer/Utils.h>

namespace DB
{

namespace
{

/// Find the maximum numeric suffix among aliases matching the given prefix.
/// For example, with prefix "__table", finds max N from aliases like "__table5", "__table12".
class MaxAliasIdVisitor : public ConstInDepthQueryTreeVisitor<MaxAliasIdVisitor>
{
public:
    explicit MaxAliasIdVisitor(std::string_view prefix_) : prefix(prefix_) {}

    void visitImpl(const QueryTreeNodePtr & node)
    {
        if (!node->hasAlias())
            return;

        const auto & alias = node->getAlias();
        if (!alias.starts_with(prefix))
            return;

        std::string_view suffix(alias);
        suffix.remove_prefix(prefix.size());

        size_t parsed = 0;
        auto [ptr, ec] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
        if (ec == std::errc{} && ptr == suffix.data() + suffix.size())
            max_id = std::max(max_id, parsed);
    }

    size_t getMaxId() const { return max_id; }

private:
    std::string_view prefix;
    size_t max_id = 0;
};

class CreateUniqueTableAliasesVisitor : public InDepthQueryTreeVisitorWithContext<CreateUniqueTableAliasesVisitor>
{
public:
    using Base = InDepthQueryTreeVisitorWithContext<CreateUniqueTableAliasesVisitor>;

    explicit CreateUniqueTableAliasesVisitor(const ContextPtr & context, size_t start_id)
        : Base(context)
        , next_id(start_id)
    {
        // Insert a fake node on top of the stack.
        scope_nodes_stack.push_back(std::make_shared<LambdaNode>(Names{}, nullptr, false));
    }

    void enterImpl(QueryTreeNodePtr & node)
    {
        auto node_type = node->getNodeType();

        switch (node_type)
        {
            case QueryTreeNodeType::QUERY:
                [[fallthrough]];
            case QueryTreeNodeType::UNION:
            {
                /// Queries like `(SELECT 1) as t` have invalid syntax. To avoid creating such queries (e.g. in StorageDistributed)
                /// we need to remove aliases for top level queries.
                /// N.B. Subquery depth starts count from 1, so the following condition checks if it's a top level.
                if (getSubqueryDepth() == 1)
                {
                    node->removeAlias();
                    break;
                }
                [[fallthrough]];
            }
            case QueryTreeNodeType::TABLE:
                [[fallthrough]];
            case QueryTreeNodeType::TABLE_FUNCTION:
            {
                /// If this table already has a synthetic alias from a previous run,
                /// keep it so repeated traversals (e.g. during shard planning) stay stable.
                if (node->hasAlias() && node->getAlias().starts_with("__table"))
                    break;

                auto & alias = table_expression_to_alias[node];
                if (alias.empty())
                {
                    scope_to_nodes_with_aliases[scope_nodes_stack.back()].push_back(node);
                    alias = fmt::format("__table{}", ++next_id);
                    node->setAlias(alias);
                }
                break;
            }
            case QueryTreeNodeType::ARRAY_JOIN:
            {
                /// Simulate previous behaviour and preserve table naming with previous versions
                ++next_id;
                break;
            }
            default:
                break;
        }

        switch (node_type)
        {
            case QueryTreeNodeType::QUERY:
                [[fallthrough]];
            case QueryTreeNodeType::UNION:
                [[fallthrough]];
            case QueryTreeNodeType::LAMBDA:
                scope_nodes_stack.push_back(node);
                break;
            default:
                break;
        }
    }

    void leaveImpl(QueryTreeNodePtr & node)
    {
        if (scope_nodes_stack.back() == node)
        {
            if (auto it = scope_to_nodes_with_aliases.find(scope_nodes_stack.back());
                it != scope_to_nodes_with_aliases.end())
            {
                for (const auto & node_with_alias : it->second)
                {
                    table_expression_to_alias.erase(node_with_alias);
                }
                scope_to_nodes_with_aliases.erase(it);
            }
            scope_nodes_stack.pop_back();
        }

        /// Here we revisit subquery for IN function. Reasons:
        /// * For remote query execution, query tree may be traversed a few times.
        ///   In such a case, it is possible to get AST like
        ///   `IN ((SELECT ... FROM table AS __table4) AS __table1)` which result in
        ///   `Multiple expressions for the alias` exception
        /// * Tables in subqueries could have different aliases => different three hashes,
        ///   which is important to be able to find a set in PreparedSets
        /// See 01253_subquery_in_aggregate_function_JustStranger.
        ///
        /// So, we revisit this subquery to make aliases stable.
        /// This should be safe cause columns from IN subquery can't be used in main query anyway.
        if (node->getNodeType() == QueryTreeNodeType::FUNCTION)
        {
            auto * function_node = node->as<FunctionNode>();
            if (isNameOfInFunction(function_node->getFunctionName()))
            {
                auto arg = function_node->getArguments().getNodes().back();
                /// Avoid aliasing IN `table`
                if (arg->getNodeType() != QueryTreeNodeType::TABLE)
                {
                    /// For the recursive call, we continue from current next_id to avoid collisions
                    CreateUniqueTableAliasesVisitor(getContext(), next_id).visit(function_node->getArguments().getNodes().back());
                }
            }
        }
    }

private:
    size_t next_id = 0;

    // Stack of nodes which create scopes: QUERY, UNION and LAMBDA.
    QueryTreeNodes scope_nodes_stack;

    std::unordered_map<QueryTreeNodePtr, QueryTreeNodes> scope_to_nodes_with_aliases;

    // We need to use raw pointer as a key, not a QueryTreeNodePtrWithHash.
    std::unordered_map<QueryTreeNodePtr, String> table_expression_to_alias;
};

class CreateUniqueArrayJoinAliasesVisitor : public InDepthQueryTreeVisitorWithContext<CreateUniqueArrayJoinAliasesVisitor>
{
public:
    using Base = InDepthQueryTreeVisitorWithContext<CreateUniqueArrayJoinAliasesVisitor>;

    explicit CreateUniqueArrayJoinAliasesVisitor(const ContextPtr & context, size_t start_id)
        : Base(context)
        , next_id(start_id)
    {
    }

    void enterImpl(QueryTreeNodePtr & node)
    {
        if (auto * array_join_typed = node->as<ArrayJoinNode>())
        {
            populateRenamingMap(array_join_typed, renaming[array_join_typed]);
            return;
        }

        auto * column_node = node->as<ColumnNode>();
        if (!column_node || replaced_nodes_set.contains(node))
            return;

        auto column_source = column_node->getColumnSource();
        auto * array_join = column_source->as<ArrayJoinNode>();
        if (!array_join)
            return;

        auto & renaming_map = getRenamingMap(array_join);

        auto new_column = column_node->getColumn();
        new_column.name = renaming_map[column_node->getColumnName()];
        auto new_column_node = std::make_shared<ColumnNode>(new_column, column_source);

        node = std::move(new_column_node);
        replaced_nodes_set.insert(node);
    }

private:

    using RenamingMap = std::unordered_map<String, String>;

    void populateRenamingMap(ArrayJoinNode * array_join, RenamingMap & result)
    {
        if (result.empty())
        {
            for (auto & array_join_expression : array_join->getJoinExpressions())
            {
                auto * array_join_column = array_join_expression->as<ColumnNode>();
                chassert(array_join_column != nullptr);

                String unique_expression_name = fmt::format("__array_join_exp_{}", ++next_id);
                result.emplace(array_join_column->getColumnName(), unique_expression_name);

                auto replacement_column = array_join_column->getColumn();
                replacement_column.name = unique_expression_name;
                auto replacement_column_node = std::make_shared<ColumnNode>(replacement_column, array_join_column->getExpression(), array_join_column->getColumnSource());
                replacement_column_node->setAlias(unique_expression_name);

                array_join_expression = std::move(replacement_column_node);
                replaced_nodes_set.insert(array_join_expression);
            }
        }
    }

    RenamingMap & getRenamingMap(ArrayJoinNode * array_join)
    {
        auto & result  = renaming[array_join];

        populateRenamingMap(array_join, result);

        return result;
    }

    size_t next_id = 0;

    std::unordered_map<ArrayJoinNode *, RenamingMap> renaming;

    // TODO: Remove this field when identifier resolution cache removed from analyzer.
    std::unordered_set<QueryTreeNodePtr> replaced_nodes_set;
};

}

void createUniqueAliasesIfNecessary(QueryTreeNodePtr & node, const ContextPtr & context)
{
    /// Find max existing alias IDs to avoid collisions.
    /// This handles: repeat traversals, user-provided __table* aliases, cloned trees.
    MaxAliasIdVisitor table_max_visitor("__table");
    table_max_visitor.visit(node);
    size_t table_start_id = table_max_visitor.getMaxId();

    MaxAliasIdVisitor array_join_max_visitor("__array_join_exp_");
    array_join_max_visitor.visit(node);
    size_t array_join_start_id = array_join_max_visitor.getMaxId();

    /*
     * For each table expression in the Query Tree generate and add a unique alias.
     * If table expression had an alias in initial query tree, override it.
     */
    CreateUniqueTableAliasesVisitor(context, table_start_id).visit(node);

    /* Generate unique aliases for array join expressions.
     * It's required to create a valid AST for distributed query.
     */
    CreateUniqueArrayJoinAliasesVisitor(context, array_join_start_id).visit(node);
}

}
