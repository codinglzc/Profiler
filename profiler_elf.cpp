#include "profiler_data_structure.h"
#include "profiler_elf.h"

using namespace std;

void get_stack_range( uint64_t* stack_base, uint64_t*stack_end)
{
	ifstream map("/proc/self/maps");

	if (map.is_open())
	{
	    size_t buffer_size = (size_t)MAX_PATH_LEN;
		char *line = new char[buffer_size];
		bool  hasFoundStack = false;
		map.getline(line, buffer_size);

		int last_pos = 0;
		while (!hasFoundStack)
		{
			while (map.fail() && map.gcount() == (streamsize)(buffer_size-1))
			{
				//printf("line is truncated! && ptr.gcount()=%lu map.tellg()=%i\n", map.gcount(), (int)map.tellg());
				map.clear();
				map.seekg(last_pos, ios_base::beg);
				delete []line;
				buffer_size *= 2;
				line = new char[buffer_size];
				map.getline(line, buffer_size);
				//printf("new ptr.gcount()=%lu map.tellg()=%i\n", map.gcount(), (int)map.tellg());
      		}

      		if (map.good())
			{
				last_pos = map.tellg();
				/* Template:
				  7ffff7ffe000-7ffff7fff000 rw-p 00000000 00:00 0 
				  7ffffffde000-7ffffffff000 rw-p 00000000 00:00 0                          [stack]
				  ffffffffff600000-ffffffffff601000 r-xp 00000000 00:00 0                  [vsyscall]
				 */
				if (strstr(line, "[stack]"))
				{
		  			string s(line);
		  			size_t pos0 = s.find("-");
		  			size_t pos1 = s.find(" ");
		  			if( pos0 != string::npos && pos1 != string::npos)
					{
		    			string st  = s.substr(0, pos0+1);
		    			string end = s.substr(pos0+1, (pos1-pos0)+1);
		    			*stack_base = (uint64_t) strtoull(st.c_str(), NULL, 16);
		    			*stack_end  = (uint64_t) strtoull(end.c_str(), NULL, 16);
		    			printf("found stack %#lx  %#lx \n", *stack_base, *stack_end);
		    			hasFoundStack=true;	    
		  			}else{
		    			printf("Error: unable to parse stack range\n");
		    			break;
		  			}
				}else
		  			map.getline(line, buffer_size);
	      	}else{
			printf("\nError: failed in getline from process memory map\n");
			break;
      		}
    	}

	delete []line;

	}else{
		printf("Error:cannot open process memory map\n");
	}

	map.close();
}


/*-------------------------------
ELF Layout I: Linking View (链接视图)
---------------------------------
ELF Header:   Elf64_Ehdr (描述整个文件的组织)
---------------------------------
Program (Optional)
Header Table: Elf64_Phdr (在链接时忽略)
---------------------------------
Section 1: 节
---------------------------------
Section 2:
---------------------------------
Section X:
---------------------------------
Section
Header Table: Elf64_Shdr (节区头部表：包含了描述文件节区的信息，比如大小、偏移等)
---------------------------------*/


/*--------------------------------
 ELF Layout II: Execution View (执行视图)
 ---------------------------------
 ELF Header:   Elf64_Ehdr (同上)
 ---------------------------------
 Program
 Header Table: Elf64_Phdr (程序头部表：描述文件中的各种段，用来告诉系统如何创建进程映像)
 ---------------------------------
 Segment 1: 段	(段和节是包含关系，一个段包含若干节)
 ---------------------------------
 Segment 2:
 ---------------------------------
 Segment X:
 ---------------------------------
 Section (Optional)
 Header Table: Elf64_Shdr (在执行时忽略)
 ---------------------------------*/

  
