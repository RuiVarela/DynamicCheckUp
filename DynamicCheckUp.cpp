/*
 *    Author: Rui Varela <rui.filipe.varela@gmail.com>
 *    		  http://ryven.kicks.ass.googlepages.com
 *
 *    Build instructions:
 *    - unpack DynamicCheckUp compressed file
 *    - make
 *    - optionally run the DynamicCheckUp unit test :
 *    		./DynamicCheckUp ./Dynamic_DCU_UnitTest
 *    - optionally run the DynamicCheckUp on every day applications
 *   		./DynamicCheckUp $(which ls)
 *    		./DynamicCheckUp $(which glxinfo)
 *          ./DynamicCheckUp $(which glxgears)
 *
 *    How To Use:
 *    - compile the target application with debug symbols (-g)
 *    - run a dynamic check-up on the application :
 *    		./DynamicCheckUp ./MyTargetApplication Parameter_1 Parameter_2 Parameter_3
 *    - DynamicCheckUp log will be written to "memory_check_up.txt"
 *
 *    Makefile Flags
 *    - DCU_THREAD_SAFE
 *    						Use thread synchronization for memory requests and releases.
 *    - DCU_C_MEMORY_CHECK
 *    						CheckUp C memory functions (malloc, calloc, realloc, free)
 *    						WARNING : C memory check-up is not accurate, and problems may be incorrectly reported
 *    - DCU_ECHO
 *    						Echo information while running application (only useful for static linking debug)
 *    - DCU_ABORT_ON_RELEASE_NOT_REQUESTED_MEMORY
 *    						Aborts application and reporst when a memory release (free, delete) occurs on not requested memory (new, malloc, calloc)
 *    - DDCU_ABORT_ON_MEMORY_OVERWRITE
 *    						Aborts application and reports when a memory overwrite occurs
 *
 *    Revisions:
 *    - xx.12.08 - Main code development.
 *    - 15.01.09 - Main leak detection code.
 *    - 20.01.09 - Locate several leaks on the same allocation frame.
 *               - Parse DCU_OUTPUT_FILE and resolve hex addresses to function names, filenames and lines.
 *    - 21.01.09 - DCU_FreeNullType problem detection implementation.
 *               - DCU_RequestZeroMemoryType problem detection implementation.
 *    - 26.01.09 - DCU_MismatchOperationType problem detection implementation.
 *    - 27.01.09 - DCU_ReleaseUnallocatedType problem detection implementation.
 *               - Memory Overwrite Protection implementation.
 *    - 28.10.09 - Major revision on system. Use of public domain dlmalloc allocator as underlying memory allocator
 *               - __builtin_return_address was replaced with backtrace :)
 *               - The first call to backtrace calls malloc, so we must call it explicity on DCU_initialize()
 *               - Support for x64 systems.
 *               - added memalign (but it is not used by the memory management system)
 *
 *
 */

static unsigned char DCU_flags = 0;

void DCU_initialize();
void DCU_shutdown();

static struct DCU_Bootstrap
{
	DCU_Bootstrap() { DCU_initialize(); }
	~DCU_Bootstrap() { DCU_shutdown(); }
} DCU_BootstrapObject;


#define USE_LOCKS 1
#define MSPACES 1
#define ONLY_MSPACES 1
#define NO_MALLINFO 1
//#define FOOTERS 1
#define DEFAULT_GRANULARITY (1 * 1024 * 1024)
#include "malloc.c.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <signal.h>
#include <execinfo.h>

class DCU_MutexScopedLock
{
public:
	DCU_MutexScopedLock(pthread_mutex_t& mutex) :
		mutex_(mutex)
	{
#ifdef DCU_THREAD_SAFE
		pthread_mutex_lock(&mutex_);
#endif //DCU_THREAD_SAFE
	}

	~DCU_MutexScopedLock()
	{
#ifdef DCU_THREAD_SAFE
		pthread_mutex_unlock(&mutex_);
#endif //DCU_THREAD_SAFE
	}
private:
	pthread_mutex_t& mutex_;
};

/*
 * OVERWRITE_DETECTION_DATA
 * OVERWRITE_DETECTION_DATA_SIZE
 * 		Allocate extra memory to detect memory over_write
 *
 * ALLOCATION_VALUE
 * 		Value used to initialize data.
 * 		Detects correct class initialization.
 * 		Helps to get segmentation fault on bad initializations.
 *
 * DEALLOCATION_VALUE
 * 		Value set on memory which will be deleted.
 *
 * Thanks to LeakTracker public domain project Homepage: <http://www.andreasen.org/LeakTracer/>
 * Authors:
 *  Erwin S. Andreasen <erwin@andreasen.org>
 *  Henner Zeller <H.Zeller@acm.org>
 *
 */

