#include "timecache.h"

/**
*  Array helper methods
* 
*  Intended for Quadrant points(672 total) 4* 24* 7
*/
PGDLLEXPORT Datum quadrants_from_points(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum biggest_breaks(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(quadrants_from_points);
PG_FUNCTION_INFO_V1(biggest_breaks);

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
	ArrayType* iarr = PG_GETARG_ARRAYTYPE_P(1);
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



typedef struct
{
	int index;
	double difference;
} Diff;

//reverse a to b to order by descending
int compare_diffs(const void* a, const void* b)
{
	return ((*(Diff*)b).difference - (*(Diff*)a).difference);
}



int* kbig(double* points, int pc, int num)
{
	if (pc < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("At least 2 points are required for ksimple"));

	int* breaks = palloc0(sizeof(int) * num);

	if (pc == num)
	{
		// Edge case - could error since we cant compute diffs, but
		// each point is its own cluster, so thats 'simple' enough...
		for (int i = 0; i < num; i++)
			breaks[i] = i;
		return breaks;
	}
	// Shouldnt ever happen...other checks should prevent this from being called.
	if (pc == 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Invalid K/PC for biggest. Getting here means another check was violated."));

	Diff* diffs = palloc0(sizeof(Diff) * (pc - 1));

	for (int i = 0; i < pc - 1; i++)
	{
		diffs[i].index = i;
		diffs[i].difference = fabs(points[i + 1] - points[i]);
	}
	qsort(diffs, pc - 1, sizeof(Diff), compare_diffs);

	for (int i = 0; i < num; i++)
	{
		breaks[i] = diffs[i].index;
	}

	pfree(diffs);

	return breaks;
}




int order_ints(const void* a, const void* b)
{
	return ((*(int*)a) - (*(int*)b));
}Datum biggest_breaks(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array was null"));
	}
	
	ArrayType* parr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(parr) != 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array was not one-dimensional: %d", ARR_DIMS(parr)));
	}
	

	Oid pointValueType = ARR_ELEMTYPE(parr);
	if (pointValueType != FLOAT8OID && pointValueType != FLOAT4OID && pointValueType != INT8OID && pointValueType != INT4OID)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array  must be of type: FLOAT8/FLOAT4/INT8/INT4."));
	}
	

	int pointArrayLength = (ARR_DIMS(parr))[0];
	if (pointArrayLength <= 0)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Point array  contained %d elements", pointArrayLength));
	}



	// Convert points
	bool* pNullFlags;
	int16 pValueTypeWidth;
	bool pValueTypeByValue;
	char pValueTypeAlignCode;
	Datum* pValueContent;

	get_typlenbyvalalign(pointValueType, &pValueTypeWidth, &pValueTypeByValue, &pValueTypeAlignCode);
	deconstruct_array(parr, pointValueType, pValueTypeWidth, pValueTypeByValue, pValueTypeAlignCode, &pValueContent, &pNullFlags, &pointArrayLength);

	
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
	pfree(convertedPointArray);
	
	int k = PG_GETARG_INT32(1);
	int originalK = k;
	// for k=3, we want 3 clusters, so we choose 2 indices
	k = k - 1;

	// TODO: Biggest breaks...
	int* kBig = kbig(convertedPointArray, pointArrayLength, k);


	qsort(kBig, k, sizeof(int), order_ints);

	//qsort(bigidx, numIndexes, sizeof(int), sort_ints);

	int* indices = palloc0(sizeof(int) * originalK);
	// Clusters will be formed by choosing points from last index up to current
	// Last cluster will be from last index to end of data
	int lastIndex = 0;
	int midPoint = 0;
	for (int i = 0; i < k; i++)
	{
		midPoint = (lastIndex + kBig[i]);
		if (midPoint > 0)
			midPoint = ceil(midPoint / 2);
		indices[i] = midPoint;
		//centroids[i] = points[indices[i]];
		lastIndex = kBig[i];
	}
	midPoint = ceil((lastIndex + pointArrayLength) / 2);
	indices[k] = midPoint;
	//centroids[numIndexes] = points[indices[numIndexes]];



	Datum* datum = palloc0(sizeof(Datum) * k);
	for (int i = 0; i < k; i++)
		datum[i] = Int32GetDatum(kBig[i]);

	int16 typLen;
	bool typByVal;
	char typAlign;

	get_typlenbyvalalign(INT8OID, &typLen, &typByVal, &typAlign);
	ArrayType* returnArray = construct_array(datum, k, INT8OID, typLen, typByVal, typAlign);

	pfree(kBig);

	PG_RETURN_ARRAYTYPE_P(returnArray);

}