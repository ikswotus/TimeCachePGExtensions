#include "timecache.h"


PGDLLEXPORT Datum ktest_adjacency_rd(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum ktest_adjacency_arr(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(ktest_adjacency_rd);
PG_FUNCTION_INFO_V1(ktest_adjacency_arr);

/// <summary>
/// Relative difference computed as (l-r)/min(l,r)
/// </summary>
/// <param name="l"></param>
/// <param name="r"></param>
/// <returns></returns>
double relative_diff_min(double l, double r)
{
	if (l == 0.0)
		return r;
	if (r == 0.0)
		return l;
	if (l < r)
		return (l - r) / l;
	return (l - r) / r;
}

/// <summary>
/// Compute a score by checking adjacent points relative difference to a threshold
/// </summary>
/// <param name=""></param>
/// <returns></returns>
Datum ktest_adjacency_rd(PG_FUNCTION_ARGS)
{
	if (fcinfo->nargs != 2)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("2 arguments expected: points, threshold, received: %d.", fcinfo->nargs));
	}
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("NULL arrays are not supported."));
	}
	ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(arr) != 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Only 1-dimensional arrays are supported."));
	}
	Oid valueType = ARR_ELEMTYPE(arr);
	if (valueType != FLOAT8OID && valueType != FLOAT4OID && valueType != INT8OID && valueType != INT4OID)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Only arrays of FLOAT8/FLOAT4/INT8/INT4 values are supported."));
	}
	
	int arrayLength = (ARR_DIMS(arr))[0];
	if (arrayLength == 0)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Empty Array."));
	}

	float8 rdThreshold = PG_GETARG_FLOAT8(1);
	if (rdThreshold < 0) // TODO: Allow any upper bound? Not sure an upper bound is strictly necessary, 
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Relative difference threshold must be >= 0, provided: %f", rdThreshold));
	}
	if (arrayLength == 1)
	{
		PG_RETURN_INT32(0);
	}

	bool* nullFlags;
	int16 valueTypeWidth;
	bool valueTypeByValue;
	char valueTypeAlignCode;
	Datum* valueContent;

	get_typlenbyvalalign(valueType, &valueTypeWidth, &valueTypeByValue, &valueTypeAlignCode);
	deconstruct_array(arr, valueType, valueTypeWidth, valueTypeByValue, valueTypeAlignCode, &valueContent, &nullFlags, &arrayLength);

	float8* convertedArray = palloc0(sizeof(float8) * arrayLength);
	

	switch (valueType)
	{
	case FLOAT8OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = DatumGetFloat8(valueContent[i]);
		}
		break;
	case FLOAT4OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = (float8)DatumGetFloat4(valueContent[i]);
		}
		break;
	case INT8OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = (float8)DatumGetInt64(valueContent[i]);
		}
		break;
	case INT4OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = (float8)DatumGetInt32(valueContent[i]);
		}
		break;
	default:
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Unsupported OID type, only FLOAT8/FLOAT4/INT8/INT4 allowed"));
		break;
	}

	int total = 0;

	for (int a = 1; a < arrayLength; a++)
	{
		float8 rd = fabs(relative_diff_min(convertedArray[a], convertedArray[a - 1]));
		if (rd > rdThreshold)
			total++;
	}
	// wraparound case
	float8 rd = fabs(relative_diff_min(convertedArray[arrayLength - 1], convertedArray[0]));
	if (rd > rdThreshold)
		total++;

	pfree(convertedArray);

	PG_RETURN_INT32(total);
}

Datum ktest_adjacency_arr(PG_FUNCTION_ARGS)
{
	if (fcinfo->nargs != 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Only points[] is expected as input, received: %d arguments.", fcinfo->nargs));
	}
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("NULL arrays are not supported."));
	}
	ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(arr) != 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Only 1-dimensional arrays are supported."));
	}
	Oid valueType = ARR_ELEMTYPE(arr);
	if (valueType != FLOAT8OID && valueType != FLOAT4OID && valueType != INT8OID && valueType != INT4OID)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Only arrays of FLOAT8/FLOAT4/INT8/INT4 values are supported."));
	}

	int arrayLength = (ARR_DIMS(arr))[0];
	if (arrayLength == 0)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Empty Array."));
	}

	if (arrayLength == 1) //  TODO: Return 0?
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Array only contains a single value. At least 2 points are required to compute difference"));
	}

	bool* nullFlags;
	int16 valueTypeWidth;
	bool valueTypeByValue;
	char valueTypeAlignCode;
	Datum* valueContent;

	get_typlenbyvalalign(valueType, &valueTypeWidth, &valueTypeByValue, &valueTypeAlignCode);
	deconstruct_array(arr, valueType, valueTypeWidth, valueTypeByValue, valueTypeAlignCode, &valueContent, &nullFlags, &arrayLength);

	float8* convertedArray = palloc0(sizeof(float8) * arrayLength);


	switch (valueType)
	{
	case FLOAT8OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = DatumGetFloat8(valueContent[i]);
		}
		break;
	case FLOAT4OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = (float8)DatumGetFloat4(valueContent[i]);
		}
		break;
	case INT8OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = (float8)DatumGetInt64(valueContent[i]);
		}
		break;
	case INT4OID:
		for (int i = 0; i < arrayLength; i++)
		{
			if (nullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in array at index %d", i));
			}
			convertedArray[i] = (float8)DatumGetInt32(valueContent[i]);
		}
		break;
	default:
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Unsupported OID type, only FLOAT8/FLOAT4/INT8/INT4 allowed"));
		break;
	}


	float8* ret = palloc0(sizeof(float8) * arrayLength);

	for (int a = 1; a < arrayLength; a++)
	{
		ret[a] = relative_diff_min(convertedArray[a], convertedArray[a - 1]);
	}
	// wraparound case
	ret[0] = relative_diff_min(convertedArray[arrayLength - 1], convertedArray[0]);

	pfree(convertedArray);


	Datum* datum = palloc0(sizeof(Datum) * arrayLength);
	for (int i = 0; i < arrayLength; i++)
	{
		datum[i] = Float8GetDatum(ret[i]);
	}
	int16 typLen;
	bool typByVal;
	char typAlign;
	
	get_typlenbyvalalign(FLOAT8OID, &typLen, &typByVal, &typAlign);
	ArrayType* returnArray = construct_array(datum, arrayLength, FLOAT8OID, typLen, typByVal, typAlign);

	pfree(ret);

	PG_RETURN_ARRAYTYPE_P(returnArray);

	
}