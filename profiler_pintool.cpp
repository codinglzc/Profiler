#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#include <os-apis/threads.h>
#include <os-apis/os_return_codes.h>
#include "profiler_pintool.h"

using namespace std;

/* ============================================================ */
// Command line
/* ============================================================ */
// get output file name
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
							"o", "", "specify output file name");

// get user-specified allocation rtn
KNOB<string> KnobAllocFuncFile(KNOB_MODE_WRITEONCE, "pintool",
							   "a", "",
							   "specify allocation routines to track");

//get user-specified free rtn
KNOB<string> KnobFreeFuncFile(KNOB_MODE_WRITEONCE, "pintool",
							  "f", "",
							  "specify free routines to track");

/* =========== */
/* Usage */
/* =========== */

INT32 Usage()
{
	cerr << "A Pintool collects array-centric metrics." << endl;
	cerr << endl
		 << KNOB_BASE::StringKnobSummary() << endl;
	return -1;
}

/* ============================================================ */
// Global Variables
/* ============================================================ */
vector<AllocFunc> list_alloc_func;
vector<AllocFunc> list_free_func;

vector<MetaObj> masterlist;
vector<TraceObjMeta> activelist;
ofstream memtrace_file;

UINT64 global_ins_cnt;
UINT64 dynamic_read_ins, dynamic_write_ins;
UINT64 static_read_ins, static_write_ins;
bool is_record_img, is_record_rtn;

ADDRINT img_addr_low, img_addr_high;
map<string, string> var_map; // 内存对象(key: 文件名+变量名；val: 变量名)

double time_start;
UINT64 send_times;
NATIVE_TID native_tid; // socket 线程 id
int client_sock;
int exp_id;

/* ===================================================================== */
// Pre function
/* ===================================================================== */
string objs_to_json(bool isFirst=false, bool isLast=false);
VOID Client_task(VOID *args);

/* ===================================================================== */
// Thread related
/* ===================================================================== */
// key for accessing TLS storage in the threads
static TLS_KEY tls_key;
PIN_LOCK lock;
vector<THREADID> tid_list;

/* ===================================================================== */
// Utility Functions
/* ===================================================================== */
/*
    std::string  name;
    int    argid_itemsize;
    int    argid_itemnum;
    int    argid_ptridx;
*/
VOID setup_allocfunclist(vector<AllocFunc> &allocfunclist, string &filename)
{
	//if the user provides wraper functions
	if (filename.length() > 0)
	{
		//setup the list of selected routines
		std::ifstream file(filename.c_str());
		if (!file.is_open())
		{
			PANIC("cannot open %s.", filename.c_str());
		}

		std::string line;
		while (getline(file, line))
		{
			stringstream linestream(line);
			string fn;
			int id0, id1, id2;
			linestream >> fn >> id0 >> id1 >> id2;

			if (fn.length() > 0)
			{
				AllocFunc uf(fn, id0, id1, id2, false);
				allocfunclist.push_back(uf);
			}
		}
	}
}

VOID setup_allocfunclist(vector<AllocFunc> &allocfunclist)
{
	//add all default functions

	//ptr = malloc(size_t)
	string a("malloc");
	AllocFunc aa(a, -1, 0, -1, false);
	allocfunclist.push_back(aa);

	//ptr = calloc(size_t num, size_t size);
	string b("calloc");
	AllocFunc bb(b, 0, 1, -1, false);
	allocfunclist.push_back(bb);

	//void* realloc (void* ptr, size_t size);
	string c("realloc");
	AllocFunc cc(c, -1, 1, -1, false);
	allocfunclist.push_back(cc);

	//void *valloc(size_t size);
	string d("valloc");
	AllocFunc dd(d, -1, 0, -1, false);
	allocfunclist.push_back(dd);

	// //void *pvalloc(size_t size);
	// string e("pvalloc");
	// AllocFunc ee(e, -1, 0, -1, false);
	// allocfunclist.push_back(ee);

	// //void *memalign(size_t alignment, size_t size);
	// string f("memalign");
	// AllocFunc ff(f, -1, 1, -1, false);
	// allocfunclist.push_back(ff);

	// //int posix_memalign(void **memptr, size_t alignment, size_t size);
	// string g("posix_memalign");
	// AllocFunc gg(g, -1, 2, 0, false);
	// allocfunclist.push_back(gg);

	//ptr = hme_alloc_dram(size_t size);
	string h("hme_alloc_dram");
	AllocFunc hh(h, -1, 0, -1, false);
	allocfunclist.push_back(hh);

	//ptr = hme_alloc_nvm(size_t size);
	string i("hme_alloc_nvm");
	AllocFunc ii(i, -1, 0, -1, false);
	allocfunclist.push_back(ii);
}

/*
    std::string  name;
    int    argid_itemsize;
    int    argid_itemnum;
    int    argid_ptridx;
*/
VOID setup_freefunclist(vector<AllocFunc> &freefunclist, string &filename)
{
	if (filename.length() > 0)
	{
		//setup the list of selected routines
		std::ifstream file(filename.c_str());
		if (!file.is_open())
		{
			PANIC("cannot open %s", filename.c_str());
		}

		std::string line;
		while (getline(file, line))
		{
			stringstream linestream(line);
			string fn;
			int id0, id1, id2;
			linestream >> fn >> id0 >> id1 >> id2;

			if (fn.length() > 0)
			{
				AllocFunc uf(fn, id0, id1, id2, false);
				freefunclist.push_back(uf);
			}
		}
	}
}

