#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <omp.h>
#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <numa.h>

using namespace std;

// ========= 在numa.h上再封装一层内存分配函数，主要用于区分DRAM和NVM。 ==========//

// 分配在DRAM上
void *hme_alloc_dram(size_t size)
{
    return numa_alloc_onnode(size, 0);
}

// 分配在NVM上
void *hme_alloc_nvm(size_t size)
{
    return numa_alloc_onnode(size, 1);
}

// 释放
void hme_free(void *ptr)
{
    numa_free(ptr, (size_t) sizeof(ptr));
}

// =========================================================================//

double gettime_in_sec()
{
	double time_sec = 0.0;
	struct timeval time_st;
	if (gettimeofday(&time_st, NULL))
	{
		cerr << "unable to gettime, skip timing. \n";
		time_sec = -1.;
	}
	else
	{
		time_sec = (double)time_st.tv_sec + (double)time_st.tv_usec * .000001;
	}
	return time_sec;
}

#define LEN 1024
double global_data = 1.1;
double global_dataarr[LEN]={1.1};
double global_bss;
double global_bssarr[LEN];
static double static_var0 = 2.2;
static double static_arr0[LEN]={2.2};
static double static_var1;
static double static_arr1[LEN];

int main()
{
	double start_time = gettime_in_sec();

	printf("\nTest main begins!\n");
	
	const size_t length = (size_t)LEN;
	double stack_arr[length];
	double *malloc_arr = (double*)malloc(sizeof(double) * length);	// 在堆上，分配n个字节，并返回void指针类型。
	double *calloc_arr = (double*)calloc(sizeof(double), length);	// 在堆上，分配n*size个字节，并初始化为0，返回void* 类型
	// 重新分配堆上的void指针p所指的空间为n个字节，同时会复制原有内容到新分配的堆上存储空间。
	double *realloc_arr= (double*)realloc(calloc_arr, sizeof(double) * length * 2); // 
	double *valloc_arr = (double*)valloc(sizeof(double) * length);	//
	// HME：分配在DRAM上
	double * numa_alloc_on_dram = (double*) hme_alloc_dram(sizeof(double) * length);
	// HME：分配在NVM上
	double * numa_alloc_on_nvm  = (double*) hme_alloc_nvm(sizeof(double) * length);

	printf("stack_arr %p\n",	stack_arr);
	printf("malloc_arr %p\n",	malloc_arr);
	printf("calloc_arr %p\n",	calloc_arr);
	printf("realloc_arr %p\n",	realloc_arr);
	printf("valloc_arr %p\n",	valloc_arr);
	printf("numa_alloc_on_dram %p\n",	numa_alloc_on_dram);
	printf("numa_alloc_on_nvm %p\n",	numa_alloc_on_nvm);
  
	//initialize
	size_t i;
// #pragma omp parallel for
	for(i = 0; i < length; i++)
	{
		global_bssarr[i] 		= 	1.1;
		static_arr1[i]			= 	2.2;
		stack_arr[i]  			= 	3.3;
		malloc_arr[i] 			= 	4.4;
		calloc_arr[i] 			= 	5.5;
		realloc_arr[i]			= 	6.6;
		valloc_arr[i] 			= 	7.7;
		numa_alloc_on_dram[i] 	= 	8.8;
		numa_alloc_on_nvm[i] 	= 	9.9;
	}

	double sum = 0.0;
// #pragma omp parallel for
	for(i = 0; i < length; i++)
	{
		sum += global_bssarr[i] + static_arr1[i] + stack_arr[i] + malloc_arr[i] + calloc_arr[i] +
			realloc_arr[i] + valloc_arr[i] + numa_alloc_on_dram[i] + numa_alloc_on_nvm[i];
		sleep(1);
	}

	printf("Sum = %f\n", sum);

  
	free(malloc_arr);
	free(realloc_arr);
	free(valloc_arr);
	hme_free(numa_alloc_on_dram);
	hme_free(numa_alloc_on_nvm);

	double end_time = gettime_in_sec();
	cout << "Test program spends : " << end_time - start_time << " s" << endl;
	printf("Test main ends!\n\n");

	return 0;
}
