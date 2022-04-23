#include "timecache.h"


#include "pgtime.h"
/***
* 
* ref: src/backend/utils/adt/timestamp.c
* 
* Mostly a copy/paste of generate_series_timestamptz
* we just return a tuple, pairing the timestamp with a value
* 
* Current Functions:
* generate_sinewave_series() - uses a sin() accounting for minute/hour
* generate_randomwalk_series() - randomw walk, allows specifying the step
* 
* TODO: Control granularity? Only include seconds factor if interval < 1m?
* 
* TODO: Figure out other numerical types? Shared methods for float8/int? Or separate but similar...See how internal generate_series works
* 
* 
* UGH - Cant seem to link some of the datetime/timestamptz stuff
* So a few things are replicated here directly from datetime.c/timestamp.c
* day_tab
* interval_cmp_value
* interval_cmp_internal
* timestamptz_pl_interval
*/

PG_MODULE_MAGIC;

PGDLLEXPORT Datum generate_sinewave_series(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum generate_randomwalk_series(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(generate_sinewave_series);
PG_FUNCTION_INFO_V1(generate_randomwalk_series);


typedef struct
{
	TimestampTz current_time;
	TimestampTz finish_time;
	Interval time_step;
	int time_step_sign;
	int current_value;
	int value_step;
} generate_series_randomwalk_fctx;

// Container for storing sinewave generation values
typedef struct
{
	TimestampTz current;
	TimestampTz finish;
	Interval	step;
	int			step_sign;
	int period;
	int amplitude;
} 
generate_series_sinewave_fctx;

/// <summary>
/// Random between 0.0 and 1.0
/// </summary>
/// <returns></returns>
double randd()
{
	return (rand() / (RAND_MAX / (1.0)));
}


#ifndef M_PI
	#define M_PI 3.14159265358979323846
#endif


#define pifactor M_PI / 180.0


const int day_tab[2][13] =
{
	{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0},
	{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0}
};

/*
 *		interval_relop	- is interval1 relop interval2
 *
 * Interval comparison is based on converting interval values to a linear
 * representation expressed in the units of the time field (microseconds,
 * in the case of integer timestamps) with days assumed to be always 24 hours
 * and months assumed to be always 30 days.  To avoid overflow, we need a
 * wider-than-int64 datatype for the linear representation, so use INT128.
 */

static inline INT128
interval_cmp_value(const Interval* interval)
{
	INT128		span;
	int64		dayfraction;
	int64		days;

	/*
	 * Separate time field into days and dayfraction, then add the month and
	 * day fields to the days part.  We cannot overflow int64 days here.
	 */
	dayfraction = interval->time % USECS_PER_DAY;
	days = interval->time / USECS_PER_DAY;
	days += interval->month * INT64CONST(30);
	days += interval->day;

	/* Widen dayfraction to 128 bits */
	span = int64_to_int128(dayfraction);

	/* Scale up days to microseconds, forming a 128-bit product */
	int128_add_int64_mul_int64(&span, days, USECS_PER_DAY);

	return span;
}

static int
interval_cmp_internal(Interval* interval1, Interval* interval2)
{
	INT128		span1 = interval_cmp_value(interval1);
	INT128		span2 = interval_cmp_value(interval2);

	return int128_compare(span1, span2);
}

/* timestamptz_pl_interval()
 * Add an interval to a timestamp with time zone data type.
 * Note that interval has provisions for qualitative year/month
 *	units, so try to do the right thing with them.
 * To add a month, increment the month, and use the same day of month.
 * Then, if the next month has fewer days, set the day of month
 *	to the last day of month.
 * Lastly, add in the "quantitative time".
 */
Datum
timestamptz_pl_interval(PG_FUNCTION_ARGS)
{
	TimestampTz timestamp = PG_GETARG_TIMESTAMPTZ(0);
	Interval* span = PG_GETARG_INTERVAL_P(1);
	TimestampTz result;
	int			tz;

	if (TIMESTAMP_NOT_FINITE(timestamp))
		result = timestamp;
	else
	{
		if (span->month != 0)
		{
			struct pg_tm tt,
				* tm = &tt;
			fsec_t		fsec;

			if (timestamp2tm(timestamp, &tz, tm, &fsec, NULL, NULL) != 0)
				ereport(ERROR,
					(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
						errmsg("timestamp out of range")));

			tm->tm_mon += span->month;
			if (tm->tm_mon > MONTHS_PER_YEAR)
			{
				tm->tm_year += (tm->tm_mon - 1) / MONTHS_PER_YEAR;
				tm->tm_mon = ((tm->tm_mon - 1) % MONTHS_PER_YEAR) + 1;
			}
			else if (tm->tm_mon < 1)
			{
				tm->tm_year += tm->tm_mon / MONTHS_PER_YEAR - 1;
				tm->tm_mon = tm->tm_mon % MONTHS_PER_YEAR + MONTHS_PER_YEAR;
			}

			/* adjust for end of month boundary problems... */
			if (tm->tm_mday > day_tab[isleap(tm->tm_year)][tm->tm_mon - 1])
				tm->tm_mday = (day_tab[isleap(tm->tm_year)][tm->tm_mon - 1]);

			tz = DetermineTimeZoneOffset(tm, session_timezone);

			if (tm2timestamp(tm, fsec, &tz, &timestamp) != 0)
				ereport(ERROR,
					(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
						errmsg("timestamp out of range")));
		}

		if (span->day != 0)
		{
			struct pg_tm tt,
				* tm = &tt;
			fsec_t		fsec;
			int			julian;

			if (timestamp2tm(timestamp, &tz, tm, &fsec, NULL, NULL) != 0)
				ereport(ERROR,
					(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
						errmsg("timestamp out of range")));

			/* Add days by converting to and from Julian */
			julian = date2j(tm->tm_year, tm->tm_mon, tm->tm_mday) + span->day;
			j2date(julian, &tm->tm_year, &tm->tm_mon, &tm->tm_mday);

			tz = DetermineTimeZoneOffset(tm, session_timezone);

			if (tm2timestamp(tm, fsec, &tz, &timestamp) != 0)
				ereport(ERROR,
					(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
						errmsg("timestamp out of range")));
		}

		timestamp += span->time;

		if (!IS_VALID_TIMESTAMP(timestamp))
			ereport(ERROR,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
					errmsg("timestamp out of range")));

		result = timestamp;
	}

	PG_RETURN_TIMESTAMP(result);
}




/**
 * COPIED from generate_series_timestamptz()
 * Generate the set of timestamps from start to finish by step
 * But also include a value that corresponds to the sine wav
 */
Datum generate_sinewave_series(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo,NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("function returning record called in context that cannot accept type record")));
	}

	if (PG_NARGS() < 4)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("generate_sinewave_series requires at least 4 arguments: start, end, interval, period. Optionally an 5th arg: amplitude")));
	}

	FuncCallContext* funcctx;
	generate_series_sinewave_fctx* fctx;
	TimestampTz result;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		TimestampTz start = PG_GETARG_TIMESTAMPTZ(0);
		TimestampTz finish = PG_GETARG_TIMESTAMPTZ(1);
		Interval* step = PG_GETARG_INTERVAL_P(2);
		int period = PG_GETARG_INT32(3);
		if (period <= 0)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("period minutes must be > 0")));
		}

		MemoryContext oldcontext;
		Interval	interval_zero;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* allocate memory for user context */
		fctx = (generate_series_sinewave_fctx*)
			palloc(sizeof(generate_series_sinewave_fctx));

		/*
		 * Use fctx to keep state from call to call. Seed current with the
		 * original start value
		 */
		fctx->current = start;
		fctx->finish = finish;
		fctx->step = *step;
		fctx->period = period;

		if (PG_NARGS() == 5)
			fctx->amplitude = PG_GETARG_INT32(4);
		else
			fctx->amplitude = 1;
		

		/* Determine sign of the interval */
		MemSet(&interval_zero, 0, sizeof(Interval));
		fctx->step_sign = interval_cmp_internal(&fctx->step, &interval_zero);

		if (fctx->step_sign == 0)
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("step size cannot equal zero")));

		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	/*
	 * get the saved state and use current as the result for this iteration
	 */
	fctx = funcctx->user_fctx;
	result = fctx->current;

	if (fctx->step_sign > 0 ?
		timestamp_cmp_internal(result, fctx->finish) <= 0 :
		timestamp_cmp_internal(result, fctx->finish) >= 0)
	{
		/* increment current in preparation for next iteration */
		fctx->current = DatumGetTimestampTz(DirectFunctionCall2(timestamptz_pl_interval,
			TimestampTzGetDatum(fctx->current),
			PointerGetDatum(&fctx->step)));

		
		BlessTupleDesc(tupDesc);

		bool isNull[2];
		isNull[0] = isNull[1] = false;
		Datum ret[2];
		ret[0] = TimestampTzGetDatum(result);
		
		
		fsec_t fsec;
		struct pg_tm tm;
		int tz;
		if (timestamp2tm(result, &tz, &tm, &fsec, NULL, session_timezone) != 0)
		{
			ereport(ERROR,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
					errmsg("timestamp out of range")));
		}

		// TODO: If interval > 1m, we dont really need to bother with second adjustment (Could make the same argument for minutes if > 1h)
		double aval = fmod(tm.tm_hour, (fctx->period / 60.0)) * (21600.0 / fctx->period) + tm.tm_min * (360.0 / fctx->period) + tm.tm_sec * (6.0 / fctx->period);

		ret[1] = Float8GetDatum(sin(aval * pifactor) * fctx->amplitude);

		HeapTuple ht = heap_form_tuple(tupDesc, ret, isNull);


		/* do when there is more left to send */
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(ht));
	}
	else
	{
		/* do when there is no more left */
		SRF_RETURN_DONE(funcctx);
	}
}