double gettime_in_sec()
{
	double time_sec = 0.0;
	struct timeval time_st;
	if (gettimeofday(&time_st, NULL))
	{
		ALERT("unable to gettime, skip timing.%s", "");
		time_sec = -1.;
	}
	else
	{
		time_sec = (double)time_st.tv_sec + (double)time_st.tv_usec * .000001;
	}
	return time_sec;
}

// 打印当前线程的调用堆栈
void handler(int sig)
{
	PIN_LockClient();

	void *array[10];
	size_t size;
	char **strings;
	size_t i;

	// get void*'s for all entries on the stack
	size = backtrace(array, 10);
	strings = backtrace_symbols(array, size);

	printf("Obtained %zd stack frames.\n", size);
	for (i = 0; i < size; i++)
		printf("%s\n", strings[i]);

	PIN_UnlockClient();

	exit(1);
}

/* ===================================================================== */
// Analysis Functions
/* ===================================================================== */

/*
 * rtnptr  : Address in memory of rtn
 * eturnIp : Address for function call
 */
VOID start_trace_main(VOID *rtnptr, ADDRINT returnIp)
{
#ifdef VERBOSE
	cout << "start tracing main" << endl;
#endif
	//switch on tracing only after entering the main rtn
	is_record_img = true;

	/*
    if(KnobTrackCallPath) 
	{
		track_callpath(rtnptr, FUNC_OPEN, returnIp);
		open_func_List.push_back((ADDRINT)rtnptr);
    }
    */
}

VOID end_trace_main(VOID *rtnptr, ADDRINT returnIp)
{
#ifdef VERBOSE
	cout << "end tracing main" << endl;
#endif

	/*
    if(KnobTrackCallPath) 
	{
		track_callpath(rtnptr, FUNC_CLOSE, returnIp);
    }
    */

	//switch off tracing
	is_record_img = false;
}

// 在alloc内存对象之前，获取关于即将生成的内存对象的部分信息
VOID AllocBefore(ADDRINT itemsize, ADDRINT num_item, ADDRINT returnip,
				 THREADID tid, UINT32 fid)
{
	if (!is_record_img)
		return;

	if (returnip < img_addr_low || returnip > img_addr_high)
		return;

	size_t size = itemsize * num_item;
	if (size < Threshold_Size)
		return;

	ThreadDataType *t_data = get_tls(tls_key, tid);

	//Always overwrite if External
	if (!list_alloc_func[fid].isInternal)
	{
		(t_data->curr_allocobj).reset();
		(t_data->curr_allocobj).ip = returnip;
		(t_data->curr_allocobj).size = size;
		(t_data->curr_allocobj).creator_tid = tid;
	}
	else
	{
		//return if no External Alloc being tracked
		if ((t_data->curr_allocobj).creator_tid == -1)
			return;

		if ((t_data->open_alloc_list).size() == 0 || (t_data->open_alloc_list).back().ip != returnip)
		{
			MetaObj anew;
			(t_data->open_alloc_list).push_back(anew);

#ifdef VERBOSE
			cout << "Enter AllocBefore: " << list_alloc_func[fid].name
				 << ", returnip " << returnip << " Internal" << endl;
#endif
		}

		//update to the threads private record
		(t_data->open_alloc_list).back().ip = returnip;
		(t_data->open_alloc_list).back().size = size;
	}
}

