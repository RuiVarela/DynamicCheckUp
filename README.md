# Memory Analyzer and leak checker
Author: Rui Varela <rui.filipe.varela@gmail.com>
		
## Build instructions
+ unpack DynamicCheckUp compressed file
+ make
+ optionally run the DynamicCheckUp unit test
    ~~~
    ./DynamicCheckUp ./Dynamic_DCU_UnitTest
    ~~~~
    
+ optionally run the DynamicCheckUp on every day applications
    ~~~~
    ./DynamicCheckUp $(which ls)
    ./DynamicCheckUp $(which glxinfo)
    ./DynamicCheckUp $(which glxgears)
    ~~~~
    
    
    
## How To Use
+ compile the target application with debug symbols (-g)
+ run a dynamic check-up on the application
    ~~~
    ./DynamicCheckUp ./MyTargetApplication Parameter_1 Parameter_2 Parameter_3
    ~~~			
    
+ DynamicCheckUp log will be written to "memory_check_up.txt"
		
## Makefile Flags
+ DCU_THREAD_SAFE
  - Use thread synchronization for memory requests and releases.
+ DCU_C_MEMORY_CHECK
  - CheckUp C memory functions (malloc, calloc, realloc, free)
  - WARNING : C memory check-up is not accurate, and problems may be incorrectly reported
+ DCU_ECHO
  - Echo information while running application (only useful for static linking debug)
+ DCU_ABORT_ON_RELEASE_NOT_REQUESTED_MEMORY
  - Aborts application and reporst when a memory release (free, delete) occurs on not requested memory (new, malloc, calloc)
+ DDCU_ABORT_ON_MEMORY_OVERWRITE
  - Aborts application and reports when a memory overwrite occurs
		
## Revisions
+ xx.12.08 - Main code development.
+ 15.01.09 - Main leak detection code.
+ 20.01.09 - Locate several leaks on the same allocation frame.
  - Parse DCU_OUTPUT_FILE and resolve hex addresses to function names, filenames and lines.
+ 21.01.09 - DCU_FreeNullType problem detection implementation.
  - DCU_RequestZeroMemoryType problem detection implementation.
+ 26.01.09 - DCU_MismatchOperationType problem detection implementation.
+ 27.01.09 - DCU_ReleaseUnallocatedType problem detection implementation.
  - Memory Overwrite Protection implementation.
+ 28.10.09 - Major revision on system. Use of public domain dlmalloc allocator as underlying memory allocator
  - __builtin_return_address was replaced with backtrace :)
  - The first call to backtrace calls malloc, so we must call it explicity on DCU_initialize()
  - Support for x64 systems.
  - added memalign (but it is not used by the memory management system)
