 CREATE TYPE __sinewavepoint AS (time timestamp with time zone, val double precision);
 


CREATE OR REPLACE FUNCTION public.generate_sinewave_series(
	timestamp with time zone,
	timestamp with time zone,
	interval,
	integer)
    RETURNS SETOF __sinewavepoint 
    LANGUAGE 'c'
    COST 1
    IMMUTABLE PARALLEL UNSAFE
    ROWS 1000

AS 'TimeCachePGExtensions', 'generate_sinewave_series'
;

-- With amplitude
CREATE OR REPLACE FUNCTION public.generate_sinewave_series(
	timestamp with time zone,
	timestamp with time zone,
	interval,
    integer,
	integer)
    RETURNS SETOF __sinewavepoint 
    LANGUAGE 'c'
    COST 1
    IMMUTABLE PARALLEL UNSAFE
    ROWS 1000

AS 'TimeCachePGExtensions', 'generate_sinewave_series'
;


select * from generate_sinewave_series(CURRENT_TIMESTAMP - interval '1 hour', CURRENT_TIMESTAMP, interval '1 minute', 400)


-------------------------------

CREATE TYPE __timepoint as (time timestamp with time zone, val integer);

CREATE OR REPLACE FUNCTION public.generate_randomwalk_series(
	timestamp with time zone,
	timestamp with time zone,
	interval,
	integer)
    RETURNS SETOF __timepoint 
    LANGUAGE 'c'
    COST 1
    IMMUTABLE PARALLEL UNSAFE
    ROWS 1000

AS 'TimeCachePGExtensions', 'generate_randomwalk_series'
;