// 在alloc内存对象之后，获取关于当前生成的内存对象的详细(全部)信息
VOID AllocAfter(ADDRINT ret, ADDRINT returnip, THREADID tid, UINT32 fid)
{
	if (!is_record_img)
		return;

	if (returnip < img_addr_low || returnip > img_addr_high)
		return;

	ThreadDataType *t_data = get_tls(tls_key, tid);

	MetaObj &curr_memobj = t_data->curr_allocobj;

	//return if no External Alloc being tracked
	if (curr_memobj.creator_tid < 0)
		return;

	//return if an External Alloc cannot find matched open
	bool isExternal = (!list_alloc_func[fid].isInternal);
	if (isExternal && curr_memobj.ip != returnip)
		return;

	//avoid adding object with duplicate address space
	vector<TraceObjMeta>::iterator it;
	for (it = activelist.begin(); it != activelist.end(); ++it)
	{
		if (it->st_addr == ret)
		{
#ifdef DEBUG
			ALERT("The address %lx has already been allocated to %s %s", ret, masterlist[it->obj_id].source_code_info.c_str(), masterlist[it->obj_id].var_name.c_str());
#endif
			//It could be realloc : make it free first and then add
			masterlist[it->obj_id].release(global_ins_cnt, (dynamic_read_ins + dynamic_write_ins), gettime_in_sec() - time_start);
			activelist.erase(it);
			break;
		}
	}

	/**
     * Start: get source code info
     */
	string var_str;
	string curr_filename;
	INT32 curr_line;
	INT32 curr_column;
	PIN_LockClient();
	PIN_GetSourceLocation(returnip, &curr_column, &curr_line, &curr_filename);
	PIN_UnlockClient();

	stringstream ss;
	size_t found = curr_filename.find_last_of("/");
	if (found == string::npos)
	{
#ifdef DEBUG
		cerr << " Source information unavailable. Skip this object: " << list_alloc_func[fid].name << endl;
#endif
		return;
	}
	else
	{
		ss << curr_filename << ":" << list_alloc_func[fid].name << ":" << curr_line;
	}

	//search for variable name if isExternal
	if (isExternal)
	{
		//check if this var_name has been captured before
		string key = ss.str();
		map<string, string>::iterator vit = var_map.find(key);
		if (vit == var_map.end())
		{
			std::ifstream file(curr_filename.c_str());
			if (!file.is_open())
			{
				if (curr_filename.length() > 0)
					cerr << "Cannot open the source code file " << curr_filename
						 << ", " << list_alloc_func[fid].name << endl;
			}
			else
			{
				file.seekg(ios::beg);
				string ll;
				int lnum = 0;
				while (lnum++ < curr_line)
					getline(file, ll);
				size_t head = ll.find_first_not_of(" \t\n\v\f\r");
				size_t tail = ll.find_last_not_of(" \t\n\v\f\r");

				if (list_alloc_func[fid].argid_ptridx == -1)
				{
					tail = ll.find_first_of("=");
				}
				ll = ll.substr(head, tail - head);
				var_map.insert(make_pair(key, ll));
				var_str = ll;
#ifdef VERBOSE
				cout << "Insert to var_map " << key << ":" << var_str << endl;
#endif
			}
		}
		else
		{
			var_str = vit->second;
		}
	}
	/**
     * End: get source code info
     */

	/**
     * Start: get source code info
     */
	if (isExternal)
	{
		curr_memobj.var_name = var_str;
		curr_memobj.source_code_info = ss.str();
		curr_memobj.alloc_func_name = list_alloc_func[fid].name;
		curr_memobj.type = AllocType::HEAP_T;
		curr_memobj.obj_id = masterlist.size();
		curr_memobj.st_addr = ret;
		curr_memobj.end_addr = curr_memobj.st_addr + curr_memobj.size - 1;
		curr_memobj.creator_tid = tid;
		curr_memobj.start_time = gettime_in_sec() - time_start;
		curr_memobj.st_ins = global_ins_cnt;
		curr_memobj.st_memins = dynamic_read_ins + dynamic_write_ins;

		//add additional info from internal routines
		size_t len = (t_data->open_alloc_list).size();
		for (size_t j = 0; j < len; j++)
		{
			if ((t_data->open_alloc_list[j]).source_code_info.length() > 0)
			{
				if ((t_data->open_alloc_list[j]).st_addr != ret)
				{
					cerr << (t_data->open_alloc_list[j]).source_code_info << ": "
						 << (t_data->open_alloc_list[j]).st_addr << " != " << ret << endl;
				}
				else
				{
					curr_memobj.source_code_info.append(" -> ");
					curr_memobj.source_code_info.append((t_data->open_alloc_list[j]).source_code_info);
					if ((t_data->open_alloc_list[j]).size > curr_memobj.size)
					{
						curr_memobj.size = (t_data->open_alloc_list[j]).size;
						curr_memobj.end_addr = curr_memobj.st_addr + curr_memobj.size - 1;
					}
				}
			}
		}

		//Update Shared Data structures
		PIN_GetLock(&lock, tid + 1);
		masterlist.push_back(curr_memobj);

		//add to active list
		MetaObj &a = masterlist.back();
		TraceObjMeta b(a);
		activelist.push_back(b);
		PIN_ReleaseLock(&lock);

		//reset tracking
		curr_memobj.reset();
		(t_data->open_alloc_list).clear();
	}
	else
	{
		size_t len = (t_data->open_alloc_list).size();
		for (int j = len - 1; j >= 0; j--)
		{
			if ((t_data->open_alloc_list[j]).ip == returnip)
			{
				(t_data->open_alloc_list[j]).source_code_info = ss.str();
				(t_data->open_alloc_list[j]).st_addr = ret;
				break;
			}
		}
	}

	/*
    if(KnobTrackCallPath && RTNSTACK_LVL > 1)
	{
        OpenAllocObj obj;
        obj.obj_id     = curr_memobj.obj_id;
        obj.timestamp = open_func_List.size();
        obj.rtn_level = 0;
        open_allocobj_list.push_back(obj);
    }
    */
}

VOID FreeBefore(ADDRINT addr, ADDRINT returnIp, THREADID tid)
{
	if (!is_record_img)
		return;

	PIN_GetLock(&lock, tid + 1);
	vector<TraceObjMeta>::iterator it;
	for (it = activelist.begin(); it != activelist.end(); ++it)
	{
		if (it->st_addr == addr)
		{
			masterlist[it->obj_id].release(global_ins_cnt, (dynamic_read_ins + dynamic_write_ins), gettime_in_sec() - time_start);
			activelist.erase(it);
			break;
		}
	}
	PIN_ReleaseLock(&lock);
}

//this is used for global timestamp
VOID countBBLIns(UINT64 c)
{
	global_ins_cnt += c;
}

VOID TraceMem(ADDRINT addr, INT32 size, BOOL isRead, THREADID tid)
{
	if (!is_record_img || !is_record_rtn)
		return;

	if (isRead)
		dynamic_read_ins++;
	else
		dynamic_write_ins++;

	/* currently, stack read/write is filtered
    if(stack_bound_low < addr && addr < stack_bound_high){
        if(isRead)
            dynamic_stackread_ins ++;
        else
            dynamic_stackwrite_ins ++;
        return;
    }*/

	//for profiler, capture address and object name
	int id = -1;
	for (vector<TraceObjMeta>::iterator it = activelist.begin(); it != activelist.end(); ++it)
	{
		if (it->st_addr <= addr && addr <= it->end_addr)
		{
			id = it->obj_id;
#ifdef DEBUG
			memtrace_file << hex << addr
						  << "	" << (isRead ? "READ" : "WRITE")
						  << "	" << masterlist[id].var_name.c_str()
						  << "	" << tid << endl;
#endif
			break;
		}
	}

	//access an object that is not tracked
	if (id < 0)
	{
		/*
		memtrace_file << "(" << hex << addr
			<< ", " << (isRead ?"READ" : "WRITE")
			<< ", " << tid << ")"
			<< " is accessed an object that is not tracked!" << endl;
		*/
		return;
	}

	//check and update thread-local accessed objects
	ThreadDataType *t_data = get_tls(tls_key, tid);
	map<int, TraceObjThreadAccess>::iterator find = t_data->accessed_objects.find(id);
	if (find != (t_data->accessed_objects).end())
	{
		(find->second).end_time = gettime_in_sec() - time_start;
		if (isRead)
			(find->second).record_read(addr);
		else
			(find->second).record_write(addr);
	}
	else
	{
		double t = gettime_in_sec() - time_start;
		TraceObjThreadAccess newaccess(addr, t, t);
		if (isRead)
			newaccess.record_read(addr);
		else
			newaccess.record_write(addr);
		(t_data->accessed_objects).insert(make_pair<int, TraceObjThreadAccess>(id, newaccess));
	}
}

