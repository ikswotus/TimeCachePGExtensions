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