//typedef struct	// ELF头部的数据结构
//148 {
//149   unsigned char e_ident[EI_NIDENT];     /* Magic number and other info 			(魔术和相关信息) */
//150   Elf64_Half    e_type;                 /* Object file type 						(目标文件类型) */
//151   Elf64_Half    e_machine;              /* Architecture 							(硬件体系) */
//152   Elf64_Word    e_version;              /* Object file version 					(目标文件版本) */
//153   Elf64_Addr    e_entry;                /* Entry point virtual address 			(程序入口的虚拟地址) */
//154   Elf64_Off     e_phoff;                /* Program header table file offset 		(程序头部表的偏移量) */
//155   Elf64_Off     e_shoff;                /* Section header table file offset 		(节头部表的偏移量) */
//156   Elf64_Word    e_flags;                /* Processor-specific flags 				(处理器相关标志) */
//157   Elf64_Half    e_ehsize;               /* ELF header size in bytes 				(ELF头部长度) */
//158   Elf64_Half    e_phentsize;            /* Program header table entry size 		(程序头部中一个条目的大小) */
//159   Elf64_Half    e_phnum;                /* Program header table entry count 		(程序头部条目个数) */
//160   Elf64_Half    e_shentsize;            /* Section header table entry size 		(节头部中一个条目的大小) */
//161   Elf64_Half    e_shnum;                /* Section header table entry count 		(节头部条目个数) */
//162   Elf64_Half    e_shstrndx;             /* Section header string table index 		(节头部字符表索引) */
//163 } Elf64_Ehdr;

//typedef struct 	// 程序头表
//{
//      Elf32_Word  p_type;               /* 段类型 */
//      Elf32_Off   p_offset;             /* 段位置相对于文件开始处的偏移量 */
//      Elf32_Addr  p_vaddr;              /* 段在内存中的地址 */
//      Elf32_Addr  p_paddr;              /* 段的物理地址 */
//      Elf32_Word  p_filesz;             /* 段在文件中的长度 */
//      Elf32_Word  p_memsz;              /* 段在内存中的长度 */
//      Elf32_Word  p_flags;              /* 段的标记 */
//      Elf32_Word  p_align;              /* 段在内存中对齐标记 */
//} Elf32_Phdr;


//275 typedef struct	// 节的数据结构
//276 {
//277   Elf64_Word    sh_name;                /* Section name (string tbl index) */
//278   Elf64_Word    sh_type;                /* Section type */
//279   Elf64_Xword   sh_flags;               /* Section flags (节在进程中执行的特性——读/写)*/
//280   Elf64_Addr    sh_addr;                /* Section virtual addr at execution */
//281   Elf64_Off     sh_offset;              /* Section file offset */
//282   Elf64_Xword   sh_size;                /* Section size in bytes */
//283   Elf64_Word    sh_link;                /* Link to another section */
//284   Elf64_Word    sh_info;                /* Additional section information */
//285   Elf64_Xword   sh_addralign;           /* Section alignment */
//286   Elf64_Xword   sh_entsize;             /* Entry size if section holds table */
//287 } Elf64_Shdr;