/* ===================================================================== */
// Instrumentation Functions
/* ===================================================================== */

//instrument allocation functions
//mark the tracked functions
VOID Image(IMG img, VOID *v)
{

#ifdef VERBOSE
	cout << "Current loaded Image Name: " << IMG_Name(img) << ", isMainImage=" << IMG_IsMainExecutable(img) << endl;
#endif

	if (IMG_IsMainExecutable(img))
	{
		img_addr_low = IMG_LowAddress(img);   // The image's lowest address or the text segment low address if the image is split.
		img_addr_high = IMG_HighAddress(img); // The image's highest address or the text segment high address if the image is split.

		//Only enable the tracing in the main rtn
		RTN mainRtn = RTN_FindByName(img, "main");
		if (RTN_Valid(mainRtn))
		{
			RTN_Open(mainRtn);
			RTN_InsertCall(mainRtn, IPOINT_BEFORE, (AFUNPTR)start_trace_main,
						   IARG_PTR, RTN_Address(mainRtn), // Address in memory of rtn
						   IARG_RETURN_IP, IARG_END);
			RTN_InsertCall(mainRtn, IPOINT_AFTER, (AFUNPTR)end_trace_main,
						   IARG_PTR, RTN_Address(mainRtn),
						   IARG_RETURN_IP, IARG_END);
			RTN_Close(mainRtn);
		}
	}

	//Iterate all allocation routines
	if (list_alloc_func.size() > 0)
	{
#ifdef VERBOSE
		for (vector<AllocFunc>::iterator it = list_alloc_func.begin(); it < list_alloc_func.end(); it++)
			printf("Alloc function: %s %s\n", (it->name).c_str(),
				   (it->isInternal ? "Internal" : "External"));
#endif
		// the index of allocation routine
		UINT32 fid = 0;
		for (vector<AllocFunc>::iterator it = list_alloc_func.begin();
			 it != list_alloc_func.end(); ++it)
		{
			AllocFunc &func = *it;
			//Option A: iterate all allocation routines of interest
			RTN allocRtn = RTN_FindByName(img, it->name.c_str());

			// 如果得到的 allocRtn 是非法的，那么通过遍历符号表来得到合法的 allocRtn
			if (!RTN_Valid(allocRtn))
			{
				//Option B: iterate all symbols undecorated name
				//Walk through the symbols in the symbol table.
				//compare with the routines of interest
				for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
				{
					string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);
					if (undFuncName.find("<") != string::npos)
						undFuncName = undFuncName.substr(0, undFuncName.find("<"));

					if (undFuncName.compare(it->name) == 0)
					{
						//RTN allocRtn1 = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
						allocRtn = RTN_FindByName(img, SYM_Name(sym).c_str());
						break;
					}
				}
			}

			// 如果得到的 allocRtn 是合法的
			if (RTN_Valid(allocRtn))
			{
#ifdef VERBOSE
				cout << "Located allocation routine: " << it->name
					 << "(" << func.argid_itemsize
					 << "," << func.argid_itemnum
					 << "," << func.argid_ptridx << ")" << endl;
#endif

				RTN_Open(allocRtn);

				//check the signature of the allocation function
				if (func.argid_itemsize < 0)
					RTN_InsertCall(allocRtn, IPOINT_BEFORE, (AFUNPTR)AllocBefore,
								   IARG_ADDRINT, 1,
								   IARG_FUNCARG_ENTRYPOINT_VALUE, func.argid_itemnum,
								   IARG_RETURN_IP, IARG_THREAD_ID,
								   IARG_UINT32, fid, 
								   IARG_END);
				else
					RTN_InsertCall(allocRtn, IPOINT_BEFORE, (AFUNPTR)AllocBefore,
								   IARG_FUNCARG_ENTRYPOINT_VALUE, func.argid_itemsize,
								   IARG_FUNCARG_ENTRYPOINT_VALUE, func.argid_itemnum,
								   IARG_RETURN_IP, IARG_THREAD_ID,
								   IARG_UINT32, fid,
								   IARG_END);

				if (func.argid_ptridx < 0)
					RTN_InsertCall(allocRtn, IPOINT_AFTER, (AFUNPTR)AllocAfter,
								   IARG_FUNCRET_EXITPOINT_VALUE,
								   IARG_RETURN_IP, IARG_THREAD_ID,
								   IARG_UINT32, fid, 
								   IARG_END);

				else
					RTN_InsertCall(allocRtn, IPOINT_AFTER, (AFUNPTR)AllocAfter,
								   IARG_FUNCARG_ENTRYPOINT_VALUE, func.argid_ptridx,
								   IARG_RETURN_IP, IARG_THREAD_ID,
								   IARG_UINT32, fid, 
								   IARG_END);

				RTN_Close(allocRtn);
			}

			fid++;
		}
	} //End of all allocation routines

	//Iterate all free routines
	if (list_free_func.size() > 0)
	{

#ifdef VERBOSE
		vector<AllocFunc>::iterator it0;
		for (it0 = list_free_func.begin(); it0 < list_free_func.end(); it0++)
			printf("Free function: %s %s\n", (it0->name).c_str(),
				   (it0->isInternal ? "Internal" : "External"));
#endif

		vector<AllocFunc>::iterator it;
		for (it = list_free_func.begin(); it != list_free_func.end(); ++it)
		{
			RTN freeRtn = RTN_FindByName(img, it->name.c_str());

			// 如果得到的 freeRtn 是非法的，那么通过遍历符号表来得到合法的 freeRtn
			if (!RTN_Valid(freeRtn))
			{
				//Option B: iterate all symbols undecorated name
				//Walk through the symbols in the symbol table.
				//compare with the routines of interest
				for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
				{
					string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);
					if (undFuncName.find("<") != string::npos)
						undFuncName = undFuncName.substr(0, undFuncName.find("<"));

					if (undFuncName.compare(it->name) == 0)
					{
						//RTN freeRtn = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
						freeRtn = RTN_FindByName(img, SYM_Name(sym).c_str());
						break;
					}
				}
			}

			if (RTN_Valid(freeRtn))
			{
#ifdef VERBOSE
				cout << "Located free routine: " << it->name << endl;
#endif
				RTN_Open(freeRtn);
				RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)FreeBefore,
							   IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
							   IARG_RETURN_IP, IARG_THREAD_ID, IARG_END);
				RTN_Close(freeRtn);
			}
		}
	}

	/*
    if(list_track_func.size() > 0)
	{  
		//Iterate all tracking routines of interest
		vector<string>::iterator it;
      	for(it = list_track_func.begin(); it != list_track_func.end(); ++it)
		{
			RTN track_rtn = RTN_FindByName(img, it->c_str());
        
	  		if(!RTN_Valid(track_rtn))
			{
			    //Option B: iterate all symbols undecorated name 
			    //Walk through the symbols in the symbol table.
			    //compare with the routines of interest
			    for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
			    {
					string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);
					if(undFuncName.find("<")!=string::npos)
				  		undFuncName = undFuncName.substr(0,undFuncName.find("<"));
				
					if(undFuncName.find(*it) == 0){
				  		track_rtn  = RTN_FindByName(img,SYM_Name(sym).c_str());
				  		break;
					}
	      		}
	  		}

	  		if(RTN_Valid(track_rtn))
			{
#ifdef VERBOSE
            	cout << "Located tracked routine: " << *it <<endl;
#endif            
			    RTN_Open(track_rtn);
			    RTN_InsertCall(track_rtn, IPOINT_BEFORE, (AFUNPTR)start_track_rtn,
					   			IARG_PTR, new string(*it), 
					   			IARG_UINT32, RTN_Id(track_rtn), IARG_END);
			    RTN_InsertCall(track_rtn, IPOINT_AFTER,  (AFUNPTR)end_track_rtn,
					   			IARG_PTR, new string(*it),  
					   			IARG_UINT32, RTN_Id(track_rtn), IARG_BOOL,false,IARG_END);
			    RTN_Close(track_rtn);
			}
      	}
    }else if(list_hooks.size() > 0)
    {
		if(list_hooks.size()!=2)
		{
			PANIC("Only a pair of hooks can be setup, received %lu hooks", list_hooks.size());
	    }
	    RTN hookopen_rtn = RTN_FindByName(img, list_hooks[0].c_str());
	    if(RTN_Valid(hookopen_rtn))
		{
#ifdef VERBOSE
			cout << "Located hook: " << list_hooks[0] <<endl;
#endif
			RTN_Open(hookopen_rtn);
			RTN_InsertCall(hookopen_rtn, IPOINT_BEFORE, (AFUNPTR)start_track_rtn,
				       		IARG_PTR, new string( list_hooks[0]),
				       		IARG_UINT32,0, IARG_END);
			RTN_Close(hookopen_rtn);
	    }

	   	RTN hookclose_rtn = RTN_FindByName(img, list_hooks[1].c_str());
	    if(RTN_Valid(hookclose_rtn))
		{
#ifdef VERBOSE
			cout << "Located hook: " << list_hooks[1] <<endl;
#endif
			RTN_Open(hookclose_rtn);
			RTN_InsertCall(hookclose_rtn, IPOINT_BEFORE, (AFUNPTR)end_track_rtn,
					       	IARG_PTR, new string( list_hooks[1]),
			       			IARG_UINT32, RTN_Id(hookclose_rtn), IARG_BOOL,true,IARG_END);
			RTN_Close(hookclose_rtn);
	   	}


    }else{
        is_record_rtn = true;
    }
*/

	is_record_rtn = true;
}

