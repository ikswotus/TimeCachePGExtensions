------------------------------------------------------------------------------------------
-- timestamp_to_quadrant_key
-- return the indexing key for a timestamp, valid keys will be in the range [0-691] 4x24*7
CREATE OR REPLACE FUNCTION public.timestamp_to_quadrant_key(ts timestamp with time zone)
RETURNS integer
LANGUAGE 'plpgsql'
COST 100
STABLE STRICT PARALLEL UNSAFE
as $BODY$

BEGIN
	return extract(dow from ts)::int * 96 + extract(hour from ts) * 4 + floor(extract(minute from ts) / 15)::int;
END;

$BODY$;

alter function public.timestamp_to_quadrant_key(timestamp with time zone) owner to postgres;

select public.timestamp_to_quadrant_key(current_timestamp);
-----------------------------------------------------------
-- Test: Expect 671
select max(timestamp_to_quadrant_key(generate_series)) from generate_series(CURRENT_TIMESTAMP - interval '9 days', CURRENT_TIMESTAMP, interval '15 minutes')
-- Test: Expect 0
select min(timestamp_to_quadrant_key(generate_series)) from generate_series(CURRENT_TIMESTAMP - interval '9 days', CURRENT_TIMESTAMP, interval '15 minutes')
----------------------------------------------------------------------------------------------------------------------------------------------
CREATE OR REPLACE FUNCTION public.timestamp_to_dh_key(ts timestamp with time zone)
RETURNS integer
LANGUAGE 'plpgsql'
COST 100
STABLE STRICT PARALLEL UNSAFE
as $BODY$

BEGIN
	return extract(dow from ts)::int * 96 + extract(hour from ts) * 4;
END;

$BODY$;

alter function public.timestamp_to_dh_key(timestamp with time zone) owner to postgres;




/**
* compute a relative difference using formula:
* (l-r) / min(l,r)
*
*/
CREATE OR REPLACE FUNCTION public.relative_diff_min(l integer, r integer)
RETURNS double precision
as $$
BEGIN
IF l = 0 then return r::double precision;
ELSIF (r = 0) THEN RETURN l::double precision;
ELSE
	IF l < r THEN RETURN (l-r::double precision)/ l;
	ELSE RETURN (l-r::double precision) / r;
	END IF;
END IF;
END;
$$
LANGUAGE 'plpgsql' IMMUTABLE;
ALTER FUNCTION public.relative_diff_min(integer, integer) OWNER TO postgres;


--------------------------
CREATE OR REPLACE FUNCTION public.aggregate_quadrant_percentages(base_date timestamp with time zone)
RETURNS double precision[]
LANGUAGE 'sql'
AS $BODY$

with q1 as (select (floor(extract(minute from base_date))::int % 15) / 15.0 as q)
, q2 as (select 1.0 as q)
, q3 as (select 1.0 as q)
, q4 as (select 1.0 as q)
, q5 as (select (15.0 - floor(extract(minute from base_date))::int % 15) / 15.0 as q)
select array_agg(q)
from ( SELECT q from q1 UNION ALL select q from q2 UNION ALL select q from q3 UNION ALL
	   SELECT q from q4 UNION ALL select q from q5) unioned
$BODY$;

ALTER FUNCTION public.aggregate_quadrant_percentages(timestamp with time zone) OWNER TO postgres;

-- EXAMPLE:
--select public.aggregate_quadrant_percentages(CURRENT_TIMESTAMP);


---------------------------------------------------
CREATE OR REPLACE FUNCTION public.aggregate_hourly_quadrants(base_date timestamp with time zone)
RETURNS integer[]
LANGUAGE 'sql'
AS $BODY$
SELECT array_agg(extract(dow from generate_series) * 96  +
				 extract(hour from generate_series) * 4 + 
				 floor((extract(minute from generate_series) / 15))::int)
	   FROM generate_series(base_date, base_date - interval '1 hour', -1 * interval '15 minutes')

$BODY$;

ALTER FUNCTION public.aggregate_hourly_quadrants(timestamp with time zone) OWNER TO postgres;

-- EXAMPLE:
-- select public.aggregate_hourly_quadrants(CURRENT_TIMESTAMP);