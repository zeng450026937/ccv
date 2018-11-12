#include "case.h"
#include "ccv_case.h"
#include "ccv_nnc_case.h"
#include <ccv.h>
#include <nnc/ccv_nnc.h>
#include <nnc/ccv_nnc_easy.h>
#include "3rdparty/dsfmt/dSFMT.h"

TEST_SETUP()
{
	ccv_nnc_init();
}

static int _ccv_iter_accessed = 0;

static void _ccv_iter_int(const int column_idx, const int row_idx, const int row_size, void** const data, void* const context, ccv_nnc_stream_context_t* const stream_context)
{
	int* const array = (int*)context;
	int i;
	for (i = 0; i < row_size; i++)
		*data = (void*)(intptr_t)array[row_idx + i];
	++_ccv_iter_accessed;
}

TEST_CASE("iterate through a simple dataframe")
{
	int int_array[8] = {
		0, 3, 4, 5, 6, 7, 8, 9
	};
	ccv_cnnp_column_data_t columns[] = {
		{
			.data_enum = _ccv_iter_int,
			.context = int_array,
		}
	};
	ccv_cnnp_dataframe_t* const dataframe = ccv_cnnp_dataframe_new(columns, sizeof(columns) / sizeof(columns[0]), 8);
	ccv_cnnp_dataframe_iter_t* const iter1 = ccv_cnnp_dataframe_iter_new(dataframe, COLUMN_ID_LIST(0));
	int result[8];
	int i = 0;
	void* data;
	while (0 == ccv_cnnp_dataframe_iter_next(iter1, &data, 1, 0))
		result[i++] = (int)(intptr_t)data;
	ccv_cnnp_dataframe_iter_free(iter1);
	REQUIRE_ARRAY_EQ(int, int_array, result, 8, "iterated result and actual result should be the same");
	// iter2 test some prefetch capacities.
	ccv_cnnp_dataframe_iter_t* const iter2 = ccv_cnnp_dataframe_iter_new(dataframe, COLUMN_ID_LIST(0));
	for (i = 0; i < 3; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter2, 1, 0);
	_ccv_iter_accessed = 0;
	for (i = 0; i < 3; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter2, &data, 1, 0);
		result[i] = (int)(intptr_t)data;
	}
	REQUIRE_EQ(_ccv_iter_accessed, 0, "the iterator is not accessed at all, because prefetching");
	ccv_cnnp_dataframe_iter_next(iter2, &data, 1, 0);
	result[3] = (int)(intptr_t)data;
	REQUIRE_EQ(_ccv_iter_accessed, 1, "the iterator is accessed, because no prefetching");
	ccv_cnnp_dataframe_iter_prefetch(iter2, 1, 0);
	REQUIRE_EQ(_ccv_iter_accessed, 2, "the iterator is accessed again, for prefetching");
	_ccv_iter_accessed = 0;
	for (i = 4; i < 8; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter2, &data, 1, 0);
		result[i] = (int)(intptr_t)data;
	}
	REQUIRE_EQ(_ccv_iter_accessed, 3, "the iterator accessed 3 times, the first is prefetching");
	ccv_cnnp_dataframe_iter_free(iter2);
	REQUIRE_ARRAY_EQ(int, int_array, result, 8, "iterated result and actual result should be the same");
	// iter3 test more prefetch behavior.
	ccv_cnnp_dataframe_iter_t* const iter3 = ccv_cnnp_dataframe_iter_new(dataframe, COLUMN_ID_LIST(0));
	for (i = 0; i < 3; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
	for (i = 0; i < 2; i++)
	{
		_ccv_iter_accessed = 0;
		ccv_cnnp_dataframe_iter_next(iter3, &data, 1, 0);
		REQUIRE_EQ(_ccv_iter_accessed, 0, "no iterator accessed");
		result[i] = (int)(intptr_t)data;
		ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
		REQUIRE_EQ(_ccv_iter_accessed, 1, "iterator accessed for prefetching");
	}
	for (i = 2; i < 4; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
	_ccv_iter_accessed = 0;
	for (i = 2; i < 7; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter3, &data, 1, 0);
		result[i] = (int)(intptr_t)data;
	}
	REQUIRE_EQ(_ccv_iter_accessed, 0, "no iterator accessed");
	ccv_cnnp_dataframe_iter_next(iter3, &data, 1, 0);
	result[7] = (int)(intptr_t)data;
	REQUIRE_EQ(_ccv_iter_accessed, 1, "iterator accessed twice");
	const int fail1 = ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
	REQUIRE_EQ(fail1, -1, "cannot advance no more");
	ccv_cnnp_dataframe_iter_free(iter3);
	REQUIRE_ARRAY_EQ(int, int_array, result, 8, "iterated result and actual result should be the same");
	ccv_cnnp_dataframe_free(dataframe);
}