void elf_read_symbols(char* pathname, struct dl_phdr_info *info, 
                      vector<MetaObj>  &masterlist,
                      vector<TraceObjMeta> &activelist)
{
	uint64_t base_addr=0;
    if(info)
        base_addr = info->dlpi_addr;

    ifstream obj(pathname);
    if(obj.is_open())
	{
    	//Read ELF Header first 
    	Elf64_Ehdr ehdr;
    	obj.read((char*)(&ehdr), sizeof(Elf64_Ehdr));
    	if(ehdr.e_ident[EI_CLASS] != ELFCLASS64)
		{
      		cerr << "The object is NOT for 64-bit architecture\n";
      		return;
    	}

    	//go to the section header string table, which is inside Section header table
    	obj.seekg(ehdr.e_shoff + ehdr.e_shstrndx * ehdr.e_shentsize);
    	//read the section header of the section header string table
    	Elf64_Shdr shdr;
    	obj.read((char*)(&shdr), sizeof(Elf64_Shdr));

    	//get the Section size in bytes of the section header string table
    	uint64_t size_sec = (uint64_t)shdr.sh_size;
	    char sechdr_strtab[size_sec];

	    //read in the contect of the section header string table
	    obj.seekg(shdr.sh_offset);
	    obj.read(sechdr_strtab, size_sec);
	    
	    //now interate through each entry in the section header table
	    //In principle we need the symbol tables (.symtab) and the strings tables (.strtab)
	    //Or its minimal set:  the dynamic linking symbol table (.dynsym) and  (.dynstr)
	    char *string_table=NULL, *symbol_table=NULL;
	    char *dynsym_table=NULL, *dynstr_table=NULL;
	    int num_local = 0, num_global = 0;
	    int id_bss = -1, id_data = -1, id_rodata = -1;
	    int s, num_shdr = ehdr.e_shnum;
	    obj.seekg(ehdr.e_shoff);

	    for(s = 0; s < num_shdr; s++)
		{
      		obj.read((char*)(&shdr), sizeof(Elf64_Shdr));
      		streampos curr_pos = obj.tellg(); // 当前定位指针的位置，也代表输入流的大小。

      		if(strcmp(sechdr_strtab + shdr.sh_name, ".bss") == 0)
			{
		    	id_bss = s;
		    }else if(strcmp(sechdr_strtab + shdr.sh_name, ".data") == 0)
			{
				id_data = s;
		    }else if(strcmp(sechdr_strtab + shdr.sh_name, ".rodata") == 0)
			{
				id_rodata = s;
		    }else if(shdr.sh_type == SHT_STRTAB)
			{
				//An object file may have multiple string table sections
				//check whether the section name is .strtab or .dynstr
				if(strcmp(sechdr_strtab + shdr.sh_name, ".strtab") == 0)
				{
					string_table = (char*)malloc(sizeof(char) * shdr.sh_size);
					obj.seekg(shdr.sh_offset);
					obj.read(string_table, shdr.sh_size);
				}else if(strcmp(sechdr_strtab+shdr.sh_name, ".dynstr") == 0)
				{
					dynstr_table = (char*)malloc(sizeof(char) * shdr.sh_size);
					obj.seekg(shdr.sh_offset);
					obj.read(dynstr_table, shdr.sh_size);
				}
      		}else if(shdr.sh_type == SHT_SYMTAB)
      		{
	          	//check if the section name is .symtab
	          	if(strcmp(sechdr_strtab+shdr.sh_name, ".symtab") == 0)
				{
					symbol_table = (char*)malloc(sizeof(char)*shdr.sh_size);
					obj.seekg(shdr.sh_offset);
					obj.read(symbol_table, shdr.sh_size);

					//The global symbols immediately follow the local symbols in the symbol table.
					//The first global symbol is identified by the symbol table sh_info value.
					//Local and global symbols are always kept separate in this manner, and cannot be mixed together.
					num_local = shdr.sh_info;
					num_global= shdr.sh_size/shdr.sh_entsize - num_local;
	          	}
	      	}else if(shdr.sh_type == SHT_DYNSYM)
	      	{
				//check if the section name is .dynsym
				if(strcmp(sechdr_strtab+shdr.sh_name, ".dynsym") == 0)
				{
					dynsym_table = (char*)malloc(sizeof(char)*shdr.sh_size);
					obj.seekg(shdr.sh_offset);
					obj.read(dynsym_table, shdr.sh_size);
					num_local =0;
					num_global=shdr.sh_size/shdr.sh_entsize;
				}
			}
			obj.seekg(curr_pos);
    	}// end of all section headers

		//if we have the whole table, we parse all symbols
		if(string_table && symbol_table)
		{
			getLocalSym((Elf64_Sym*)symbol_table, string_table,
                    	base_addr, num_local, id_bss, id_data,
                    	id_rodata, masterlist, activelist);
        
        	getGlobalSym(((Elf64_Sym*)symbol_table + num_local),string_table,
                     	base_addr, num_global, id_bss, id_data,
                     	id_rodata, masterlist, activelist);

     	//if we only have the minimum set of dynamic linking
    	}else if(dynstr_table && dynsym_table)
    	{
	        getGlobalSym( (Elf64_Sym*)dynsym_table, dynstr_table,
	                     base_addr, num_global, id_bss, id_data,
	                     id_rodata, masterlist, activelist);
     
	    }else
		{
			cerr<< "Unable to read dynstr_table && dynsym_table or string_table && symbol_table.\n";
	    }

	    if(string_table) free(string_table);
	    if(symbol_table) free(symbol_table);
	    if(dynsym_table) free(dynsym_table);
	    if(dynstr_table) free(dynstr_table);

	}else{

		cerr<< "Unable to open object " << pathname <<endl;

	}
  
}

