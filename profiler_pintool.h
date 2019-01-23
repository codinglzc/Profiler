#ifndef PROFILER_PINTOOL_H
#define PROFILER_PINTOOL_H

//#ifdef TIMING
#include <time.h>
#include <sys/time.h>
//#endif

#include <map>
#include <vector>
#include <sys/syscall.h>
#include <iomanip>

#include <execinfo.h>
#include <signal.h>

#include "profiler_data_structure.h"
#include "profiler_config.h"
#include "profiler_elf.h"

#include "pin.H"

using namespace std;



/* ===================================================================== */
/* Thread-local Data Structure stored in TLS*/
/* ===================================================================== */

typedef struct ThreadDataType
{
	double start_time; // when the thread starts
	double end_time; // when the thread ends

	// the memory object allocated by the give tracked alloc func
	MetaObj curr_allocobj; 

	// intenal alloc func if required
	vector<MetaObj> open_alloc_list;

	// key: obj id
	map<int, TraceObjThreadAccess> accessed_objects;

}ThreadDataType;

/* function to access thread-specific data*/
ThreadDataType* get_tls(TLS_KEY tls_key, THREADID threadid)
{
    ThreadDataType* tdata = static_cast<ThreadDataType*>(PIN_GetThreadData(tls_key, threadid));
    return tdata;
}



#endif