//instrument all read and write accesses
//accesses to the stack are excluded
//prefetch accesses are included
VOID Trace(TRACE trace, VOID *val)
{
	RTN rtn = TRACE_Rtn(trace); // RTN that contains first instruction of trace
	if (!RTN_Valid(rtn))
		return;

	IMG img = SEC_Img(RTN_Sec(rtn));
	if (!IMG_Valid(img))
		return; //to avoid image stale error

	// 遍历trace里面的每一个BBL块
	for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
	{
		//coaser granulairy than instrumenting instructions
		BBL_InsertCall(bbl, IPOINT_BEFORE, (AFUNPTR)countBBLIns, IARG_UINT64, BBL_NumIns(bbl), IARG_END);

		// 遍历BBL块里面的每一条指令
		for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins))
		{
			//Instrument memops
			if (!INS_IsStackRead(ins) && !INS_IsStackWrite(ins))
				if (INS_IsStandardMemop(ins) || INS_HasMemoryVector(ins))
				{
					if (INS_IsMemoryRead(ins))
					{
						static_read_ins++;
						INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceMem,
												 IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
												 IARG_BOOL, true, IARG_THREAD_ID, IARG_END);
					}

					if (INS_HasMemoryRead2(ins))
					{
						static_read_ins++;
						INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceMem,
												 IARG_MEMORYREAD2_EA, IARG_MEMORYREAD_SIZE,
												 IARG_BOOL, true, IARG_THREAD_ID, IARG_END);
					}

					if (INS_IsMemoryWrite(ins))
					{
						static_write_ins++;
						INS_InsertPredicatedCall(ins, IPOINT_BEFORE, (AFUNPTR)TraceMem,
												 IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,
												 IARG_BOOL, false, IARG_THREAD_ID, IARG_END);
					}
				}
		}
	}
}