static void _ccv_int_plus_1(void*** const column_data, const int column_size, const int batch_size, void** const data, void* const context, ccv_nnc_stream_context_t* const stream_context)
{
	int i;
	for (i = 0; i < batch_size; i++)
	{
		int k = (int)(intptr_t)column_data[0][i];
		data[i] = (void*)(intptr_t)(k + 1);
	}
}

TEST_CASE("iterate through derived column")
{
	int int_array[8] = {
		2, 3, 4, 5, 6, 7, 8, 9
	};
	ccv_cnnp_column_data_t columns[] = {
		{
			.data_enum = _ccv_iter_int,
			.context = int_array,
		}
	};
	ccv_cnnp_dataframe_t* const dataframe = ccv_cnnp_dataframe_new(columns, sizeof(columns) / sizeof(columns[0]), 8);
	const int derived = ccv_cnnp_dataframe_map(dataframe, _ccv_int_plus_1, 0, COLUMN_ID_LIST(0), 0);
	assert(derived > 0);
	ccv_cnnp_dataframe_iter_t* const iter = ccv_cnnp_dataframe_iter_new(dataframe, COLUMN_ID_LIST(derived));
	int result[8];
	int i = 0;
	void* data;
	while (0 == ccv_cnnp_dataframe_iter_next(iter, &data, 1, 0))
		result[i++] = (int)(intptr_t)data;
	ccv_cnnp_dataframe_iter_free(iter);
	for (i = 0; i < 8; i++)
		++int_array[i];
	REQUIRE_ARRAY_EQ(int, int_array, result, 8, "iterated result and actual result should be the same");
	// iter2 test some prefetch capacities.
	ccv_cnnp_dataframe_iter_t* const iter2 = ccv_cnnp_dataframe_iter_new(dataframe, COLUMN_ID_LIST(derived));
	for (i = 0; i < 3; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter2, 1, 0);
	_ccv_iter_accessed = 0;
	for (i = 0; i < 3; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter2, &data, 1, 0);
		result[i] = (int)(intptr_t)data;
	}
	REQUIRE_EQ(_ccv_iter_accessed, 0, "the iterator is not accessed at all, because prefetching");
	ccv_cnnp_dataframe_iter_next(iter2, &data, 1, 0);
	result[3] = (int)(intptr_t)data;
	REQUIRE_EQ(_ccv_iter_accessed, 1, "the iterator is accessed, because no prefetching");
	ccv_cnnp_dataframe_iter_prefetch(iter2, 1, 0);
	REQUIRE_EQ(_ccv_iter_accessed, 2, "the iterator is accessed again, for prefetching");
	_ccv_iter_accessed = 0;
	for (i = 4; i < 6; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter2, &data, 1, 0);
		result[i] = (int)(intptr_t)data;
	}
	REQUIRE_EQ(_ccv_iter_accessed, 1, "the iterator accessed 3 times, the first is prefetching");
	const int success0 = ccv_cnnp_dataframe_iter_prefetch(iter2, 1, 0);
	REQUIRE_EQ(success0, 0, "success");
	const int success1 = ccv_cnnp_dataframe_iter_prefetch(iter2, 1, 0);
	REQUIRE_EQ(success1, 0, "success");
	const int fail0 = ccv_cnnp_dataframe_iter_prefetch(iter2, 1, 0);
	REQUIRE_EQ(fail0, -1, "should fail");
	for (i = 0; i < 6; i++)
		++int_array[i];
	ccv_cnnp_dataframe_iter_free(iter2);
	REQUIRE_ARRAY_EQ(int, int_array, result, 6, "iterated result and actual result should be the same up to 6");
	// iter3 test more prefetch behavior.
	ccv_cnnp_dataframe_iter_t* const iter3 = ccv_cnnp_dataframe_iter_new(dataframe, COLUMN_ID_LIST(derived));
	for (i = 0; i < 3; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
	for (i = 0; i < 2; i++)
	{
		_ccv_iter_accessed = 0;
		ccv_cnnp_dataframe_iter_next(iter3, &data, 1, 0);
		REQUIRE_EQ(_ccv_iter_accessed, 0, "no iterator accessed");
		result[i] = (int)(intptr_t)data;
		ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
		REQUIRE_EQ(_ccv_iter_accessed, 1, "iterator accessed for prefetching");
	}
	for (i = 2; i < 4; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
	_ccv_iter_accessed = 0;
	for (i = 2; i < 7; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter3, &data, 1, 0);
		result[i] = (int)(intptr_t)data;
	}
	REQUIRE_EQ(_ccv_iter_accessed, 0, "no iterator accessed");
	ccv_cnnp_dataframe_iter_next(iter3, &data, 1, 0);
	result[7] = (int)(intptr_t)data;
	REQUIRE_EQ(_ccv_iter_accessed, 1, "iterator accessed twice");
	const int fail1 = ccv_cnnp_dataframe_iter_prefetch(iter3, 1, 0);
	REQUIRE_EQ(fail1, -1, "cannot advance no more");
	for (i = 0; i < 8; i++)
		++int_array[i];
	ccv_cnnp_dataframe_iter_free(iter3);
	REQUIRE_ARRAY_EQ(int, int_array, result, 8, "iterated result and actual result should be the same");
	ccv_cnnp_dataframe_free(dataframe);
}

