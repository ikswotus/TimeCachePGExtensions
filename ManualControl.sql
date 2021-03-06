
CREATE OR REPLACE FUNCTION ksimple(double precision[], int)
returns TABLE(average double precision, minimum double precision, maximum double precision, standarddev double precision, numcount int)
as 'TimeCachePGExtensions'
LANGUAGE C IMMUTABLE;



CREATE OR REPLACE FUNCTION ksimple(double precision[], int)
returns TABLE(average double precision, minimum double precision, maximum double precision, standarddev double precision, numcount int)
as 'TimeCachePGExtensions'
LANGUAGE C IMMUTABLE;




CREATE OR REPLACE FUNCTION biggest_breaks(double precision[], int)
returns int[] 
as 'TimeCachePGExtensions'
LANGUAGE C IMMUTABLE;



CREATE OR REPLACE FUNCTION public.ksimple_all(
	double precision[], integer)
    RETURNS TABLE(cluster_number integer, average double precision, minimum double precision, 
				  maximum double precision, stddev double precision, numcount integer)
    LANGUAGE 'c'
    COST 1
    IMMUTABLE PARALLEL UNSAFE
    ROWS 1000

AS 'TimeCachePGExtensions', 'ksimple_all';




CREATE OR REPLACE FUNCTION public.kplusplus_all(
	double precision[], integer, integer, integer)
    RETURNS TABLE(cluster_number integer, average double precision, minimum double precision, 
				  maximum double precision, stddev double precision, numcount integer)
    LANGUAGE 'c'
    COST 1
    IMMUTABLE PARALLEL UNSAFE
    ROWS 1000

AS 'TimeCachePGExtensions', 'kplusplus_all';