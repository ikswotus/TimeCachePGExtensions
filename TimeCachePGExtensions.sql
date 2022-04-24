\echo use 'CREATE EXTENSION TimeCachePGExtensions;' to load this file. \quit

CREATE OR REPLACE FUNCTION ktest_adjacency_rd(double precision[], double precision)
returns integer
as 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION ktest_adjacency_arr(double precision[])
returns double precision[]
as 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE;


CREATE OR REPLACE FUNCTION ksimple(double precision[], int)
returns TABLE(average double precision, minimum double precision, maximum double precision, standarddev double precision, numcount int)
as 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE;

CREATE OR REPLACE FUNCTION kplusplus(double precision[], int, int, int)
returns TABLE(average double precision, minimum double precision, maximum double precision, standarddev double precision, numcount int)
as 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE;