//#define OVERWRITE_DETECTION_DATA		"\x42\x41\x44\x21" //BAD!
#define OVERWRITE_DETECTION_DATA		"\xAA\xBB\xCC\xDD"
#define OVERWRITE_DETECTION_DATA_SIZE	(sizeof(OVERWRITE_DETECTION_DATA) - 1)
#define ALLOCATION_VALUE				0xAA
#define DEALLOCATION_VALUE				0xEE

#define DCU_DYNAMIC_OPERATION_TYPES		8
enum DCU_DynamicOperationType
{
	DCU_MallocType,
	DCU_FreeType,
	DCU_ReallocType,
	DCU_CallocType,
	DCU_NewType,
	DCU_DeleteType,
	DCU_NewArrayType,
	DCU_DeleteArrayType
};

static const char* DCU_OperationTypeNames[] =
{
		"Malloc",
		"Free",
		"Realloc",
		"Calloc",
		"new",
		"delete",
		"new[]",
		"delete[]"
};

#define DCU_DYNAMIC_PROBLEM_TYPES		6
enum DCU_ProblemType
{
	DCU_LeakType,
	DCU_ReleaseUnallocatedType,
	DCU_MismatchOperationType,
	DCU_FreeNullType,
	DCU_RequestZeroMemoryType,
	DCU_MemoryOverWriteType
};

static const char* DCU_ProblemTypenames[] =
{
		"Memory Leak",
		"Release Unallocated Memory",
		"Mismatch Memory Allocation/Deletion",
		"Free Null Pointer",
		"Request Zero Memory",
		"Memory Over-Write",
};

typedef void* DCU_Pointer;
typedef void const* DCU_ConstPointer;
typedef unsigned long DCU_MemoryInt;
typedef long DCU_SignedMemoryInt;

struct DCU_MemoryStats
{
	DCU_MemoryInt count;
	DCU_MemoryInt total_memory;
	DCU_MemoryInt max_value;
};

#define DCU_STACK_TRACE_SIZE 8

struct DCU_OperationInfo
{
	DCU_OperationInfo *next;
	DCU_DynamicOperationType type;
	DCU_ConstPointer memory_address;
	size_t size;
	DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE];
};

struct DCU_ProblemInfo
{
	DCU_ProblemInfo *next;
	DCU_ProblemType type;
	size_t size;
	size_t count;
	DCU_MemoryInt total_memory;
	DCU_ConstPointer allocation_stack[DCU_STACK_TRACE_SIZE];
	DCU_ConstPointer deallocation_stack[DCU_STACK_TRACE_SIZE];
};

#define DCU_INITIALIZED			1
#define DCU_TRACING				2
#define DCU_FINISHED			4
#define DCU_MUTEX_INITED		8

#define DCU_SET_FLAG(flag) (DCU_flags |= flag)
#define DCU_CLEAR_FLAG(flag) (DCU_flags &= ~flag)
#define DCU_STATE(flag) (DCU_flags & flag)

#define DCU_OUTPUT_FILE "memory_check_up.txt"
#define DCU_FALLBACK_STREAM stdout

typedef size_t HastIterator;
#define DCU_HASH_TABLE_SIZE 35323 //prime number, for many allocations use 343051
#define DCU_HASH_FUNCTION(address) (  HastIterator(address) % HastIterator(DCU_HASH_TABLE_SIZE) )

static mspace memory_space;
#define DCU_malloc(size) mspace_malloc(memory_space, size)
#define DCU_free(p) mspace_free(memory_space, p)
#define DCU_realloc(p, size) mspace_realloc(memory_space, p, size)
#define DCU_calloc(nmemb, size) mspace_calloc(memory_space, nmemb, size)
#define DCU_memalign(msp, alignment, bytes) mspace_memalign(memory_space, alignment, bytes)

#define DCU_STREAM_BUFFER_SIZE 512
static FILE* DCU_stream;
static char stream_trace_buffer[DCU_STREAM_BUFFER_SIZE];

static pthread_mutex_t DCU_mutex;
static DCU_OperationInfo** DCU_memory;
static DCU_ProblemInfo* DCU_problems;

static DCU_MemoryStats DCU_memory_stats[DCU_DYNAMIC_OPERATION_TYPES];
static DCU_MemoryStats DCU_memory_stats_c;
static DCU_MemoryStats DCU_memory_stats_new;
static DCU_MemoryStats DCU_memory_stats_new_array;

