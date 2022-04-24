#include "timecache.h"


PGDLLEXPORT Datum kplusplus(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum ksimple(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum ksimple_all(PG_FUNCTION_ARGS);
PGDLLEXPORT Datum kplusplus_all(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(kplusplus);
PG_FUNCTION_INFO_V1(ksimple);
PG_FUNCTION_INFO_V1(ksimple_all);
PG_FUNCTION_INFO_V1(kplusplus_all);

typedef struct {
	double average;
	double min;
	double max;
	double stddev;
	int count;
} ClusterStats;

typedef struct
{
	int c_index;
	double value;
} ClusterPoint;

typedef struct
{
	ClusterPoint* points;
	int count;
	double score;
	int k;
} Cluster;

typedef struct
{
	int cluster_index;
	int count;
} ClusterCounts;

typedef struct
{
	int index;
	double difference;
} DDiff;


int sort_ints(const void* a, const void* b)
{
	return ((*(int*)a) - (*(int*)b));
}


int compare_descending_counts(const void* a, const void* b)
{
	return ((*(ClusterCounts*)b).count - (*(ClusterCounts*)a).count);
}

//reverse a to b to order by descending
int compare_ddiffs(const void* a, const void* b)
{
	return ((*(DDiff*)b).difference - (*(DDiff*)a).difference);
	/*DDiff da = (*(DDiff*)a);
	DDiff db = (*(DDiff*)b);

	if (da.difference < db.difference)
		return 1;
	if (da.difference > db.difference)
		return -1;
	return 0;*/
}

int* choose_biggest_k(double* points, int pc, int num)
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
	if(pc == 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Invalid K/PC for biggest. Getting here means another check was violated."));

	DDiff* diffs = palloc0(sizeof(DDiff) * (pc - 1));

	for (int i = 0; i < pc - 1; i++)
	{
		diffs[i].index = i;
		diffs[i].difference = fabs(points[i + 1] - points[i]);
	}
	qsort(diffs, pc - 1, sizeof(DDiff), compare_ddiffs);

	for (int i = 0; i < num; i++)
	{
		breaks[i] = diffs[i].index;
	}

	pfree(diffs);

	return breaks;
}


double cluster_standard_dev(double avg, Cluster* c, int clusterIndex)
{
	double sum = 0.0;
	if (c == NULL)
		return sum;

	int count = 0;
	for (int i = 0; i < c->count; i++)
	{
		if (c->points[i].c_index != clusterIndex)
			continue;
		count++;
		sum += pow((c->points[i].value - avg), 2);
	}
	if (count > 0)
		sum = sum / count;
	else
		return 0.0;
	return sqrt(sum);
}



void fill_cluster_stats(Cluster* c, ClusterStats* stats, int clusterIndex)
{
	if (c == NULL || c->count == 0)
	{
		stats->count = 0;
		stats->average = 0;
		stats->min = 0.0;
		stats->max = 0.0;
		stats->stddev = 0.0;
	}
	else
	{
		if (c == NULL || c->count == 0)
		{
			stats->count = 0;
			stats->average = 0.0;
			stats->min = 0.0;
			stats->max = 0.0;
			stats->stddev = 0.0;
		}
		else
		{
			bool sawPoint = false;

			double total = 0.0;
			stats->min = DBL_MAX;
			stats->max = DBL_MIN;

			for (int i = 0; i < c->count; i++)
			{
				if (c->points[i].c_index != clusterIndex)
					continue;
				double val = c->points[i].value;

				stats->count++;
				total += val;
				if (sawPoint)
				{
					if (val < stats->min)
						stats->min = val;
					if (val > stats->max)
						stats->max = val;
				}
				else
				{
					sawPoint = true;
					stats->min = val;
					stats->max = val;
				}
			}
			if (stats->count == 0)
			{
				stats->average = 0.0;
				stats->stddev = 0.0;
				stats->min = 0.0;
				stats->max = 0.0;
			}
			else
			{
				stats->average = total / stats->count;
				stats->stddev = cluster_standard_dev(stats->average, c, clusterIndex);
			}

		}
	}
	
}



ClusterStats* get_cluster_stats(Cluster* c, int clusterIndex)
{
	ClusterStats* stats = palloc0(sizeof(ClusterStats));

	fill_cluster_stats(c, stats, clusterIndex);

	return stats;
}

ClusterStats* get_all_cluster_stats(Cluster* c, int k)
{
	ClusterStats* cstats = palloc0(sizeof(ClusterStats) * k);

	for (int i = 0; i < k; i++)
	{
		fill_cluster_stats(c, &cstats[i], i);
	}

	return cstats;
}



int choose_random_index(int pc)
{
	return rand() % pc;
}

int choose_probable_index(double sum, double* distances, int pc)
{
	double cp = 0.0;
	double p = (double)rand() / RAND_MAX;
	for (int i = 0; i < pc; i++)
	{
		cp += (distances[i] / sum);
		if (cp > p)
		{
			return i;
		}
	}
	// If for some reason we failed, choose randomly (used to be pc-1, but we dont want the same point every time...)
	return choose_random_index(pc);
}


double score_cluster(double* centroids, int k, ClusterPoint* points, int pc)
{
	double* avgs = palloc0(sizeof(double) * k);
	int* counts = palloc0(sizeof(int) * k);

	for (int i = 0; i < pc; i++)
	{
		int c = points[i].c_index;
		counts[c] += 1;
		avgs[c] += points[i].value;
	}
	for (int a = 0; a < k; a++)
	{
		if(counts[a] > 0)
			avgs[a] += (avgs[a] / counts[a]);
	}
	pfree(counts);
	double total = 0.0;
	for (int i = 0; i < pc; i++)
	{
		double pv = points[i].value;
		double avg = avgs[points[i].c_index];

		total += pow((pv - avg), 2);
	}
	pfree(avgs);

	return total;
}

/// <summary>
/// Recalculate centroid for each cluster and see if it has moved
/// </summary>
/// <param name="points"></param>
/// <param name="pc"></param>
/// <param name="centroids"></param>
/// <param name="k"></param>
/// <returns></returns>
bool recalculate_centroids(ClusterPoint* points, int pc, double* centroids, int k)
{
	bool moved = false;

	for (int i = 0; i < k; i++)
	{
		double c = 0;
		int num = 0;
		for (int p = 0; p < pc; p++)
		{
			if (points[p].c_index != i) 
				continue;
			num++;
			c += points[p].value;

		}
		if (num > 0)
		{
			c = c / num;
		}
		if (centroids[i] != c)
		{
			centroids[i] = c;
			moved = true;
		}
	}


	return moved;
}

void kplus_choose(double* points, int pcount, int* indices, int k, double* centroids)
{
	double* distance = palloc0(sizeof(double) * pcount);
	double sum = 0;
	for (int i = 0; i < pcount; i++)
		sum += points[i];
	
	// Choose first index at random
	indices[0] = rand() % pcount;
	centroids[0] = points[indices[0]];

	// Compute next k-1 centroids
	for (int i = 1; i < k; i++)
	{
		for (int j = 0; j < pcount; j++)
		{
			double d = DBL_MAX;
			for (int c = 0; c < i; c++)
			{
				double temp = -fabs(points[j] - centroids[c]);
				d = min(d, temp);
			}
			distance[j] = d;
		}
		int ind = 0;
		bool in_use = false;
		// limit our loop...should be unnecessary, but for some reason
		// this gets stuck with negative numbers in postgres. It seems fine in standalone mode
		// so perhaps rand() is a different rand() when running in pg context?
		int max_attempts = 1000;
		do
		{
			in_use = false;
			ind = choose_probable_index(sum, distance, pcount);

			for (int k = i - 1; k >= 0; k--)
			{
				if (indices[k] == ind)
				{
					in_use = true;
					break;
				}
			}
		} while (in_use && max_attempts-- > 0);
		// After maxattempts- use the first available index.
		// This is not ideal. not sure why pg gets hung up
		// perhaps its best to error/fail here instead...
		if (in_use)
		{
			for (int p = 0; p < pcount; p++)
			{
				in_use = false;
				ind = p;
				for (int k = i - 1; k >= 0; k--)
				{
					if (indices[k] == ind)
					{
						in_use = true;
						break;
					}
				}
				if (!in_use)
				{
					ind = p;
					break;
				}
			}
		}
		indices[i] = ind;
		centroids[i] = points[indices[i]];
	}
	pfree(distance);
}

void kplus_choose_simple(double* points, int pcount, int* indices, int k, double* centroids)
{
	int numIndexes = k - 1;
	int* bigidx = choose_biggest_k(points, pcount,numIndexes);

	qsort(bigidx, numIndexes, sizeof(int), sort_ints);

	// Clusters will be formed by choosing points from last index up to current
	// Last cluster will be from last index to end of data
	//int lastIndex = 0;
	//int midPoint = 0;
	for (int i = 0; i < numIndexes; i++)
	{
		/*midPoint = (lastIndex + bigidx[i]);
		if (midPoint > 0)
			midPoint = ceil(midPoint / 2);*/
		indices[i] = bigidx[i];
		centroids[i] = points[indices[i]];
		//lastIndex = bigidx[i];
	}
	//midPoint = ceil((lastIndex + pcount) / 2);
	indices[numIndexes] = pcount-1;
	centroids[numIndexes] = points[indices[numIndexes]];
	pfree(bigidx);
}


/// <summary>
/// Returns int[2] indicating high/low index
/// if k=1,k=2, or k=3 clustering should be utilized
/// 
/// default values are {-1,-1}
/// 
/// if(ret[0] == -1)  no low cluster
/// if(ret[1] == -1)  no high cluster
/// 
/// This is similar to 'biggest break' index choosing, but allows for a variable k
/// so we check the edge cases
/// 
/// </summary>
/// <param name="parr"></param>
/// <param name="np"></param>
/// <param name="perc"></param>
/// <returns></returns>
int* getKIndices(double* parr, int np, double perc, int sdevs)
{
	if (sdevs < 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Standard Deviations count must be >= 1, provided: %d",sdevs));
	}

	int* indices = palloc(sizeof(int) * 2);
	indices[0] = indices[1] = -1;

	if (np <= 3)
		return indices;

	int skipCount = (int)floor(floor((1.0 - perc) * np) / 2.0);
	if (skipCount <= 0)
		skipCount = 1;

	double mavg = 0;
	int n = np - (2 * skipCount);
	if (n  < 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("No points for middle average using parameters np=%d, perc=%f", np, perc));
	}
	for (int i = skipCount; i < np - skipCount; i++)
	{
		mavg += parr[i];
	}

	mavg = mavg / n;
	double sumDiff = 0;

	for (int i = skipCount; i < np - skipCount; i++)
	{
		sumDiff += pow(parr[i] - mavg, 2);
	}
	sumDiff = sumDiff / n;
	double std = sqrt(sumDiff);


	double lowBound = mavg - (sdevs * std);
	for (int i = skipCount - 1; i >= 0; i--)
	{
		if (parr[i] < lowBound)
		{
			indices[0] = i;
			break;
		}
	}
	double highBound = mavg + (sdevs * std);
	for (int i = np - 1 - skipCount; i < np; i++)
	{
		if (parr[i] > highBound)
		{
			indices[1] = i;
			break;
		}
	}

	return indices;
}

bool reassign_points(ClusterPoint* points, int pc, double* centroids, int k)
{
	bool moved = false;
	for (int i = 0; i < pc; i++)
	{
		ClusterPoint curr = points[i];
		double dist = fabs(centroids[0] - curr.value);
		int c = 0;
		for (int j = 1; j < k; j++)
		{
			double nextd = fabs(centroids[j] - curr.value);
			if (nextd < dist)
			{
				c = j;
				dist = nextd;
			}
		}
		if (c != points[i].c_index)
		{
			points[i].c_index = c;
			moved = true;
		}
	}
	return moved;
}

void kplus_assign_c(ClusterPoint* assigned, double* points, int point_count, double* centroids, int k)
{
	for (int i = 0; i < point_count; i++)
	{
		double val = points[i];
		double dist = fabs(centroids[0] - val);
		int c = 0;
		for (int j = 1; j < k; j++)
		{
			double nextd = fabs(centroids[j] - val);
			if (nextd < dist)
			{
				c = j;
				dist = nextd;
			}
		}
		assigned[i].c_index = c;
		assigned[i].value = val;
	}
}

void kpp_c(Cluster* c, double* arr, int pc, int k, int updates)
{
	int* indices = palloc(sizeof(int) * k);
	double* centroids = palloc(sizeof(double) * k);


	kplus_choose(arr, pc, indices, k, centroids);

	c->count = pc;
	c->score = 0.0;
	kplus_assign_c(c->points, arr, pc, centroids, k);

	bool centered = false;
	bool pointed = false;
	int maxLoops = updates;

	do
	{
		centered = recalculate_centroids(c->points, pc, centroids, k);

		if (centered)
		{
			pointed = reassign_points(c->points, pc, centroids, k);
		}

	} while (maxLoops-- > 0 && centered && pointed);

	c->score = score_cluster(centroids, k, c->points, pc);

	pfree(centroids);
	pfree(indices);
}

void kpp_c_simple(Cluster* c, double* arr, int pc, int k)
{
	int* indices = palloc0(sizeof(int) * k);
	double* centroids = palloc0(sizeof(double) * k);

	kplus_choose_simple(arr, pc, indices, k, centroids);

	kplus_assign_c(c->points, arr, pc, centroids, k);

	c->score = score_cluster(centroids, k, c->points, pc);

	pfree(centroids);
	pfree(indices);
}

void kpp_c_dynamic(Cluster* c, double* arr, int pc, double threshold)
{
	int* kidx = getKIndices(arr, pc, threshold, 2);

	int k = 1;
	if (kidx[0] != -1)
		k++;
	if (kidx[1] != -1)
		k++;

	int* indices = palloc0(sizeof(int) * k);
	indices[0] = (int)floor(pc / 2);
	if (kidx[0] != -1)
	{
		indices[1] = kidx[0];
	}
	if (kidx[1] != 1)
	{
		indices[k - 1] = kidx[1];
	}
	pfree(kidx);

	double* centroids = palloc(sizeof(double) * k);
	for (int i = 0; i < k; i++)
	{
		centroids[i] = arr[indices[i]];
	}
	kplus_assign_c(c->points, arr, pc, centroids, k);

	c->score = score_cluster(centroids, k, c->points, pc);

	pfree(centroids);
	pfree(indices);

	c->k = k;
}

Cluster* internal_kplusplus(double* values, int count, int k, int seeds, int updates)
{
	//srand(time(NULL));

	Cluster* best = palloc(sizeof(Cluster));
	best->count = count;
	best->score = 0.0;
	best->points = palloc(sizeof(ClusterPoint) * count);

	if (k == 1)// shortcut - no updates
	{
		for (int i = 0; i < count; i++)
		{
			best->points[i].c_index = 0;
			best->points[i].value = values[i];
		}
		return best;
	}

	kpp_c(best, values, count, k, updates);

	if (seeds <= 1)
		return best;

	Cluster* alt = palloc(sizeof(Cluster));
	alt->points = palloc(sizeof(ClusterPoint) * count);

	Cluster* temp = NULL;
	for (int i = 0; i < seeds - 1; i++)
	{
		kpp_c(alt, values, count, k, updates);

		if (alt->score < best->score)
		{
			temp = best;
			best = alt;
			alt = temp;
		}
	}
	if (temp != NULL && alt != temp) // Make sure we didnt screw this up
		ereport(ERROR, errcode(ERRCODE_EXTERNAL_ROUTINE_EXCEPTION), errmsg("POinter swap fail..."));
	if (alt != NULL)
	{
		pfree(alt->points);
		pfree(alt);
	}

	// TODO: Sanity checks, remove these?
	if (best->points == NULL)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus - failure - cluster returned no points."));

	for (int i = 0; i < count; i++)
	{
		if (best->points[i].c_index >= k)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus - failure - cluster index > k."));
	}

	return best;
}


Cluster* internal_ksimple(double* values, int count, int k)
{
	Cluster* best = palloc(sizeof(Cluster));
	best->count = count;
	best->score = 0.0;
	best->points = palloc(sizeof(ClusterPoint) * count);
	

	if (count > 1)
		kpp_c_simple(best, values, count, k);
	else
	{
		best->points[0].c_index = 0;
		best->points[0].value = values[0];
	}

	// TODO: Sanity checks, remove these?
	if (best->points == NULL)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus - failure - cluster returned no points."));

	for (int i = 0; i < count; i++)
	{
		if (best->points[i].c_index >= k)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus - failure - cluster index > k."));
	}

	return best;
}

Cluster* internal_kdynamic(double* values, int count, double threshold)
{
	Cluster* best = palloc(sizeof(Cluster));

	best->count = count;
	best->score = 0.0;
	best->points = palloc(sizeof(ClusterPoint) * count);

	kpp_c_dynamic(best, values, count, threshold);

	return best;
}


double* get_converted_array(ArrayType* arr, Oid valueType, int array_length)
{
	bool* valsNullFlags;
	int16 valsTypeWidth;
	bool valsTypeByValue;
	char valsTypeAlignmentCode;
	Datum* valsContent;

	get_typlenbyvalalign(valueType, &valsTypeWidth, &valsTypeByValue, &valsTypeAlignmentCode);
	deconstruct_array(arr, valueType, valsTypeWidth, valsTypeByValue, valsTypeAlignmentCode, &valsContent, &valsNullFlags, &array_length);

	double* convArray = palloc0(sizeof(double) * array_length);
	for (int i = 0; i < array_length; i++)
	{
		if (valsNullFlags[i])
			ereport(ERROR, errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED), errmsg("kplusplus array null value at index %d", i));
		switch (valueType)
		{
		case FLOAT8OID:
			convArray[i] = DatumGetFloat8(valsContent[i]);
			break;
		case FLOAT4OID:
			convArray[i] = (double)DatumGetFloat4(valsContent[i]);
			break;
		case INT8OID:
			convArray[i] = (double)DatumGetInt64(valsContent[i]);
			break;
		case INT4OID:
			convArray[i] = (double)DatumGetInt32(valsContent[i]);
			break;
		default:
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus non-float/int type at index %d", i));
		}
	}

	return convArray;
}


Datum kplusplus_c(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Function call not composite."));
	if(fcinfo->nargs < 4)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus requires four arguments: points,k,seeds,updates."));
	if(PG_ARGISNULL(0))
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus called with NULL array."));
	ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
	if(ARR_NDIM(arr) != 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus only supports 1-dimensional arrays."));
	Oid valueType = ARR_ELEMTYPE(arr);

	if(valueType != FLOAT4OID && valueType != FLOAT8OID && valueType != INT8OID && valueType != INT4OID)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus supports only integer/float 4/8 types."));

	int array_length = (ARR_DIMS(arr))[0];
	if(array_length < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus empty array."));
	int k = PG_GETARG_INT32(1);
	if (k < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus k must be >= 1, given: %d", k));
	if(k > array_length)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus array length %d less than k: %d", array_length, k));

	int seeds = PG_GETARG_INT32(2);
	if (seeds < 1)
		seeds = 1;
	int updates = PG_GETARG_INT32(3);
	if (updates < 1)
		updates = 1;

	
	double* convArray = get_converted_array(arr, valueType, array_length);

	
	int c = fcinfo->nargs > 4 ? PG_GETARG_INT32(4) : k - 1;

	if (c >= k || c < 0)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus invalid cluster index given: %d", c));

	Cluster* best = internal_kplusplus(convArray, array_length, k, seeds, updates);

	pfree(convArray);

	int* counts = palloc0(sizeof(int) * k);
	ClusterCounts* ccounts = palloc0(sizeof(ClusterCounts) * k);

	for (int i = 0; i < array_length; i++)
	{
		counts[best->points[i].c_index]++;
	}
	for (int i = 0; i < k; i++)
	{
		ccounts[i].cluster_index = i;
		ccounts[i].count = counts[i];
	}
	qsort(ccounts, k, sizeof(ClusterCounts), compare_descending_counts);
	// Desired index will be k-1-c
	int bigIndex = k - 1 - c;

	int actualIndex = ccounts[bigIndex].cluster_index;

	pfree(ccounts);
	pfree(counts);


	ClusterStats* stats = get_cluster_stats(best, actualIndex);

	pfree(best);

	// Convert to record type for return
	bool isnull[5];
	for (int i = 0; i < 5; i++)
		isnull[i] = false;
	Datum retDat[5];
	retDat[0] = Float8GetDatum(stats->average);
	retDat[1] = Float8GetDatum(stats->min);
	retDat[2] = Float8GetDatum(stats->max);
	retDat[3] = Float8GetDatum(stats->stddev);
	retDat[4] = Int32GetDatum(stats->count);

	BlessTupleDesc(tupDesc);
	HeapTuple hd = heap_form_tuple(tupDesc, retDat, isnull);

	Datum d = HeapTupleGetDatum(hd);

	pfree(stats);

	PG_RETURN_DATUM(d);
}


Datum kplusplus(PG_FUNCTION_ARGS)
{
	return kplusplus_c(fcinfo);
}

Datum ksimple(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Function call not composite."));
	if (fcinfo->nargs < 2)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple requires two arguments: points,k."));
	if (PG_ARGISNULL(0))
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple called with NULL array."));
	ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(arr) != 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple only supports 1-dimensional arrays."));
	Oid valueType = ARR_ELEMTYPE(arr);

	if (valueType != FLOAT4OID && valueType != FLOAT8OID && valueType != INT8OID && valueType != INT4OID)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple supports only integer/float 4/8 types."));

	int array_length = (ARR_DIMS(arr))[0];
	if (array_length < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple empty array."));
	
	int k = PG_GETARG_INT32(1);
	if (k < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple k must be >= 1, given: %d", k));
	if (k > array_length)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple array length %d less than k: %d", array_length));

	double* convArray = get_converted_array(arr, valueType, array_length);
	

	Cluster* best = internal_ksimple(convArray, array_length, k);
	
	pfree(convArray);

	int* counts = palloc0(sizeof(int) * k);
	for (int i = 0; i < array_length; i++)
	{
		counts[best->points[i].c_index]++;
	}
	int bigIndex = 0;
	int bigIndexCounts = counts[0];
	for (int i = 0; i < k; i++)
	{
		if (counts[i] > bigIndexCounts)
		{
			bigIndexCounts = counts[i];
			bigIndex = i;
		}
	}
	pfree(counts);

	ClusterStats* stats = get_cluster_stats(best, bigIndex);

	pfree(best);

	// Convert to record type for return
	bool isnull[5];
	for (int i = 0; i < 5; i++)
		isnull[i] = false;
	Datum retDat[5];
	retDat[0] = Float8GetDatum(stats->average);
	retDat[1] = Float8GetDatum(stats->min);
	retDat[2] = Float8GetDatum(stats->max);
	retDat[3] = Float8GetDatum(stats->stddev);
	retDat[4] = Int32GetDatum(stats->count);

	BlessTupleDesc(tupDesc);
	HeapTuple hd = heap_form_tuple(tupDesc, retDat, isnull);

	Datum d = HeapTupleGetDatum(hd);

	pfree(stats);

	PG_RETURN_DATUM(d);
}

Datum knear_avg(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Function call not composite."));
	if (fcinfo->nargs != 6)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("knear_avg requires six arguments: points,k,seeds,updates,target_value,min_cluster_count."));
	if (PG_ARGISNULL(0))
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus called with NULL array."));
	ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(arr) != 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus only supports 1-dimensional arrays."));
	Oid valueType = ARR_ELEMTYPE(arr);

	if (valueType != FLOAT4OID && valueType != FLOAT8OID && valueType != INT8OID && valueType != INT4OID)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus supports only integer/float 4/8 types."));

	int array_length = (ARR_DIMS(arr))[0];
	if (array_length < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus empty array."));
	int k = PG_GETARG_INT32(1);
	if (k < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus k must be >= 1, given: %d", k));
	if (k > array_length)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus array length %d less than k: %d", array_length, k));

	int seeds = PG_GETARG_INT32(2);
	if (seeds < 1)
		seeds = 1;
	int updates = PG_GETARG_INT32(3);
	if (updates < 1)
		updates = 1;

	double target_value = PG_GETARG_FLOAT8(4);
	int min_cluster_count = PG_GETARG_INT32(5);

	if (min_cluster_count < 1)
		min_cluster_count = 1;

	double* convArray = get_converted_array(arr, valueType, array_length);


	Cluster* best = internal_kplusplus(convArray, array_length, k, seeds, updates);

	pfree(convArray);

	ClusterStats* allStats = get_all_cluster_stats(best, k);

	pfree(best);

	double closestDist = DBL_MAX;

	int index = -1;
	for (int i = 0; i < k; i++)
	{
		if (allStats[i].count >= min_cluster_count)
		{
			double dist = fabs(allStats[i].average - target_value);
			if (dist < closestDist)
			{
				closestDist = dist;
				index = i;
			}
		}
	}
	if (index < 0)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("knear_avg failed to locate valid cluster index."));
	
	double cavg = allStats[index].average;
	
	pfree(allStats);

	PG_RETURN_FLOAT8(cavg);
}

Datum knear(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Function call not composite."));
	if (fcinfo->nargs != 6)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("knear requires six arguments: points,k,seeds,updates,target_value,min_cluster_count."));
	if (PG_ARGISNULL(0))
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus called with NULL array."));
	ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(arr) != 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus only supports 1-dimensional arrays."));
	Oid valueType = ARR_ELEMTYPE(arr);

	if (valueType != FLOAT4OID && valueType != FLOAT8OID && valueType != INT8OID && valueType != INT4OID)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus supports only integer/float 4/8 types."));

	int array_length = (ARR_DIMS(arr))[0];
	if (array_length < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus empty array."));
	int k = PG_GETARG_INT32(1);
	if (k < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus k must be >= 1, given: %d", k));
	if (k > array_length)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus array length %d less than k: %d", array_length, k));

	int seeds = PG_GETARG_INT32(2);
	if (seeds < 1)
		seeds = 1;
	int updates = PG_GETARG_INT32(3);
	if (updates < 1)
		updates = 1;

	double target_value = PG_GETARG_FLOAT8(4);
	int min_cluster_count = PG_GETARG_INT32(5);

	if (min_cluster_count < 1)
		min_cluster_count = 1;

	double* convArray = get_converted_array(arr, valueType, array_length);


	Cluster* best = internal_kplusplus(convArray, array_length, k, seeds, updates);

	pfree(convArray);

	ClusterStats* allStats = get_all_cluster_stats(best, k);

	pfree(best);

	double closestDist = DBL_MAX;

	int index = -1;
	for (int i = 0; i < k; i++)
	{
		if (allStats[i].count >= min_cluster_count)
		{
			double dist = fabs(allStats[i].average - target_value);
			if (dist < closestDist)
			{
				closestDist = dist;
				index = i;
			}
		}
	}
	if (index < 0)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("knear_avg failed to locate valid cluster index."));
	
	ClusterStats stats = allStats[index];
	// Convert to record type for return
	bool isnull[5];
	for (int i = 0; i < 5; i++)
		isnull[i] = false;
	Datum retDat[5];
	retDat[0] = Float8GetDatum(stats.average);
	retDat[1] = Float8GetDatum(stats.min);
	retDat[2] = Float8GetDatum(stats.max);
	retDat[3] = Float8GetDatum(stats.stddev);
	retDat[4] = Int32GetDatum(stats.count);

	BlessTupleDesc(tupDesc);
	HeapTuple hd = heap_form_tuple(tupDesc, retDat, isnull);

	Datum d = HeapTupleGetDatum(hd);

	pfree(allStats);

	PG_RETURN_DATUM(d);
}

int getKCount(double* parr, int np, double perc, int sdevs)
{
	int k = 0;
	if (np <= 3)
		return k;

	int skipCount = (int)floor(floor((1.0 - perc) * np) / 2.0);
	if (skipCount <= 0)
		skipCount = 1;

	double mavg = 0;
	int n = np - (2 * skipCount);
	if (n < 1)
	{
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("No points for middle average using parameters np=%d, perc=%f", np, perc));
	}
	for (int i = skipCount; i < np - skipCount; i++)
	{
		mavg += parr[i];
	}

	mavg = mavg / n;
	double sumDiff = 0;

	for (int i = skipCount; i < np - skipCount; i++)
	{
		sumDiff += pow(parr[i] - mavg, 2);
	}
	sumDiff = sumDiff / n;
	double std = sqrt(sumDiff);


	double lowBound = mavg - (sdevs * std);
	for (int i = skipCount - 1; i >= 0; i--)
	{
		if (parr[i] < lowBound)
		{
			k++;
			break;
		}
	}
	double highBound = mavg + (sdevs * std);
	for (int i = np - 1 - skipCount; i < np; i++)
	{
		if (parr[i] > highBound)
		{
			k++;
			break;
		}
	}
	return k;
}

Datum kdynamic(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("Function call not composite."));
	if (fcinfo->nargs != 6)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("knear requires six arguments: points,k,seeds,updates,target_value,min_cluster_count."));
	if (PG_ARGISNULL(0))
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus called with NULL array."));
	ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
	if (ARR_NDIM(arr) != 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus only supports 1-dimensional arrays."));
	Oid valueType = ARR_ELEMTYPE(arr);

	if (valueType != FLOAT4OID && valueType != FLOAT8OID && valueType != INT8OID && valueType != INT4OID)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus supports only integer/float 4/8 types."));

	int array_length = (ARR_DIMS(arr))[0];
	if (array_length < 1)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus empty array."));
	double middle = PG_GETARG_FLOAT8(1);
	if (middle < 0 || middle > 1.0)
		ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kdynamic middle threshold percent must be [0-1.0]"));
	
	double* convArray = get_converted_array(arr, valueType, array_length);

	int k = getKCount(convArray, array_length, middle, 2);

	Cluster* best = internal_kplusplus(convArray, array_length, k, 300, 50);// TODO: Seeds,Updates args

	pfree(convArray);

	int* counts = palloc0(sizeof(int) * k);
	for (int i = 0; i < array_length; i++)
	{
		counts[best->points[i].c_index]++;
	}
	int bigIndex = 0;
	int bigIndexCounts = counts[0];
	for (int i = 0; i < k; i++)
	{
		if (counts[i] > bigIndexCounts)
		{
			bigIndexCounts = counts[i];
			bigIndex = i;
		}
	}
	pfree(counts);

	ClusterStats* stats = get_cluster_stats(best, bigIndex);

	pfree(best);

	// Convert to record type for return, include extra int for k
	bool isnull[6];
	for (int i = 0; i < 6; i++)
		isnull[i] = false;
	Datum retDat[6];
	retDat[0] = Float8GetDatum(stats->average);
	retDat[1] = Float8GetDatum(stats->min);
	retDat[2] = Float8GetDatum(stats->max);
	retDat[3] = Float8GetDatum(stats->stddev);
	retDat[4] = Int32GetDatum(stats->count);
	retDat[5] = Int32GetDatum(k);

	BlessTupleDesc(tupDesc);
	HeapTuple hd = heap_form_tuple(tupDesc, retDat, isnull);

	Datum d = HeapTupleGetDatum(hd);

	pfree(stats);

	PG_RETURN_DATUM(d);
	
}


typedef struct
{
	int current_value;
	int cluster_count;
	ClusterStats* stats;
} ksimple_fctx;



/**
 * ksimple, but returns all clusters
 * implemented as srf
 */
Datum ksimple_all(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("function returning record called in context that cannot accept type record")));
	}
	

	FuncCallContext* funcctx;
	ksimple_fctx* fctx;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		if (fcinfo->nargs < 2)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple_all requires two arguments: points,k."));
		if (PG_ARGISNULL(0))
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple_all called with NULL array."));
		ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
		if (ARR_NDIM(arr) != 1)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple_all only supports 1-dimensional arrays."));
		Oid valueType = ARR_ELEMTYPE(arr);

		if (valueType != FLOAT4OID && valueType != FLOAT8OID && valueType != INT8OID && valueType != INT4OID)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple_all supports only integer/float 4/8 types."));

		int array_length = (ARR_DIMS(arr))[0];
		if (array_length < 1)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple_all empty array."));
		int k = PG_GETARG_INT32(1);
		if (k < 1)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple_all k must be >= 1, given: %d", k));
		if (k > array_length)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("ksimple_all array length %d less than k: %d", array_length, k));
		double* convArray = get_converted_array(arr, valueType, array_length);

		MemoryContext oldcontext;


		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* allocate memory for user context */
		fctx = (ksimple_fctx*)
			palloc(sizeof(ksimple_fctx));

		/*
		 * Use fctx to keep state from call to call. Seed current with the
		 * original start value
		 */
		fctx->cluster_count = k;
		fctx->current_value = 0;
		
		Cluster* best = internal_ksimple(convArray, array_length, k);

		pfree(convArray);

		int* counts = palloc0(sizeof(int) * k);
		for (int i = 0; i < array_length; i++)
		{
			counts[best->points[i].c_index]++;
		}
		int bigIndex = 0;
		int bigIndexCounts = counts[0];
		for (int i = 0; i < k; i++)
		{
			if (counts[i] > bigIndexCounts)
			{
				bigIndexCounts = counts[i];
				bigIndex = i;
			}
		}
		pfree(counts);

		fctx->stats = get_all_cluster_stats(best, k);
		pfree(best);
	
		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	/*
	 * get the saved state and use current as the result for this iteration
	 */
	fctx = funcctx->user_fctx;
	


	if (fctx->current_value < fctx->cluster_count)
	{
		
		BlessTupleDesc(tupDesc);

		// Convert to record type for return
		bool isnull[6];
		for (int i = 0; i < 6; i++)
			isnull[i] = false;
		Datum retDat[6];

		retDat[0] = Int32GetDatum(fctx->current_value);
		retDat[1] = Float8GetDatum(fctx->stats[fctx->current_value].average);
		retDat[2] = Float8GetDatum(fctx->stats[fctx->current_value].min);
		retDat[3] = Float8GetDatum(fctx->stats[fctx->current_value].max);
		retDat[4] = Float8GetDatum(fctx->stats[fctx->current_value].stddev);
		retDat[5] = Int32GetDatum(fctx->stats[fctx->current_value].count);

		BlessTupleDesc(tupDesc);
		HeapTuple hd = heap_form_tuple(tupDesc, retDat, isnull);

		Datum d = HeapTupleGetDatum(hd);

		
		HeapTuple ht = heap_form_tuple(tupDesc, retDat, isnull);

		fctx->current_value++;


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
 * kplusplus, but returns all clusters
 * implemented as srf
 * 
 * TODO: We do basically all the work in the init(),
 * subsequent calls just retrieve stats via index...
 * Perhaps there's a better way to do this
 */
Datum kplusplus_all(PG_FUNCTION_ARGS)
{
	TupleDesc tupDesc;

	if (get_call_result_type(fcinfo, NULL, &tupDesc) != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("function returning record called in context that cannot accept type record")));
	}


	FuncCallContext* funcctx;
	ksimple_fctx* fctx;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		if (fcinfo->nargs < 4)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus_all requires four arguments: points,k,seeds,updates."));
		if (PG_ARGISNULL(0))
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus_all called with NULL array."));
		ArrayType* arr = PG_GETARG_ARRAYTYPE_P(0);
		if (ARR_NDIM(arr) != 1)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus_all only supports 1-dimensional arrays."));
		Oid valueType = ARR_ELEMTYPE(arr);

		if (valueType != FLOAT4OID && valueType != FLOAT8OID && valueType != INT8OID && valueType != INT4OID)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus_all supports only integer/float 4/8 types."));

		int array_length = (ARR_DIMS(arr))[0];
		if (array_length < 1)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus_all empty array."));
		int k = PG_GETARG_INT32(1);
		if (k < 1)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus_all k must be >= 1, given: %d", k));
		if (k > array_length)
			ereport(ERROR, errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("kplusplus_all array length %d less than k: %d", array_length, k));
		double* convArray = get_converted_array(arr, valueType, array_length);

		MemoryContext oldcontext;

		int seeds = PG_GETARG_INT32(2);
		if (seeds < 1)
			seeds = 1;
		int updates = PG_GETARG_INT32(3);
		if (updates < 1)
			updates = 1;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* allocate memory for user context */
		fctx = (ksimple_fctx*)
			palloc(sizeof(ksimple_fctx));

		/*
		 * Use fctx to keep state from call to call. Seed current with the
		 * original start value
		 */
		fctx->cluster_count = k;
		fctx->current_value = 0;

		Cluster* best = internal_kplusplus(convArray, array_length, k, seeds, updates);

		pfree(convArray);

		int* counts = palloc0(sizeof(int) * k);
		for (int i = 0; i < array_length; i++)
		{
			counts[best->points[i].c_index]++;
		}
		int bigIndex = 0;
		int bigIndexCounts = counts[0];
		for (int i = 0; i < k; i++)
		{
			if (counts[i] > bigIndexCounts)
			{
				bigIndexCounts = counts[i];
				bigIndex = i;
			}
		}
		pfree(counts);

		fctx->stats = get_all_cluster_stats(best, k);
		pfree(best);

		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	/*
	 * get the saved state and use current as the result for this iteration
	 */
	fctx = funcctx->user_fctx;



	if (fctx->current_value < fctx->cluster_count)
	{

		BlessTupleDesc(tupDesc);

		// Convert to record type for return
		bool isnull[6];
		for (int i = 0; i < 6; i++)
			isnull[i] = false;
		Datum retDat[6];

		retDat[0] = Int32GetDatum(fctx->current_value);
		retDat[1] = Float8GetDatum(fctx->stats[fctx->current_value].average);
		retDat[2] = Float8GetDatum(fctx->stats[fctx->current_value].min);
		retDat[3] = Float8GetDatum(fctx->stats[fctx->current_value].max);
		retDat[4] = Float8GetDatum(fctx->stats[fctx->current_value].stddev);
		retDat[5] = Int32GetDatum(fctx->stats[fctx->current_value].count);

		BlessTupleDesc(tupDesc);
		HeapTuple hd = heap_form_tuple(tupDesc, retDat, isnull);

		Datum d = HeapTupleGetDatum(hd);


		HeapTuple ht = heap_form_tuple(tupDesc, retDat, isnull);

		fctx->current_value++;


		/* do when there is more left to send */
		SRF_RETURN_NEXT(funcctx, HeapTupleGetDatum(ht));
	}
	else
	{
		/* do when there is no more left */
		SRF_RETURN_DONE(funcctx);
	}
}