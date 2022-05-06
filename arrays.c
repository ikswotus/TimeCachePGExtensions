#include "timecache.h"

/**
*  Array helper methods
* 
*  Intended for Quadrant points(672 total) 4* 24* 7
* 
* Inputs should be an array of points, ordered by a 'time' index:
* Index is:
	day_of_week * 96 + hour_of_day * 4 + quadrant
	where day of week is 0-6, hour_of_day is 0-23 and quadrant is 0-3
	resulting in a range of possible indices of
	0 = (0,0,0)
	through
	671 = (6,23,3)

	Expected that we will retrieve 5 elements for an 'hourly' check, but not enforced by the method.

	Returns an array containing the values of the point index for the provided indexes

	TODO: There's probably a clean/easy way to do this directly in pg already...
* 
*/
PGDLLEXPORT Datum quadrants_from_points(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(quadrants_from_points);

Datum quadrants_from_points(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array was null"));
	}
	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Quadrant index array was null"));
	}

	ArrayType* parr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(parr) != 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array was not one-dimensional: %d", ARR_DIMS(parr)));
	}
	ArrayType* iarr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(iarr) != 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Quadrant index array was not one-dimensional: %d", ARR_DIMS(iarr)));
	}

	Oid pointValueType = ARR_ELEMTYPE(parr);
	if (pointValueType != FLOAT8OID && pointValueType != FLOAT4OID && pointValueType != INT8OID && pointValueType != INT4OID)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array  must be of type: FLOAT8/FLOAT4/INT8/INT4."));
	}
	Oid indexValueType = ARR_ELEMTYPE(iarr);
	if (indexValueType != INT8OID && indexValueType != INT4OID)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Quadrant index array  must be of type: INT8/INT4."));
	}

	int pointArrayLength = (ARR_DIMS(parr))[0];
	if (pointArrayLength != 672)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array length not the expected 672. Input point array contained %d elements", pointArrayLength));
	}

	int indexArrayLength = (ARR_DIMS(iarr))[0];
    if (indexArrayLength != 5)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Quadrant index array length not the expected 5. Input point array contained %d elements", indexArrayLength));
	}

	// Convert points
	bool* pNullFlags;
	int16 pValueTypeWidth;
	bool pValueTypeByValue;
	char pValueTypeAlignCode;
	Datum* pValueContent;

	get_typlenbyvalalign(pointValueType, &pValueTypeWidth, &pValueTypeByValue, &pValueTypeAlignCode);
	deconstruct_array(parr, pointValueType, pValueTypeWidth, pValueTypeByValue, pValueTypeAlignCode, &pValueContent, &pNullFlags, &pointArrayLength);

	// Convert Quad indices
	bool* iNullFlags;
	int16 iValueTypeWidth;
	bool iValueTypeByValue;
	char iValueTypeAlignCode;
	Datum* iValueContent;

	get_typlenbyvalalign(indexValueType, &iValueTypeWidth, &iValueTypeByValue, &iValueTypeAlignCode);
	deconstruct_array(iarr, indexValueType, iValueTypeWidth, iValueTypeByValue, iValueTypeAlignCode, &iValueContent, &iNullFlags, &indexArrayLength);

	float8* convertedPointArray = palloc0(sizeof(float8) * pointArrayLength);
	switch (pointValueType)
	{
	case FLOAT8OID:
		for (int i = 0; i < pointArrayLength; i++)
		{
			if (pNullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in point array at index %d", i));
			}
			convertedPointArray[i] = DatumGetFloat8(pValueContent[i]);
		}
		break;
	case FLOAT4OID:
		for (int i = 0; i < pointArrayLength; i++)
		{
			if (pNullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in point array at index %d", i));
			}
			convertedPointArray[i] = (float8)DatumGetFloat4(pValueContent[i]);
		}
		break;
	case INT8OID:
		for (int i = 0; i < pointArrayLength; i++)
		{
			if (pNullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in point array at index %d", i));
			}
			convertedPointArray[i] = (float8)DatumGetInt64(pValueContent[i]);
		}
		break;
	case INT4OID:
		for (int i = 0; i < pointArrayLength; i++)
		{
			if (pNullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in point array at index %d", i));
			}
			convertedPointArray[i] = (float8)DatumGetInt32(pValueContent[i]);
		}
		break;
	default:
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Unsupported OID type, only FLOAT8/FLOAT4/INT8/INT4 allowed"));
		break;
	}


	int32* convertedIndexArray = palloc0(sizeof(int32) * indexArrayLength);
	switch (indexValueType)
	{
	case INT8OID:
		for (int i = 0; i < indexArrayLength; i++)
		{
			if (iNullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in index array at index %d", i));
			}
			convertedIndexArray[i] = (int32)DatumGetInt64(iValueContent[i]);
		}
		break;
	case INT4OID:
		for (int i = 0; i < indexArrayLength; i++)
		{
			if (iNullFlags[i])
			{
				//TODO: Allow nulls and treat as zero?
				ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Null value in index array at index %d", i));
			}
			convertedIndexArray[i] = (int32)DatumGetInt32(iValueContent[i]);
		}
		break;
	default:
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Unsupported OID type for index, only INT8/INT4 allowed"));
		break;
	}

	float8* ret = palloc0(sizeof(float8) * indexArrayLength);
	for (int i = 0; i < indexArrayLength; i++)
	{
		int qidx = convertedIndexArray[i];
		if (qidx > 671 || qidx < 0)
		{
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Invalid value in index array at position %d, value must be [0-671], received: %d", i, qidx));
		}
		ret[i] = convertedPointArray[convertedIndexArray[i]];
	}
	pfree(convertedPointArray);
	pfree(convertedIndexArray);


	Datum* datum = palloc0(sizeof(Datum) * indexArrayLength);
	for (int i = 0; i < indexArrayLength; i++)
		datum[i] = Float8GetDatum(ret[i]);

	int16 typLen;
	bool typByVal;
	char typAlign;

	get_typlenbyvalalign(FLOAT8OID, &typLen, &typByVal, &typAlign);
	ArrayType* returnArray = construct_array(datum, indexArrayLength, FLOAT8OID, typLen, typByVal, typAlign);

	pfree(ret);

	PG_RETURN_ARRAYTYPE_P(returnArray);

}