static DCU_ConstPointer DCU_null_stack[DCU_STACK_TRACE_SIZE];

void* DCU_requestMemory(DCU_DynamicOperationType const& type, size_t size, void* pointer);
void DCU_releaseMemory(DCU_DynamicOperationType const& type, void* pointer);

void DCU_analyzeMemory();
void DCU_reportMemoryStatus();

DCU_OperationInfo* DCU_createOperation();
DCU_ProblemInfo* DCU_createProblem();

//
// Generic DCU_OperationInfo Linked-List Management
//
void DCU_emptyOperationList(DCU_OperationInfo** list);
void DCU_removeOperationFromList(DCU_OperationInfo** list, DCU_OperationInfo* element);
DCU_OperationInfo* DCU_findOperationOnList(DCU_OperationInfo* list, DCU_ConstPointer memory_address);
void DCU_addOperationToList(DCU_OperationInfo** list, DCU_OperationInfo* element);

//
// Generic DCU_ProblemInfo Linked-List Management
//
void DCU_emptyProblemList(DCU_ProblemInfo** list);
void DCU_addProblemToList(DCU_ProblemInfo** list, DCU_ProblemInfo* element);
DCU_ProblemInfo* DCU_findProblem(DCU_ProblemInfo** list, DCU_ProblemType const type,
								DCU_ConstPointer allocation_stack[DCU_STACK_TRACE_SIZE],
								DCU_ConstPointer deallocation_stack[DCU_STACK_TRACE_SIZE]);

//
// Hash table management
//
void DCU_addMemory(DCU_OperationInfo* element);
DCU_OperationInfo* DCU_findMemory(DCU_ConstPointer memory_address);
void DCU_removeMemory(DCU_OperationInfo* element);
void DCU_emptyMemory();

void DCU_createStackTrace(DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE]);
bool DCU_stacksMatch(DCU_ConstPointer lhs[DCU_STACK_TRACE_SIZE], DCU_ConstPointer rhs[DCU_STACK_TRACE_SIZE]);

void DCU_abort(char const* message, ...);
void DCU_write(char const* message, ...);

//
// Implementation
//

void DCU_initialize()
{
	if (DCU_STATE(DCU_INITIALIZED))
	{
		return;
	}

	if (DCU_STATE(DCU_MUTEX_INITED))
	{
		DCU_MutexScopedLock lock(DCU_mutex);
		if (DCU_STATE(DCU_INITIALIZED))
		{
			return;
		}
		else
		{
			fprintf(DCU_FALLBACK_STREAM, "DynamicCheckUp Concurrency error.\n");
			_exit(1);
		}
	}

	if (pthread_mutex_init(&DCU_mutex, 0) < 0)
	{
		fprintf(DCU_FALLBACK_STREAM, "DynamicCheckUp unable to initialize mutex\n");
		_exit(1);
	}
	else
	{
		DCU_SET_FLAG(DCU_MUTEX_INITED);
	}


	{
		DCU_MutexScopedLock lock(DCU_mutex);

		memory_space = create_mspace(0, 0);
		DCU_SET_FLAG(DCU_INITIALIZED);

		//
		// init backtrace so it wont recursively call malloc
		//
		DCU_Pointer stack[DCU_STACK_TRACE_SIZE];
		backtrace(stack, DCU_STACK_TRACE_SIZE);

		//
		// Init Tracing data
		//

		DCU_stream = DCU_FALLBACK_STREAM;
		memset(DCU_memory_stats, 0, sizeof(DCU_MemoryStats) * DCU_DYNAMIC_OPERATION_TYPES);
		memset(&DCU_memory_stats_new, 0, sizeof(DCU_MemoryStats));
		memset(&DCU_memory_stats_new_array, 0, sizeof(DCU_MemoryStats));
		memset(&DCU_memory_stats_c, 0, sizeof(DCU_MemoryStats));
		memset(DCU_null_stack, 0, sizeof(DCU_null_stack));

		//
		// Operations HashTable
		//
		DCU_memory = (DCU_OperationInfo**) DCU_malloc( DCU_HASH_TABLE_SIZE * sizeof(DCU_OperationInfo*) );
		memset(DCU_memory, 0, DCU_HASH_TABLE_SIZE * sizeof(DCU_OperationInfo*));

		//
		// Problems Linked-List
		//
		DCU_problems = 0;

		//
		// Open Log File
		//
		DCU_stream = fopen(DCU_OUTPUT_FILE, "w");
		if (DCU_stream < 0)
		{
			fprintf(DCU_FALLBACK_STREAM, "DynamicCheckUp: Unable to open %s: %m\n", DCU_OUTPUT_FILE);
			DCU_stream = DCU_FALLBACK_STREAM;
		}
		else
		{
			int flags = fcntl(fileno(DCU_stream), F_GETFD, 0);
			if (flags >= 0)
			{
				flags |= FD_CLOEXEC;
				fcntl(fileno(DCU_stream), F_SETFD, flags);
			}

			setvbuf(DCU_stream, stream_trace_buffer, _IOFBF, DCU_STREAM_BUFFER_SIZE);
		}

		DCU_SET_FLAG(DCU_TRACING);
	}

	DCU_write("DynamicCheckUp Started\n");
}

