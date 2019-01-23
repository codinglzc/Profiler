#ifndef PROFILER_DATA_STRUCTURE_H
#define PROFILER_DATA_STRUCTURE_H

#include <iostream>
#include <vector>
#include <string>
#include "profiler_config.h"

class MetaObj;

#define PANIC(format, ...){\
    printf(format "PANIC: %s %d\n", __VA_ARGS__, __FILE__, __LINE__);\
    exit(13);\
  }


#define ALERT(format, ...){\
    printf(format "PANIC: %s %d\n", __VA_ARGS__, __FILE__, __LINE__);\
  }

/**************************************************/
/*     Signature of allocation/free functions     */
/**************************************************/
class AllocFunc
{
  public:
	std::string  name;
    int    argid_itemsize;
    int    argid_itemnum;
    int    argid_ptridx;
    bool   isInternal;
    AllocFunc(std::string& nm, int item_size, int num_item, int ptr_idx, bool i)
	{
      name           = nm;
      argid_itemsize = item_size;
      argid_itemnum  = num_item;
      argid_ptridx   = ptr_idx;
      isInternal     = i;
    }
};

/*************************************************/
/*    			 Allocation Type  				 */
/*************************************************/

typedef enum
{
	STATIC_T = 0,
	HEAP_T   = 1,
	STACK_T  = 2,
}AllocType;

const char AllocTypeStr[3][8] = {"Static", "Heap", "Stack"};


/*************************************************/
/*     	   Active Memory Object for tracing   	 */
/*************************************************/
class TraceObj
{
  public:
    int     obj_id;
    ADDRINT st_addr;
    ADDRINT end_addr;
    UINT64  dynamic_read;
    UINT64  dynamic_write;
    UINT64  read_in_cache;
    UINT64  strided_read;
    UINT64  pointerchasing_read;
    UINT64  random_read;
    ADDRINT last_accessed_addr;
    ADDRINT last_accessed_addr_cacheline;
    ADDRINT last_accessed_addr_value;
    ADDRINT access_stride;
    time_t  start_time;
    time_t  end_time;
    UINT64 	cnt;
    
    //constructor
    TraceObj(MetaObj &objref);
    
    void backup_trace(MetaObj* MetaObj_ptr, UINT64 ins_cnt, UINT64 memins_cnt);
    
    // void record_read(ADDRINT addr, int granularity=0);
    
    // void record_write(ADDRINT addr);
    
    void summary(std::ostream& output=std::cout);
    
    void print_short(std::ostream& output=std::cout);
};

class TraceObjMeta
{
  public:
    int obj_id;
    ADDRINT st_addr;
    ADDRINT end_addr;

    TraceObjMeta(MetaObj &obj);
    void print_short(std::ostream& output=std::cout);
};

class TraceObjThreadAccess
{
  public:
    ADDRINT st_addr;
	time_t  start_time; // when a thread start accessing this object
	time_t  end_time;	// when a thread access this object last time
    UINT64  dynamic_read;
    UINT64  dynamic_write;
    UINT64  read_in_cache;
    UINT64  strided_read;
    UINT64  pointerchasing_read;
    UINT64  random_read;
    ADDRINT last_accessed_addr;
    ADDRINT last_accessed_addr_cacheline;
    ADDRINT last_accessed_addr_value;
    ADDRINT access_stride;
    
    TraceObjThreadAccess(ADDRINT st_addr_, double st_time, double en_time);
    
    void record_read(ADDRINT addr, int granularity=0);
    
    void record_write(ADDRINT addr, int granularity=0);
};


typedef struct ThreadMemAccess
{
	int		thread_id;
	double	start_time; // when a thread start accessing
	double	end_time;   // when a thread is killed or the object is released

	ADDRINT accessed_addr_low;
	ADDRINT accessed_addr_high;
	UINT64	dynamic_read;
	UINT64	dynamic_write;
	UINT64  read_in_cache;
    UINT64  strided_read;
    UINT64  pointerchasing_read;
    UINT64  random_read;
}ThreadMemAccess;


/**************************************************/
/*     Memory Object allocated during execution   */
/**************************************************/

class MetaObj
{
  public:

  	/* Identifier */
  	int			obj_id;
	ADDRINT		st_addr;
	ADDRINT		end_addr;
	ADDRINT		size;
	int			creator_tid;
	ADDRINT		ip;
	std::string	source_code_info;
	std::string	var_name;
    std::string alloc_func_name;
	AllocType 	type;
	double		start_time;
	double		end_time;
	UINT64		st_ins;
	UINT64		end_ins;
	UINT64		st_memins;
	UINT64		end_memins;

	/* Thread Accesses */
	std::vector<ThreadMemAccess> access_list;

	/*Aggregated Raw Metrics from all threads*/
    UINT64  dynamic_read;
    UINT64  dynamic_write;
    UINT64  read_in_cache;
    UINT64  read_not_in_cache;
    UINT64  strided_read;
    UINT64  pointerchasing_read;
    UINT64  random_read;
    int     num_threads;

	/*Derived metrics from row metrics*/
    UINT64 	mem_ref;
    double 	mem_ref_percentage;
    double 	size_in_mb;
    double 	readwrite_ratio;
    double 	readincache_ratio;
    double 	strided_read_ratio;
    double 	random_read_ratio;
    double 	pointchasing_read_ratio;
    double 	non_memory_ins_per_ref;

	//constructor
    MetaObj();

    void reset();

    void release(UINT64 ins, UINT64 mem_ins, double t)
	{
        end_ins = ins;
        end_memins = mem_ins;
        end_time = t;
    }

    void add_thread_accesses(int tid, TraceObjThreadAccess ta);

    void print_stats(std::ostream& output=std::cout);
    
    void print_meta(std::ostream& output=std::cout);

    // void print_preference(std::ostream& output=std::cout);

    std::string getSourceCodeInfo(){return source_code_info;}

    bool operator<(const MetaObj& rhs) const
    {
    	return (dynamic_read+dynamic_write) > (rhs.dynamic_read + rhs.dynamic_write);
    }
  
};


#endif