//init a thread-private data structure when a thread starts
VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
	ThreadDataType *tdata = new ThreadDataType();
	tdata->start_time = gettime_in_sec() - time_start;
	PIN_SetThreadData(tls_key, tdata, tid);

	PIN_GetLock(&lock, tid + 1);
	tid_list.push_back(tid);
	PIN_ReleaseLock(&lock);

#ifdef VERBOSE
	cout << "Thread " << tid << " [" << syscall(SYS_gettid) << "] is starting\n";
#endif
}

//push back a thread's accesses to the master list when it ends
VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
#ifdef VERBOSE
	cout << "Threadid " << tid << " is killed " << endl
		 << flush;
#endif

	ThreadDataType *t_data = get_tls(tls_key, tid);
	t_data->end_time = gettime_in_sec() - time_start;

	PIN_GetLock(&lock, tid + 1);
	for (map<int, TraceObjThreadAccess>::iterator it = (t_data->accessed_objects).begin(); it != (t_data->accessed_objects).end(); ++it)
	{

#ifdef VERBOSE
		cout << "Threadid " << tid << " has accessed Obj Id " << it->first << endl
			 << flush;
#endif

		masterlist[it->first].add_thread_accesses(tid, it->second);
	}
	// delete the tid in tid_list
	for (vector<THREADID>::iterator it = tid_list.begin(); it < tid_list.end(); ++it)
	{
		if (*it == tid)
		{
			tid_list.erase(it);
			break;
		}
	}
	PIN_ReleaseLock(&lock);

	//delete thread data
	PIN_SetThreadData(tls_key, 0, tid);
}

VOID Fini(INT32 code, VOID *v)
{
	// save time stamps
	double time_end = gettime_in_sec();
	UINT64 ins = global_ins_cnt;
	UINT64 mem_ins = dynamic_read_ins + dynamic_write_ins;

	// #ifdef TIMING
	cout << "Profiling time: " << (time_end - time_start) << " seconds." << endl;
	// #endif

	//By right, there should be no objects in the active list
	//if there are any objects, this causes memory leakage
	for (vector<TraceObjMeta>::iterator it = activelist.begin(); it != activelist.end(); ++it)
	{
		// #ifdef VERBOSE
		if (masterlist[it->obj_id].source_code_info.compare("Main Image") != 0)
			cout << "Obj Id " << it->obj_id << " created at "
				 << masterlist[it->obj_id].source_code_info << ", not freed!\n";
		// #endif
		masterlist[it->obj_id].release(ins, mem_ins, time_end - time_start);
	}

	// 使输出不采用科学计数法
	memtrace_file.setf(ios::fixed, ios::floatfield);
	memtrace_file << setw(6) << "obj_id"
				  << " "
				  << setw(20) << "st_addr"
				  << " "
				  << setw(20) << "end_addr"
				  << " "
				  << setw(8) << "size"
				  << " "
				  << setw(12) << "creator_tid"
				  << " "
				  << setw(8) << "ip"
				  << " "
				  << setw(50) << "source_code_info"
				  << " "
				  << setw(30) << "var_name"
				  << " "
				  << setw(8) << "type"
				  << " "
				  << setw(25) << "start_time"
				  << " "
				  << setw(25) << "end_time"
				  << " "
				  << setw(12) << "st_ins"
				  << " "
				  << setw(12) << "end_ins"
				  << " "
				  << setw(12) << "st_memins"
				  << " "
				  << setw(12) << "end_memins"
				  << " "
				  << setw(25) << "the size of access_list" << endl;

	for (vector<MetaObj>::iterator it = masterlist.begin(); it != masterlist.end(); ++it)
	{
		memtrace_file << setw(6) << dec << it->obj_id << " "
					  << setw(20) << hex << it->st_addr << " "
					  << setw(20) << hex << it->end_addr << " "
					  << setw(8) << dec << it->size << " "
					  << setw(12) << it->creator_tid << " "
					  << setw(8) << it->ip << " "
					  << setw(50) << it->source_code_info << " "
					  << setw(30) << it->var_name << " "
					  << setw(8) << AllocTypeStr[it->type] << " "
					  << setw(25) << it->start_time << " "
					  << setw(25) << it->end_time << " "
					  << setw(12) << it->st_ins << " "
					  << setw(12) << it->end_ins << " "
					  << setw(12) << it->st_memins << " "
					  << setw(12) << it->end_memins << " "
					  << setw(25) << it->access_list.size() << endl;

		if (it->access_list.size() > 0)
		{
			memtrace_file << "		"
						  << setw(20) << "thread_id"
						  << setw(20) << "start_time"
						  << setw(20) << "end_time"
						  << setw(20) << "dynamic_read"
						  << setw(20) << "dynamic_write"
						  << endl;
			vector<ThreadMemAccess> tmplist = it->access_list;
			vector<ThreadMemAccess>::iterator it_tmp;
			for (it_tmp = tmplist.begin(); it_tmp != tmplist.end(); ++it_tmp)
			{
				memtrace_file << "		"
							  << setw(20) << dec << it_tmp->thread_id
							  << setw(20) << it_tmp->start_time
							  << setw(20) << it_tmp->end_time
							  << setw(20) << it_tmp->dynamic_read
							  << setw(20) << it_tmp->dynamic_write
							  << endl;
			}
		}
	}

	memtrace_file.close();

#ifdef VERBOSE
	cout << "Done!\n";
#endif

	// Transfer the last data before the program ends.
	string message;
	char buf[256];
	message = objs_to_json(false, true);
	message += "\n";
	// cout << "======last send message :======" << endl << message << endl;
	write(client_sock, message.c_str(), message.length() + 1);
	read(client_sock, buf, sizeof(buf));
	// cout << "======read  message :======" << endl << buf << endl;
	close(client_sock);
	// Deregister a thread from the threads database and
	// release all the resources used to track this thread (including TLS).
	OS_DeregisterThread(native_tid);
}