void DCU_shutdown()
{
	if (!DCU_STATE(DCU_FINISHED))
	{
		{
			DCU_MutexScopedLock lock(DCU_mutex);
			DCU_CLEAR_FLAG(DCU_TRACING);

			DCU_analyzeMemory();
			DCU_reportMemoryStatus();

			DCU_emptyMemory();
			DCU_free(DCU_memory);

			DCU_emptyProblemList(&DCU_problems);
		}

		if (DCU_stream != DCU_FALLBACK_STREAM)
		{
			fclose(DCU_stream);
			DCU_stream = DCU_FALLBACK_STREAM;
		}

		pthread_mutex_destroy(&DCU_mutex);
		DCU_SET_FLAG(DCU_FINISHED);
	}
}

void* DCU_requestMemory(DCU_DynamicOperationType const& type, size_t size, void* pointer)
{
	DCU_initialize();

	void* out = 0;
//	DCU_write("Request Type: %d Size:\t%d", type, size);

	if (!size && ((type == DCU_CallocType) || (type == DCU_MallocType) || (type == DCU_NewType) || (type == DCU_NewArrayType)))
	{
		DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE];
		DCU_createStackTrace(stack);

		if (DCU_STATE(DCU_TRACING))
		{
			DCU_MutexScopedLock lock(DCU_mutex);
			DCU_ProblemInfo* problem = DCU_findProblem(&DCU_problems, DCU_RequestZeroMemoryType, stack, DCU_null_stack);
			if (!problem)
			{
				problem = DCU_createProblem();
				problem->type = DCU_RequestZeroMemoryType;
				memcpy(problem->allocation_stack, stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
				DCU_addProblemToList(&DCU_problems, problem);
			}

			problem->count += 1;
		}

		return out;
	}

	if (type == DCU_CallocType)
	{
		out = DCU_malloc(size  + OVERWRITE_DETECTION_DATA_SIZE);
		if (out)
		{
			memset(out, 0, size);
		}
	}
	else if (type == DCU_ReallocType)
	{
		bool old_allocation_found = false;
		size_t old_size = 0;

		if (DCU_STATE(DCU_TRACING))
		{
			DCU_MutexScopedLock lock(DCU_mutex);
			DCU_OperationInfo* operation = DCU_findMemory(pointer);
			if (operation)
			{
				old_allocation_found = true;
				old_size = operation->size;
				DCU_removeMemory(operation);

				DCU_memory_stats[DCU_FreeType].count++;
				DCU_memory_stats[DCU_FreeType].total_memory += operation->size;
			}
		}

		out = DCU_malloc(size + OVERWRITE_DETECTION_DATA_SIZE);
		if (out)
		{
#ifdef ALLOCATION_VALUE
			memset(out, ALLOCATION_VALUE, size + OVERWRITE_DETECTION_DATA_SIZE);
#endif
			if (old_allocation_found)
			{
				memcpy(out, pointer, ((size > old_size) ? old_size : size));
			}
		}

		if (old_allocation_found)
		{
			DCU_free(pointer);
		}
	}
	else
	{
		out = DCU_malloc(size + OVERWRITE_DETECTION_DATA_SIZE);
#ifdef ALLOCATION_VALUE
	memset(out, ALLOCATION_VALUE, size + OVERWRITE_DETECTION_DATA_SIZE);
#endif
	}

	if (out)
	{

#ifdef OVERWRITE_DETECTION_DATA
        memcpy((char*)(out) + size, OVERWRITE_DETECTION_DATA, OVERWRITE_DETECTION_DATA_SIZE);
#endif

		DCU_MutexScopedLock lock(DCU_mutex);
		if (DCU_STATE(DCU_TRACING))
		{
			DCU_OperationInfo* operation = DCU_createOperation();
			operation->memory_address = out;
			operation->type = type;
			operation->size = size;

			DCU_createStackTrace(operation->stack);
			DCU_addMemory(operation);

			DCU_memory_stats[type].count++;
			DCU_memory_stats[type].total_memory += size;

			if (size > DCU_memory_stats[type].max_value)
			{
				DCU_memory_stats[type].max_value = size;
			}

		}
	}

//	DCU_write(" Done\n");

	return out;
}