/**
 * COPIED from generate_series_timestamptz()
 * Generate the set of timestamps from start to finish by step
 * But also include a value that is a random walk from each point
 */
Datum generate_randomwalk_series(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("function returning record called in context that cannot accept type record")));
	}

	if (PG_NARGS() < 4)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("generate_sinewave_series requires at least 4 arguments: start, end, interval, period. Optionally an 5th arg: amplitude")));
	}

	FuncCallContext* funcctx;
	generate_series_randomwalk_fctx* fctx;
	TimestampTz result;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		TimestampTz start = PG_GETARG_TIMESTAMPTZ(0);
		TimestampTz finish = PG_GETARG_TIMESTAMPTZ(1);
		Interval* step = PG_GETARG_INTERVAL_P(2);
		int val_step = PG_GETARG_INT32(3);
		if (val_step <= 0)
		{
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("value step must be > 0")));
		}
	

		MemoryContext oldcontext;
		Interval	interval_zero;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* allocate memory for user context */
		fctx = (generate_series_randomwalk_fctx*)
			palloc(sizeof(generate_series_randomwalk_fctx));

		/*
		 * Use fctx to keep state from call to call. Seed current with the
		 * original start value
		 */
		fctx->current_time = start;
		fctx->finish_time = finish;
		fctx->time_step = *step;
		fctx->value_step = val_step;

		if (PG_NARGS() == 5)
		{
			fctx->current_value = PG_GETARG_INT32(4);
		}
		else
		{
			fctx->current_value = 0;
		}


		/* Determine sign of the interval */
		MemSet(&interval_zero, 0, sizeof(Interval));
		fctx->time_step_sign = interval_cmp_internal(&fctx->time_step, &interval_zero);

		if (fctx->time_step_sign == 0)
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("step size cannot equal zero")));

		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	/*
	 * get the saved state and use current as the result for this iteration
	 */
	fctx = funcctx->user_fctx;
	result = fctx->current_time;

	if (fctx->time_step_sign > 0 ?
		timestamp_cmp_internal(result, fctx->finish_time) <= 0 :
		timestamp_cmp_internal(result, fctx->finish_time) >= 0)
	{
		/* increment current in preparation for next iteration */
		fctx->current_time = DatumGetTimestampTz(DirectFunctionCall2(timestamptz_pl_interval,
			TimestampTzGetDatum(fctx->current_time),
			PointerGetDatum(&fctx->time_step)));


		BlessTupleDesc(tupDesc);

		bool isNull[2];
		isNull[0] = isNull[1] = false;
		Datum ret[2];
		ret[0] = TimestampTzGetDatum(result);


		fsec_t fsec;
		struct pg_tm tm;
		int tz;
		if (timestamp2tm(result, &tz, &tm, &fsec, NULL, session_timezone) != 0)
		{
			ereport(ERROR,
				(errcode(ERRCODE_DATETIME_VALUE_OUT_OF_RANGE),
					errmsg("timestamp out of range")));
		}

		double r = randd();
		if (r < 0.5)
		{
			fctx->current_value = fctx->current_value + fctx->value_step;
		}
		else
		{
			fctx->current_value = fctx->current_value - fctx->value_step;
		}

		ret[1] = Int8GetDatum(fctx->current_value);

		HeapTuple ht = heap_form_tuple(tupDesc, ret, isNull);


		/* do when there is more left to send */
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(ht));
	}
	else
	{
		/* do when there is no more left */
		SRF_RETURN_DONE(funcctx);
	}
}