/*
  594 typedef struct
  595 {
  596   Elf64_Word    st_name;                //Symbol name (string tbl index)
  597   unsigned char st_info;                //Symbol type and binding
  598   unsigned char st_other;               //No defined meaning, 0
  599   Elf64_Section st_shndx;               //Section index
  600   Elf64_Addr    st_value;               //Symbol value
  601   Elf64_Xword   st_size;                //Symbol size
  602 } Elf64_Sym;
*/
/* How to extract and insert information held in the st_info field.
  609 
  610 #define ELF32_ST_BIND(val)              (((unsigned char) (val)) >> 4)
  611 #define ELF32_ST_TYPE(val)              ((val) & 0xf)
  612 #define ELF32_ST_INFO(bind, type)       (((bind) << 4) + ((type) & 0xf))
*/
/* Both Elf32_Sym and Elf64_Sym use the same one-byte st_info field.
  615 #define ELF64_ST_BIND(val)              ELF32_ST_BIND (val)
  616 #define ELF64_ST_TYPE(val)              ELF32_ST_TYPE (val)
  617 #define ELF64_ST_INFO(bind, type)       ELF32_ST_INFO ((bind), (type))
*/
void getLocalSym(const Elf64_Sym* symbols, const char* strtab, uint64_t base_addr, 
		 int num_local, int id_bss, int id_data, int id_rodata,
		 vector<MetaObj> &masterlist,
		 vector<TraceObjMeta> &activelist)
{  
	for(int s = 0; s < num_local; s++)
	{
    	if( ELF64_ST_TYPE(symbols[s].st_info) == STT_OBJECT && //STT_COMMON label uninitialized common blocks
        	symbols[s].st_size > Threshold_Size) 
        {      
			const char* sym_name = strtab + symbols[s].st_name;
#ifdef DEBUG
			cout << "Local sym_name=" << sym_name << ", index=" << symbols[s].st_shndx
				<< " Addr base="<<hex << base_addr << ",offset="<<(base_addr + symbols[s].st_value)
				<< " size=" << dec << symbols[s].st_size<<endl;
#endif

			MetaObj tempObj;
			tempObj.type = AllocType::STATIC_T;
			tempObj.creator_tid = 0;
			tempObj.source_code_info.append("Main Image");
			tempObj.var_name.append(sym_name); //cout << "\nlocal sym var_name "<<tempObj.var_name.c_str()<<endl;
			tempObj.st_addr = base_addr + symbols[s].st_value;
			tempObj.end_addr= tempObj.st_addr + symbols[s].st_size  -1;
			tempObj.size    = symbols[s].st_size;
			tempObj.obj_id  = masterlist.size();
			tempObj.st_ins  = 0;
			tempObj.st_memins  = 0;
			tempObj.num_threads= 0;
			masterlist.push_back(tempObj);
#ifdef DEBUG
			masterlist.back().print_meta(cout);
#endif
			//add the obj to active list
			MetaObj& a = masterlist.back();
			TraceObjMeta b( a );
			activelist.push_back(b);      
		}
	}
}


void getGlobalSym(const Elf64_Sym* symbols, const char* strtab, uint64_t base_addr, 
		  int num_global, int id_bss,int id_data, int id_rodata,
		  vector<MetaObj> &masterlist,
		  vector<TraceObjMeta> &activelist)
{
	for(int s = 0; s < num_global; s++)
	{
		//check if it is an data object: STT_OBJECT or STT_COMMON
		//check if it is global (STB_GLOBAL, STB_WEAK) 
		if( ELF64_ST_TYPE(symbols[s].st_info) == STT_OBJECT &&
			ELF64_ST_BIND(symbols[s].st_info) == STB_GLOBAL &&
			symbols[s].st_size > Threshold_Size) 
		{               
			//Check each  symbol's visibility by its st_other field
			//This visibility defines how that symbol may be accessed
			//As we only capture global symbols, only STV_DEFAULT and STV_PROTECTED 
			// global and weak symbols are visible outside of their defining component, the executable file or shared object. 
			//Local symbols are hidden.
			if(ELF64_ST_VISIBILITY(symbols[s].st_other) == STV_DEFAULT ||
				ELF64_ST_VISIBILITY(symbols[s].st_other) == STV_PROTECTED) 
			{

				const char* sym_name = strtab+symbols[s].st_name;
#ifdef DEBUG
				cout << "Global sym_name=" << sym_name << ", index=" << symbols[s].st_shndx 
					<< " Addr base="<<hex << base_addr << ", offset="<<(base_addr+symbols[s].st_value)
					<< " size=" << dec << symbols[s].st_size<<endl;
#endif

				MetaObj tempObj;
				tempObj.type = AllocType::STATIC_T;
				tempObj.creator_tid = 0;
				tempObj.source_code_info.append("Main Image");
				tempObj.var_name.append(sym_name);//cout<<"\nglobal sym var_name "<<tempObj.var_name.c_str()<<endl;
				tempObj.st_addr = base_addr+symbols[s].st_value;
				tempObj.end_addr= tempObj.st_addr + symbols[s].st_size -1;
				tempObj.size    = symbols[s].st_size;
				tempObj.obj_id  = masterlist.size();
				tempObj.st_ins  = 0;
				tempObj.st_memins  = 0;
				tempObj.num_threads= 0;
				masterlist.push_back(tempObj);
#ifdef DEBUG
				masterlist.back().print_meta(cout);
#endif
				//add the obj to active list
				MetaObj& a = masterlist.back();
				TraceObjMeta b( a );
				activelist.push_back(b);
      		}
    	}
  	}//end of all symbols
}

