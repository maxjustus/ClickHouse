SET enable_analyzer = 1;
SET enable_identifier_resolve_cache = 1;

WITH number + 1 AS x
SELECT x, x, x FROM numbers(3) ORDER BY x;

WITH
    number + 1 AS a,
    a + a AS b,
    b + b AS c
SELECT c FROM numbers(3) ORDER BY c;

SELECT number % 3 AS k, count() AS c
FROM numbers(10)
WHERE k >= 0
GROUP BY k
HAVING c > 0
ORDER BY k;

SELECT number + 1 AS x, sum(x)
FROM numbers(10)
GROUP BY x
ORDER BY x;

SET group_by_use_nulls = 1;

SELECT number + 1 AS x, count()
FROM numbers(5)
GROUP BY x WITH ROLLUP
ORDER BY x NULLS FIRST;

SELECT untuple(tuple(1, 2)) AS x;

WITH (x -> x + 1) AS f SELECT f(1), f(2);

SET enable_identifier_resolve_cache = 0;
SET group_by_use_nulls = 0;

WITH number + 1 AS x
SELECT x, x, x FROM numbers(3) ORDER BY x;

SELECT number % 3 AS k, count() AS c
FROM numbers(10)
WHERE k >= 0
GROUP BY k
HAVING c > 0
ORDER BY k;
