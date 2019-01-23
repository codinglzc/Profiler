SHELL:=/bin/bash
CC=g++
BASE=/home/lzc/pin-3.7
SRC=${BASE}/source
EXT=${BASE}/extras
LIB=${BASE}/intel64
USR_FLAGS=#-DDEBUG#-DTIMING #-DVERBOSE

CFLAGS= -g -std=c++0x -DBIGARRAY_MULTIPLIER=1 -Wall -Werror -Wno-unknown-pragmas -D__PIN__=1 -DPIN_CRT=1 -fno-stack-protector -fno-exceptions -funwind-tables -fasynchronous-unwind-tables -fno-rtti -DTARGET_IA32E -DHOST_IA32E -fPIC -DTARGET_LINUX -fabi-version=2  -I${SRC}/include/pin -I${SRC}/include/pin/gen -isystem  ${BASE}/extras/stlport/include -isystem  ${BASE}/extras/libstdc++/include -isystem  ${BASE}/extras/crt/include -isystem  ${BASE}/extras/crt/include/arch-x86_64 -isystem  ${BASE}/extras/crt/include/kernel/uapi -isystem  ${BASE}/extras/crt/include/kernel/uapi/asm-x86 -I ${EXT}/components/include -I${EXT}/xed-intel64/include/xed -I${SRC}/tools/InstLib -O3 -fomit-frame-pointer -fno-strict-aliasing ${USR_FLAGS} -I/home/1bp/aspenR/pintrace -I.

CLINKS= -std=c++0x -shared -Wl,--hash-style=sysv  ${LIB}/runtime/pincrt/crtbeginS.o -Wl,-Bsymbolic -Wl,--version-script=${SRC}/include/pin/pintool.ver -fabi-version=2 -L ${LIB}/runtime/pincrt -L ${LIB}/lib -L ${LIB}/lib-ext -L ${EXT}/xed-intel64/lib -lpin -lxed  ${LIB}/runtime/pincrt/crtendS.o -lpin3dwarf  -ldl-dynamic -nostdlib -lstlport-dynamic -lm-dynamic -lc-dynamic -lunwind-dynamic 

SOURCES=profiler_data_structure.cpp profiler_elf.cpp profiler_pintool.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXE=PROFILER

# HME etc
HME_RUN_PATH=/home/HME-test/scripts/runenv.sh

default:${SOURCES} ${EXE}

${EXE}:${OBJECTS}
	${CC} -o $@  ${OBJECTS}  ${CLINKS}

.cpp.o:
	${CC} ${CFLAGS} $< -c -o $@

test: 
	${CC} -fopenmp -O0 -g -o profiler_test profiler_test.cpp -lnuma

run: $(EXE) test
	${BASE}/pin -t $(EXE) -a conf/alloc_func.conf -f conf/free_func.conf -o log  -- ./profiler_test

clean:
	rm -rf *.o *.a *.so *~ *.exe ${EXE}
	
veryclean:clean
	rm -rf pintrace.* *.log profiler_test

auto: veryclean default test run

hmerun: $(EXE) test
	numactl --cpunodebind=0 --membind=0 ${HME_RUN_PATH} ${BASE}/pin -t $(EXE) -o log  -- ./profiler_test

hmeauto: veryclean default test hmerun