VOID DetachFunc(VOID *v)
{
	std::cerr << "Detached Now " << endl;
}

/* ===================================================================== */
// Objects to JSON
/* ===================================================================== */
string objs_to_json(bool isFirst, bool isLast)
{
	ostringstream oss;
	oss.setf(ios::fixed, ios::floatfield);
	oss << "{";
	// Global variables to JSON
	oss << "\"global_vars\":{";
	oss << "\"exp_id\":" << dec << exp_id << ",";
	oss << "\"send_times\":" << dec << ++send_times << ",";
	oss << "\"time\":" << dec << gettime_in_sec() - time_start << ",";
	oss << "\"global_ins_cnt\":" << dec << global_ins_cnt << ",";
	oss << "\"dynamic_read_ins\":" << dec << dynamic_read_ins << ",";
	oss << "\"dynamic_write_ins\":" << dec << dynamic_write_ins << ",";
	oss << "\"static_read_ins\":" << dec << static_read_ins << ",";
	oss << "\"static_write_ins\":" << dec << static_write_ins << ",";
	oss << "\"isFirst\":" << isFirst << ",";
	oss << "\"isLast\":" << isLast;
	oss << "},";
	// masterlist to JSON
	oss << "\"masterlist\":[";
	const char *separator = "";
	for (vector<MetaObj>::iterator it = masterlist.begin(); it != masterlist.end(); ++it)
	{
		oss << separator << "{";
		oss << "\"objId\":" << dec << it->obj_id << ",";
		oss << "\"startAddress\":\"" << hex << it->st_addr << "\",";
		oss << "\"endAddress\":\"" << hex << it->end_addr << "\",";
		oss << "\"size\":" << dec << it->size << ",";
		oss << "\"createdThreadId\":" << it->creator_tid << ",";
		oss << "\"ip\":\"" << it->ip << "\",";
		oss << "\"sourceCodeInfo\":\"" << it->source_code_info << "\",";
		oss << "\"varName\":\"" << it->var_name << "\",";
		oss << "\"allocFuncName\":\"" << it->alloc_func_name << "\",";
		oss << "\"allocType\":\"" << AllocTypeStr[it->type] << "\",";
		oss << "\"startTime\":" << it->start_time << ",";
		oss << "\"endTime\":" << it->end_time << ",";
		oss << "\"startInstruction\":" << it->st_ins << ",";
		oss << "\"endInstruction\":" << it->end_ins << ",";
		oss << "\"startMemoryInstruction\":" << it->st_memins << ",";
		oss << "\"endMemoryInstruction\":" << it->end_memins << ",";
		oss << "\"accessList\":"
			<< "[";
		if ((it->access_list).size() > 0)
		{
			separator = "";
			for (vector<ThreadMemAccess>::iterator it0 = (it->access_list).begin(); it0 != (it->access_list).end(); ++it0)
			{
				oss << separator << "{";
				oss << "\"threadId\":" << dec << it0->thread_id << ",";
				oss << "\"startTime\":" << it0->start_time << ",";
				oss << "\"endTime\":" << it0->end_time << ",";
				oss << "\"accessedAddressLow\":\"" << hex << it0->accessed_addr_low << "\",";
				oss << "\"accessedAddressHigh\":\"" << hex << it0->accessed_addr_high << "\",";
				oss << "\"dynamicRead\":" << dec << it0->dynamic_read << ",";
				oss << "\"dynamicWrite\":" << dec << it0->dynamic_write << ",";
				oss << "\"readInCache\":" << dec << it0->read_in_cache << ",";
				oss << "\"stridedRead\":" << dec << it0->strided_read << ",";
				oss << "\"pointerChasingRead\":" << dec << it0->pointerchasing_read << ",";
				oss << "\"randomRead\":" << dec << it0->random_read;
				oss << "}";
				separator = ",";
			}
		}
		oss << "]";
		oss << "}";
		separator = ",";
	}
	oss << "],";
	// activelist to JSON
	oss << "\"activelist\":[";
	separator = "";
	for (vector<TraceObjMeta>::iterator it = activelist.begin(); it != activelist.end(); ++it)
	{
		oss << separator << dec << it->obj_id;
		separator = ",";
	}
	oss << "],";
	// TLS to JSON
	oss << "\"TLS\":[";
	separator = "";
	for (vector<THREADID>::iterator it = tid_list.begin(); it != tid_list.end(); ++it)
	{
		ThreadDataType *t_data = get_tls(tls_key, *it);
		oss << separator << "{";
		oss << "\"tid\":" << *it << ",";
		oss << "\"startTime\":" << t_data->start_time << ",";
		oss << "\"endTime\":" << t_data->end_time << ",";
		oss << "\"accessedObjs\":"
			<< "[";
		if (t_data->accessed_objects.size() > 0)
		{
			separator = "";
			for (map<int, TraceObjThreadAccess>::iterator mit = (t_data->accessed_objects).begin(); mit != (t_data->accessed_objects).end(); ++mit)
			{
				oss << separator << "{";
				TraceObjThreadAccess tota = mit->second;
				oss << "\"objId\":" << dec << mit->first << ",";
				oss << "\"st_addr\":\"" << hex << tota.st_addr << "\",";
				oss << "\"start_time\":" << dec << tota.start_time << ",";
				oss << "\"end_time\":" << dec << tota.end_time << ",";
				oss << "\"dynamic_read\":" << dec << tota.dynamic_read << ",";
				oss << "\"dynamic_write\":" << dec << tota.dynamic_write << ",";
				oss << "\"read_in_cache\":" << dec << tota.read_in_cache << ",";
				oss << "\"strided_read\":" << dec << tota.strided_read << ",";
				oss << "\"pointerchasing_read\":" << dec << tota.pointerchasing_read << ",";
				oss << "\"random_read\":" << dec << tota.random_read << ",";
				oss << "\"last_accessed_addr\":\"" << hex << tota.last_accessed_addr << "\",";
				oss << "\"last_accessed_addr_cacheline\":\"" << hex << tota.last_accessed_addr_cacheline << "\",";
				oss << "\"last_accessed_addr_value\":\"" << hex << tota.last_accessed_addr_value << "\",";
				oss << "\"access_stride\":" << dec << tota.access_stride;
				oss << "}";
				separator = ",";
			}
		}
		oss << "]";
		oss << "}";
		separator = ",";
	}
	oss << "]";
	oss << "}";
	return oss.str();
}

