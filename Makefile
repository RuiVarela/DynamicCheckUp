CC 		:= g++
FLAGS 	:= -g -Wall -W -DDCU_THREAD_SAFE -DDCU_ABORT_ON_RELEASE_NOT_REQUESTED_MEMORY -DDCU_ABORT_ON_MEMORY_OVERWRITE
#FLAGS 	:= -g -Wall -W -DDCU_THREAD_SAFE -DDCU_ABORT_ON_RELEASE_NOT_REQUESTED_MEMORY -DDCU_ABORT_ON_MEMORY_OVERWRITE -DDCU_C_MEMORY_CHECK
#FLAGS 	:= -g -Wall -W -DDCU_THREAD_SAFE -DDCU_ABORT_ON_RELEASE_NOT_REQUESTED_MEMORY -DDCU_ABORT_ON_MEMORY_OVERWRITE -DDCU_C_MEMORY_CHECK -DDCU_ECHO
SHLIBS  := -ldl

TEST_APP:= Dynamic_DCU_UnitTest Static_DCU_UnitTest
TEST_SRC:= DynamicCheckUpUnitTest.cpp
TEST_OBJ:= $(patsubst %.cpp, %.o, $(TEST_SRC))

DCU_SRC := DynamicCheckUp.cpp
DCU_OBJ := $(patsubst %.cpp, %.o, $(DCU_SRC))
DCU_SOBJ:= $(patsubst %.o, %.so, $(DCU_OBJ))

OBJ		:= $(TEST_OBJ) $(DCU_OBJ)

all: $(OBJ) $(DCU_SOBJ) test

clean:
	rm -f $(OBJ) $(DCU_SOBJ) $(TEST_APP)

test: $(TEST_APP)

%.o: %.cpp
	$(CC) -fPIC $(FLAGS) -c $< -o $@

%.so : %.o
	$(CC) -shared $(FLAGS) -o $@ $< $(SHLIBS)
	
Dynamic_DCU_UnitTest: $(TEST_OBJ)
	$(CC) $(FLAGS) $^ -o $@ 
	
Static_DCU_UnitTest: $(TEST_OBJ) $(DCU_OBJ)
	$(CC) $(FLAGS) $^ -o $@ -ldl
	