int elf_callback(struct dl_phdr_info *info, size_t size, void *data)
{
	size_t bufsiz = (size_t)MAX_PATH_LEN;
	char *buffer  = (char*)malloc(sizeof(char) * bufsiz );
    
	//The first object visited by callback is the main program.
	//For the main program, the dlpi_name field will be an empty string.
	if( strcmp(info->dlpi_name, "") == 0)
	{
		cout<< "Parse the main object "  <<endl;
    
		//ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
		//places the contents of the symbolic link pathname in the buffer buf, which has size bufsiz.
		size_t size_read = (size_t)readlink("/proc/self/exe", buffer, bufsiz);

		//resolve truncation 
		while(size_read == bufsiz)
		{
			bufsiz *= 2;
      		buffer  = (char*)realloc(buffer, sizeof(char) * bufsiz );
      		size_read = readlink("/proc/self/exe", (char*)buffer, bufsiz);
    	}
    
		if(size_read <= 0)
		{ 
			cout<< "Cannot find the symbolic link pathname of the main program " << endl; 
			return 0;
		}
    
		buffer[size_read] = '\0';
		cout<< "Parse the main object "<< buffer  <<endl;
    
		//elf_read_symbols(buffer, info);
    
	}else
	{
		cout<< " Parse " << info->dlpi_name <<endl;
	}

  
  free(buffer);

  return 0;

}


/*
struct dl_phdr_info {
   ElfW(Addr)        dlpi_addr;  //  Base address of object
   const char       *dlpi_name;  //  (Null-terminated) name of object
   const ElfW(Phdr) *dlpi_phdr;  //  Pointer to array of ELF program headers for this object
   ElfW(Half)        dlpi_phnum; //  # of items in dlpi_phdr

   // The following fields were added in glibc 2.4, after the first
   // version of this structure was available.  Check the size
   // argument passed to the dl_iterate_phdr callback to determine
   // whether or not each later member is available. 

   unsigned long long int dlpi_adds;
   //  Incremented when a new object may have been added
   unsigned long long int dlpi_subs;
   //  Incremented when an object may have been removed
   size_t dlpi_tls_modid;
   //  If there is a PT_TLS segment, its module ID as used in TLS relocations, else zero
   void  *dlpi_tls_data;
   // The address of the calling thread's instance
   // of this module's PT_TLS segment, if it has
   // one and it has been allocated in the calling
   // thread, otherwise a null pointer
};
*/

//This is not working in pin
void get_global_var()
{
	//walk through list of shared objects and calls the function callback once for each object
	//int dl_iterate_phdr(int (*callback) (struct dl_phdr_info *info, size_t size, void *data), void *data);
	dl_iterate_phdr(elf_callback, NULL);  
}


void get_static_allocation(vector<MetaObj>  &masterlist,
                           vector<TraceObjMeta> &activelist)
{
    size_t size_buffer = 256;
	// 正在运行的主程序文件路径
    char *buffer  = (char*)malloc(sizeof(char) * size_buffer );
    size_t size_read = (size_t) readlink("/proc/self/exe", buffer, size_buffer);
    
    //resolve truncation
    while(size_read == size_buffer)
	{
        size_buffer *= 2;
        buffer  = (char*) realloc(buffer, sizeof(char) * size_buffer );
        size_read = readlink("/proc/self/exe", (char*)buffer, size_buffer);
    }
    
    //skip if we cannot find the path
    if(size_read <= 0)
	{
        cerr << "Cannot find the symbolic link pathname of the main program.\n";
    }
    
    buffer[size_read] = '\0';

	// print the path of the application exe
    //cout<< " Parse the main object " << buffer << endl;

    elf_read_symbols(buffer, NULL, masterlist, activelist);
    
}