void DCU_releaseMemory(DCU_DynamicOperationType const& type, void* pointer)
{
	DCU_initialize();

//	DCU_write("Release Type: %d Address:10%p", type, pointer);

	if (pointer)
	{
		{
			DCU_MutexScopedLock lock(DCU_mutex);
			if (DCU_STATE(DCU_TRACING))
			{
				DCU_OperationInfo* operation = DCU_findMemory(pointer);
				if (operation)
				{
					DCU_memory_stats[type].count++;
					DCU_memory_stats[type].total_memory += operation->size;

#ifdef OVERWRITE_DETECTION_DATA
					if (memcmp((char*)(pointer) + operation->size, OVERWRITE_DETECTION_DATA, OVERWRITE_DETECTION_DATA_SIZE))
					{
						DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE];
						DCU_createStackTrace(stack);

						DCU_ProblemInfo* problem = DCU_findProblem(&DCU_problems, DCU_MemoryOverWriteType, operation->stack, stack);
						if (!problem)
						{
							problem = DCU_createProblem();
							problem->type = DCU_MemoryOverWriteType;
							memcpy(problem->allocation_stack, operation->stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
							memcpy(problem->deallocation_stack, stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
							DCU_addProblemToList(&DCU_problems, problem);
						}
						problem->count += 1;

#ifdef DCU_ABORT_ON_MEMORY_OVERWRITE
						DCU_abort("Abnormal program termination : 'Memory Overwrite Detected'\n");
						return;
#endif //DDCU_ABORT_ON_MEMORY_OVERWRITE
					}
#endif

#ifdef DEALLOCATION_VALUE
					memset(pointer, DEALLOCATION_VALUE, operation->size + OVERWRITE_DETECTION_DATA_SIZE);
#endif //DEALLOCATION_VALUE

					//
					// Check for mismatch operations
					//
					bool mismatched_release = true;
					if (type == DCU_FreeType)
					{
						mismatched_release = ! ((operation->type == DCU_MallocType) || (operation->type == DCU_CallocType) || (operation->type == DCU_ReallocType));
					}
					else if (type == DCU_DeleteType)
					{
						mismatched_release = (operation->type != DCU_NewType);
					}
					else if (type == DCU_DeleteArrayType)
					{
						mismatched_release = (operation->type != DCU_NewArrayType);
					}

					if (mismatched_release)
					{
						DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE];
						DCU_createStackTrace(stack);

						DCU_ProblemInfo* problem = DCU_findProblem(&DCU_problems, DCU_MismatchOperationType, operation->stack, stack);
						if (!problem)
						{
							problem = DCU_createProblem();
							problem->type = DCU_MismatchOperationType;
							memcpy(problem->allocation_stack, operation->stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
							memcpy(problem->deallocation_stack, stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
							DCU_addProblemToList(&DCU_problems, problem);
						}
						problem->count += 1;
					}

				}
				else
				{
					//
					// Releasing unallocated data
					//

					DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE];
					DCU_createStackTrace(stack);

					DCU_ProblemInfo* problem = DCU_findProblem(&DCU_problems, DCU_ReleaseUnallocatedType, DCU_null_stack, stack);
					if (!problem)
					{
						problem = DCU_createProblem();
						problem->type = DCU_ReleaseUnallocatedType;
						memcpy(problem->deallocation_stack, stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
						DCU_addProblemToList(&DCU_problems, problem);
					}
					problem->count += 1;

#ifdef DCU_ABORT_ON_RELEASE_NOT_REQUESTED_MEMORY
					DCU_abort("Abnormal program termination : 'Release Unallocated Memory'\n");
					return;
#endif //DCU_ABORT_ON_RELEASE_NOT_REQUESTED_MEMORY
				}

				DCU_removeMemory(operation);
			}
		}

		DCU_free(pointer);
	}
	else // pointer is null
	{

#ifdef DCU_C_MEMORY_CHECK
		if (type == DCU_FreeType)
		{
			DCU_MutexScopedLock lock(DCU_mutex);
			if (DCU_STATE(DCU_TRACING))
			{
				DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE];
				DCU_createStackTrace(stack);

				DCU_ProblemInfo* problem = DCU_findProblem(&DCU_problems, DCU_FreeNullType, DCU_null_stack, stack);
				if (!problem)
				{
					problem = DCU_createProblem();
					problem->type = DCU_FreeNullType;
					memcpy(problem->deallocation_stack, stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
					DCU_addProblemToList(&DCU_problems, problem);
				}

				problem->count += 1;
			}
		}
#endif //DCU_C_MEMORY_CHECK

	}

//	DCU_write(" Done\n");
}

void DCU_analyzeMemory()
{
	//
	// Memory Stats
	//
	for (unsigned int i = 0; i != DCU_DYNAMIC_OPERATION_TYPES; ++i)
	{
		switch (i)
		{
			case DCU_MallocType:
			case DCU_ReallocType:
			case DCU_CallocType:
			{
				DCU_memory_stats_c.count += DCU_memory_stats[i].count;
				DCU_memory_stats_c.total_memory += DCU_memory_stats[i].total_memory;
			}
				break;
			case DCU_FreeType:
			{
				DCU_memory_stats_c.count -= DCU_memory_stats[i].count;
				DCU_memory_stats_c.total_memory -= DCU_memory_stats[i].total_memory;
			}
				break;
			case DCU_NewType:
			{
				DCU_memory_stats_new.count += DCU_memory_stats[i].count;
				DCU_memory_stats_new.total_memory += DCU_memory_stats[i].total_memory;
			}
				break;
			case DCU_DeleteType:
			{
				DCU_memory_stats_new.count -= DCU_memory_stats[i].count;
				DCU_memory_stats_new.total_memory -= DCU_memory_stats[i].total_memory;
			}
				break;
			case DCU_NewArrayType:
			{
				DCU_memory_stats_new_array.count += DCU_memory_stats[i].count;
				DCU_memory_stats_new_array.total_memory += DCU_memory_stats[i].total_memory;
			}
				break;
			case DCU_DeleteArrayType:
			{
				DCU_memory_stats_new_array.count -= DCU_memory_stats[i].count;
				DCU_memory_stats_new_array.total_memory -= DCU_memory_stats[i].total_memory;
			}
				break;
			default:
				break;
		}
	}

	//
	// Detect Memory Leaks
	//
	for (HastIterator hash_index = 0; hash_index != DCU_HASH_TABLE_SIZE; ++hash_index)
	{
		DCU_OperationInfo* iterator = DCU_memory[hash_index];
		while(iterator)
		{
			DCU_ProblemInfo* problem = DCU_findProblem(&DCU_problems, DCU_LeakType, iterator->stack, DCU_null_stack);
			if (!problem)
			{
				problem = DCU_createProblem();
				problem->type = DCU_LeakType;
				memcpy(problem->allocation_stack, iterator->stack, DCU_STACK_TRACE_SIZE * sizeof(DCU_ConstPointer));
				DCU_addProblemToList(&DCU_problems, problem);
			}
			problem->count += 1;
			problem->size = iterator->size;
			problem->total_memory += iterator->size;

			iterator = iterator->next;
		}
	}

}

void DCU_reportMemoryStatus()
{
	DCU_write("DynamicCheckUp Memory Report\n");
	DCU_write("----------------------------------------------------------------\n");
	DCU_write("%15s %15s %15s %15s\n", "->", "operations", "total mem", "max value");

	for (unsigned int i = 0; i != DCU_DYNAMIC_OPERATION_TYPES; ++i)
	{
#ifndef DCU_C_MEMORY_CHECK
		if (i < (unsigned int)(DCU_NewType))
		{
			continue;
		}
#endif //DCU_C_MEMORY_CHECK

		DCU_write("%15s %15d %15d %15d\n",
				DCU_OperationTypeNames[i],
				DCU_memory_stats[i].count, DCU_memory_stats[i].total_memory, DCU_memory_stats[i].max_value);
	}

	DCU_write("\nDynamic Memory Balance\n");
	DCU_write("----------------------------------------------------------------\n");
#ifdef DCU_C_MEMORY_CHECK
	DCU_write("%15s %15d %15d\n", "C Memory", DCU_memory_stats_c.count, DCU_memory_stats_c.total_memory);
#endif //DCU_C_MEMORY_CHECK
	DCU_write("%15s %15d %15d\n", "New Del", DCU_memory_stats_new.count, DCU_memory_stats_new.total_memory);
	DCU_write("%15s %15d %15d\n", "New Del[]", DCU_memory_stats_new_array.count, DCU_memory_stats_new_array.total_memory);

	DCU_write("\nProblems\n");
	DCU_write("----------------------------------------------------------------\n");

	DCU_ProblemInfo* iterator = DCU_problems;
	while (iterator)
	{
		bool needs_allocation_stack = false;
		bool needs_deallocation_stack = false;

		DCU_write("{\n");
		DCU_write("[%d] %s\n", iterator->type, DCU_ProblemTypenames[ iterator->type ]);
		DCU_write("Count: %d\n", iterator->count);

		if (iterator->type == DCU_LeakType)
		{
			DCU_write("Total Memory Lost: %d \n", iterator->total_memory);
			needs_allocation_stack = true;
		}

		if ((iterator->type == DCU_RequestZeroMemoryType) ||
			(iterator->type == DCU_MismatchOperationType) ||
			(iterator->type == DCU_MemoryOverWriteType))
		{
			needs_allocation_stack = true;
		}

		if ((iterator->type == DCU_FreeNullType) ||
			(iterator->type == DCU_MismatchOperationType) ||
			(iterator->type == DCU_ReleaseUnallocatedType) ||
			(iterator->type == DCU_MemoryOverWriteType))
		{
			needs_deallocation_stack = true;
		}

		if (needs_allocation_stack)
		{
			DCU_write("Allocation Stack: ");
			for (unsigned int frame = 0; frame != DCU_STACK_TRACE_SIZE; ++frame)
			{
				if (iterator->allocation_stack[frame])
				{
					DCU_write("%p ", iterator->allocation_stack[frame]);
				}
			}
			DCU_write("\n");
		}

		if (needs_deallocation_stack)
		{
			DCU_write("Deallocation Stack: ");
			for (unsigned int frame = 0; frame != DCU_STACK_TRACE_SIZE; ++frame)
			{
				if (iterator->deallocation_stack[frame])
				{
					DCU_write("%p ", iterator->deallocation_stack[frame]);
				}
			}
			DCU_write("\n");
		}

		DCU_write("}\n");
		iterator = iterator->next;
	}
}


void DCU_abort(char const* message, ...)
{
	va_list argp;
	va_start(argp, message);

#ifdef DCU_ECHO
if (DCU_stream != DCU_FALLBACK_STREAM)
{
	vfprintf(DCU_FALLBACK_STREAM, message, argp);
}
#endif //DDCU_ECHO

	vfprintf(DCU_stream, message, argp);
	va_end(argp);

	DCU_shutdown();

	kill(getpid(), SIGKILL);
	// just in case someone catches kill
	_exit(-1);
}

void DCU_write(char const* message, ...)
{
	va_list argp;
	va_start(argp, message);

#ifdef DCU_ECHO
	if (DCU_stream != DCU_FALLBACK_STREAM)
	{
		vfprintf(DCU_FALLBACK_STREAM, message, argp);
	}
#endif //DDCU_ECHO

	vfprintf(DCU_stream, message, argp);
	va_end(argp);
}


DCU_OperationInfo* DCU_createOperation()
{
	DCU_OperationInfo* element = (DCU_OperationInfo*) DCU_malloc( sizeof(DCU_OperationInfo) );
	memset(element, 0,sizeof(DCU_OperationInfo) );
	return element;
}

DCU_ProblemInfo* DCU_createProblem()
{
	DCU_ProblemInfo* element = (DCU_ProblemInfo*) DCU_malloc( sizeof(DCU_ProblemInfo) );
	memset(element, 0,sizeof(DCU_ProblemInfo) );
	return element;
}


//
// Operation management
//
void DCU_emptyOperationList(DCU_OperationInfo** list)
{
	DCU_OperationInfo* remove = 0;
	while (*list)
	{
		remove = *list;
		*list = (*list)->next;
		DCU_free(remove);
	}
}

void DCU_removeOperationFromList(DCU_OperationInfo** list, DCU_OperationInfo* element)
{
	if (element)
	{
		DCU_OperationInfo* iterator = *list;

		if (iterator == element) // is the first element?
		{
			*list = element->next;
			DCU_free(element);
		}
		else
		{
			while (iterator && (iterator->next != element))
			{
				iterator = iterator->next;
			}

			if (iterator)
			{
				iterator->next = element->next;
				DCU_free(element);
			}
		}
	}
}

DCU_OperationInfo* DCU_findOperationOnList(DCU_OperationInfo* list, DCU_ConstPointer memory_address)
{
	DCU_OperationInfo* iterator = list;

	while( iterator && (iterator->memory_address != memory_address) )
	{
		iterator = iterator->next;
	}

	return iterator;
}

void DCU_addOperationToList(DCU_OperationInfo** list, DCU_OperationInfo* element)
{
	element->next = *list;
	*list = element;
}

inline void DCU_addMemory(DCU_OperationInfo* element)
{
	if (element)
	{
		ptrdiff_t hash_table_index = DCU_HASH_FUNCTION(element->memory_address);
		DCU_addOperationToList(&DCU_memory[hash_table_index], element);
	}
}

inline DCU_OperationInfo* DCU_findMemory(DCU_ConstPointer memory_address)
{
	HastIterator hash_table_index = DCU_HASH_FUNCTION(memory_address);
	return DCU_findOperationOnList(DCU_memory[hash_table_index], memory_address);
}

inline void DCU_removeMemory(DCU_OperationInfo* element)
{
	if (element)
	{
		HastIterator hash_table_index = DCU_HASH_FUNCTION(element->memory_address);
		DCU_removeOperationFromList(&DCU_memory[hash_table_index], element);
	}
}

inline void DCU_emptyMemory()
{
	for (HastIterator i = 0; i != DCU_HASH_TABLE_SIZE; ++i)
	{
		DCU_emptyOperationList(&DCU_memory[i]);
	}
}

//
// Generic DCU_ProblemInfo Linked-List Management
//

void DCU_emptyProblemList(DCU_ProblemInfo** list)
{
	DCU_ProblemInfo* remove = 0;
	while (*list)
	{
		remove = *list;
		*list = (*list)->next;
		DCU_free(remove);
	}
}

void DCU_addProblemToList(DCU_ProblemInfo** list, DCU_ProblemInfo* element)
{
	element->next = *list;
	*list = element;
}

DCU_ProblemInfo* DCU_findProblem(DCU_ProblemInfo** list, DCU_ProblemType const type,
		DCU_ConstPointer allocation_stack[DCU_STACK_TRACE_SIZE],
		DCU_ConstPointer deallocation_stack[DCU_STACK_TRACE_SIZE])
{
	DCU_ProblemInfo* iterator = *list;
	bool not_found = true;

	while(not_found && iterator)
	{
		not_found = ( (iterator->type != type)
					|| !DCU_stacksMatch(allocation_stack, iterator->allocation_stack)
					|| !DCU_stacksMatch(deallocation_stack, iterator->deallocation_stack) );

		if (not_found)
		{
			iterator = iterator->next;
		}
	}

	return iterator;
}

inline void DCU_createStackTrace(DCU_ConstPointer stack[DCU_STACK_TRACE_SIZE])
{
	memset(stack, 0, sizeof(stack));
	backtrace((void**)(stack), DCU_STACK_TRACE_SIZE);
}

bool DCU_stacksMatch(DCU_ConstPointer lhs[DCU_STACK_TRACE_SIZE], DCU_ConstPointer rhs[DCU_STACK_TRACE_SIZE])
{
	bool match = true;

	for (unsigned int i = 0; i != DCU_STACK_TRACE_SIZE; ++i)
	{
		match &= (lhs[i] == rhs[i]);
	}

	return match;
}

//
// Really sick thing :
// request and release memory can get called before constructor
// that is why initialize is called at the beginning of the methods
//

//
// Function Hooks
//

void* operator new(size_t size)
{
	return DCU_requestMemory(DCU_NewType, size, 0);
}

void* operator new[](size_t size)
{
	return DCU_requestMemory(DCU_NewArrayType, size, 0);
}

void operator delete (void *p)
{
	DCU_releaseMemory(DCU_DeleteType, p);
}

void operator delete[] (void *p)
{
	DCU_releaseMemory(DCU_DeleteArrayType, p);
}

#ifdef DCU_C_MEMORY_CHECK

void *malloc(size_t size)
{
	return DCU_requestMemory(DCU_MallocType, size, 0);
}

void free(void* p)
{
	DCU_releaseMemory(DCU_FreeType, p);
}

void* realloc(void *p, size_t size)
{
	return DCU_requestMemory(DCU_ReallocType, size, p);
}

void* calloc(size_t nmemb, size_t size)
{
	return DCU_requestMemory(DCU_CallocType, size * nmemb, 0);
}

void* memalign(mspace msp, size_t alignment, size_t bytes)
{
	return DCU_memalign(msp, alignment, bytes);
}

#endif //DCU_C_MEMORY_CHECK