TEST_CASE("iterate through derived column with cursor reset")
{
	int int_array[8] = {
		2, 3, 4, 5, 6, 7, 8, 9
	};
	ccv_cnnp_column_data_t columns[] = {
		{
			.data_enum = _ccv_iter_int,
			.context = int_array,
		}
	};
	ccv_cnnp_dataframe_t* const dataframe = ccv_cnnp_dataframe_new(columns, sizeof(columns) / sizeof(columns[0]), 8);
	const int derived = ccv_cnnp_dataframe_map(dataframe, _ccv_int_plus_1, 0, COLUMN_ID_LIST(0), 0);
	assert(derived > 0);
	int i;
	void* data;
	int result[8];
	ccv_cnnp_dataframe_iter_t* const iter = ccv_cnnp_dataframe_iter_new(dataframe, COLUMN_ID_LIST(derived));
	for (i = 0; i < 3; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter, 1, 0);
	for (i = 0; i < 2; i++)
	{
		_ccv_iter_accessed = 0;
		ccv_cnnp_dataframe_iter_next(iter, &data, 1, 0);
		REQUIRE_EQ(_ccv_iter_accessed, 0, "no iterator accessed");
		result[i] = (int)(intptr_t)data;
		ccv_cnnp_dataframe_iter_prefetch(iter, 1, 0);
		REQUIRE_EQ(_ccv_iter_accessed, 1, "iterator accessed for prefetching");
	}
	for (i = 2; i < 4; i++)
		ccv_cnnp_dataframe_iter_prefetch(iter, 1, 0);
	ccv_cnnp_dataframe_iter_set_cursor(iter, 0);
	ccv_cnnp_dataframe_iter_prefetch(iter, 1, 0);
	ccv_cnnp_dataframe_iter_prefetch(iter, 1, 0);
	_ccv_iter_accessed = 0;
	for (i = 0; i < 7; i++)
	{
		ccv_cnnp_dataframe_iter_next(iter, &data, 1, 0);
		result[i] = (int)(intptr_t)data;
	}
	REQUIRE_EQ(_ccv_iter_accessed, 5, "5 iterator accessed");
	_ccv_iter_accessed = 0;
	ccv_cnnp_dataframe_iter_next(iter, &data, 1, 0);
	result[7] = (int)(intptr_t)data;
	REQUIRE_EQ(_ccv_iter_accessed, 1, "iterator accessed");
	const int fail1 = ccv_cnnp_dataframe_iter_prefetch(iter, 1, 0);
	REQUIRE_EQ(fail1, -1, "cannot advance no more");
	for (i = 0; i < 8; i++)
		++int_array[i];
	ccv_cnnp_dataframe_iter_free(iter);
	REQUIRE_ARRAY_EQ(int, int_array, result, 8, "iterated result and actual result should be the same");
	ccv_cnnp_dataframe_free(dataframe);
}

#include "case_main.h"