/* ===================================================================== */
// TCP client socket thread.
/* ===================================================================== */
VOID Client_task(VOID *args)
{
	// 生成要发送的 json 字符串
	string message;
	char buf[256];
	// 发送第一条消息，用于设置 expId
	message = objs_to_json(true, false);
	message += "\n";
	write(client_sock, message.c_str(), message.length() + 1);
	read(client_sock, buf, sizeof(buf));
	exp_id = atoi(buf);
	while (1)
	{
		message = objs_to_json();
		message += "\n";
		// cout << "======write message :======" << endl << message << endl;
		write(client_sock, message.c_str(), message.length() + 1);
		read(client_sock, buf, sizeof(buf));
		// cout << "======read  message :======" << endl << buf << endl;
		OS_Sleep(SOCKET_SEND_INTERVAL); // milli-seconds
	}
	// 关闭 client_sock 留在 Fini 中
	return;
}

void start_thread()
{
	client_sock = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(SOCKET_SERVER_PORT);

	// convert IPv4 and IPv6 addresses from text to binary form
	if (inet_pton(AF_INET, SOCKET_SERVER_IP, &serv_addr.sin_addr) == -1)
	{
		printf("inet_pton error.\n");
		close(client_sock);
		exit(1);
	}

	printf("bind in %s : %d ...\n", inet_ntoa(serv_addr.sin_addr), ntohs(serv_addr.sin_port));

	int conn = connect(client_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
	if (conn == -1)
	{
		printf("connect error.\n");
		close(client_sock);
		exit(1);
	}

	OS_CreateThread(Client_task, 0, NULL, 1024 * 1024, &native_tid);
	printf("Create thread in pintool. \n");
}

int main(int argc, char *argv[])
{
	PIN_InitSymbols();
	if (PIN_Init(argc, argv))
	{
		cerr << "Failed to Init Pin.\n";
		return Usage();
	}

	// Open the file for output
	stringstream ss;
	if (KnobOutputFile.Value().length() > 0)
		ss << "pintrace." << KnobOutputFile.Value() << "." << PIN_GetPid();
	else
		ss << "pintrace."
		   << "." << PIN_GetPid();
	ss << ".memtrace";
	memtrace_file.open(ss.str().c_str());
	memtrace_file.setf(ios::showbase);

	//Init global variables
	dynamic_read_ins = 0;
	dynamic_write_ins = 0;
	static_read_ins = 0;
	static_write_ins = 0;
	is_record_img = false;
	is_record_rtn = false;
	global_ins_cnt = 0;
	send_times = 0;
	exp_id = -1;

	// Read in the user defined allocation routines
	string allocfilename(KnobAllocFuncFile.Value());
	setup_allocfunclist(list_alloc_func, allocfilename);
	if (list_alloc_func.size() == 0)
		setup_allocfunclist(list_alloc_func);
	// Read in the user defined free routines
	string freefilename(KnobFreeFuncFile.Value());
	setup_freefunclist(list_free_func, freefilename);

	// Find static allocations
	get_static_allocation(masterlist, activelist);

	// Different granularity here from coarse to refined
	IMG_AddInstrumentFunction(Image, 0);
	TRACE_AddInstrumentFunction(Trace, 0);

	//Use thread local storage for thead-level memory accesses
	PIN_InitLock(&lock);
	// Obtain  a key for TLS storage.
	tls_key = PIN_CreateThreadDataKey(0);
	PIN_AddThreadStartFunction(ThreadStart, 0);
	PIN_AddThreadFiniFunction(ThreadFini, 0);

	PIN_AddFiniFunction(Fini, 0);
	// Callback functions to invoke before Pin releases control of the application
	PIN_AddDetachFunction(DetachFunc, 0);

	// When illegal memory access occurs, 'handler' function will be invoked.
	signal(SIGSEGV, handler);

	time_start = gettime_in_sec();

	// Tcp client socket
	start_thread();

	// Never returns
	PIN_StartProgram();

	return 0;
}
