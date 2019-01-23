#ifndef PROFILER_ELF_H
#define PROFILER_ELF_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif


#include <link.h>
#include <unistd.h> //for readlink
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <climits>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <vector>
#include "elf.h"
#include <sys/syscall.h>

#include "profiler_config.h"

#define MAX_PATH_LEN 128

/*
 * 在pin3.6里面，这些变量已经被定义在：/root/pin-3.6/extras/crt/include/freebsd/sys/sys/中
 * 如果使用的是pin3.5及以下版本，则需要将注释取消
 */
//#define ELF32_ST_VISIBILITY(o)  ((o) & 0x03)

/* For ELF64 the definitions are the same.  */
//#define ELF64_ST_VISIBILITY(o)  ELF32_ST_VISIBILITY (o)

/* Symbol visibility specification encoded in the st_other field.  */
//#define STV_DEFAULT     0               /* Default symbol visibility rules */
//#define STV_INTERNAL    1               /* Processor specific hidden class */
//#define STV_HIDDEN      2               /* Sym unavailable in other modules */
//#define STV_PROTECTED   3               /* Not preemptible, not exported */

void get_stack_range(uint64_t* stack_base, uint64_t*stack_end);

void get_static_allocation(std::vector<MetaObj>  &masterlist,
                           std::vector<TraceObjMeta> &activelist);

void getGlobalSym(const Elf64_Sym* symbols, const char* strtab, uint64_t base_addr,
                  int num_global, int id_bss,int id_data,int id_rodata,
                  std::vector<MetaObj> &masterlist,
                  std::vector<TraceObjMeta> &activelist);

void getLocalSym(const Elf64_Sym* symbols, const char* strtab, uint64_t base_addr,
                 int num_global, int id_bss, int id_data,int id_rodat,
                 std::vector<MetaObj> &masterlist,
		 std::vector<TraceObjMeta> &activelista);

void elf_read_symbols(char* pathname, struct dl_phdr_info *info,
                      std::vector<MetaObj> &masterlist,
                      std::vector<TraceObjMeta> &activelist);

#endif
