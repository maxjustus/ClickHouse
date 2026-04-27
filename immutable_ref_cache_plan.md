# Alias Cache COW Hardening Plan

## Goal

`enable_identifier_resolve_cache` avoids repeated alias resolution by caching resolved alias expressions and returning cheap cache-hit clones. The cache-hit clone path uses `shallowClone`, so the cloned root has private metadata but shares child pointers with the cached expression.

The goal is to keep this pragmatic representation and make it safe by consistently using copy-on-write accessors before mutating shared children.

## Chosen approach

Use the existing mutable query tree model with targeted COW:

- keep `shallowClone` for alias-cache hits;
- keep shared children until a mutation actually happens;
- before mutating a child/list section, clone the shared child/list if its `use_count` is greater than 1;
- add regression tests that compare important alias-cache paths with cache enabled and disabled.

This avoids adding a new query-tree node type and avoids planner/equality/hash special cases.

## Why not `ExpressionReferenceNode`

`ExpressionReferenceNode` gives a stronger structural guarantee, but the implementation cost is too high for the problem:

- new `QueryTreeNodeType` case in many switches;
- transparent equality and hashing changes in `IQueryTreeNode`;
- planner special handling and `const_pointer_cast`;
- materialization before nullable conversion;
- every future analyzer pass must decide whether to skip, unwrap, or materialize references.

The actual safety issue is narrower: shared children from `shallowClone` must not be mutated in place. COW accessors address that directly.

## COW contract

Mutation code must use one of these patterns:

```cpp
QueryTreeNodePtr & child = parent->getMutableChild(index);
```

or node-specific helpers such as:

```cpp
function_node->getMutableArguments();
function_node->getMutableParameters();
query_node->getMutableProjection();
query_node->getMutableGroupBy();
query_node->getMutableOrderBy();
```

Read-only code may keep using const accessors:

```cpp
function_node->getArguments().getNodes();
query_node->getProjection().getNodes();
```

Direct mutable access through `getChildren` or `getNodes` is only safe when the caller has already ensured the owner node/list is private.

## Implementation checklist

1. Keep `IQueryTreeNode::shallowClone` as the cache-hit strategy.
2. Keep `IQueryTreeNode::getMutableChild` as the generic COW boundary.
3. Use node-specific mutable helpers for common list children, especially function arguments/parameters and query sections.
4. Audit analyzer passes for direct mutable access to `getNodes` and replace with mutable helpers where the node can come from a cached alias expression.
5. Add regression tests covering repeated aliases, nested aliases, aggregate/group-by interactions, nullable grouping, `untuple`, and lambdas.

## Tests

Add a stateless test for the alias cache COW behavior. It should run with:

```sql
SET enable_analyzer = 1;
SET enable_identifier_resolve_cache = 1;
```

and cover:

1. repeated scalar aliases;
2. nested aliases;
3. aliases in `WHERE`, `GROUP BY`, `HAVING`, and `ORDER BY`;
4. aliases used inside and outside aggregate expressions;
5. `GROUP BY ... WITH ROLLUP` with `group_by_use_nulls = 1`;
6. `untuple` alias expansion;
7. lambda aliases;
8. representative queries repeated with `enable_identifier_resolve_cache = 0` for differential coverage.

## Success criteria

1. Deep repeated aliases do not cause exponential tree growth.
2. Cache-enabled and cache-disabled results match on alias-heavy queries.
3. Shared child mutation is routed through COW accessors.
4. No planner or query-tree infrastructure special cases are needed for cache-hit nodes.
