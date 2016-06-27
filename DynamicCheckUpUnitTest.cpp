#include <iostream>
#include <cstdlib>

using namespace std;

void newTest()
{
	bool *bool_pointer0 = new bool[1];
	delete[] (bool_pointer0);

	int *int_pointer1 = new int();
	delete (int_pointer1);

	double *double0 = new double();
	delete (double0);

	bool_pointer0 = new bool[123];
	double0 = new double();
	int_pointer1 = new int();

	//
	// comment the following
	//
	delete (double0);
	delete[] (bool_pointer0);
	delete (int_pointer1);
}

void mallocTest()
{
	int *int_pointer0 = (int*) malloc(4);
	int *int_pointer1 = (int*) calloc(100, 40);
	int *int_pointer2 = (int*) malloc(4);
	int_pointer2 = (int*) realloc((void*) int_pointer2, 16);

	//
	// comment the following
	//
	free(int_pointer0);
	free(int_pointer1);
	free(int_pointer2);
}

void newAndLoseMemory(unsigned int bytes)
{
	char *memory = new char[bytes];
	for (unsigned int i = 0; i != bytes; ++i)
	{
		memory[i] = 'H';
	}
}

void repeateTest()
{
	for (unsigned int i = 0; i != 5; ++i)
	{
		newAndLoseMemory(i + 10);
	}
}

void mismatchTest_0()
{
	bool *bool_pointer0 = new bool[1];
	delete (bool_pointer0);

	int *int_pointer1 = new int();
	delete[] (int_pointer1);

	double *double0 = new double();
	free(double0);
}

void mismatchTest_1()
{
	int *int_pointer0 = (int*) malloc(4);
	delete (int_pointer0);

	int *int_pointer1 = (int*) calloc(100, 40);
	delete (int_pointer1);

	int *int_pointer2 = (int*) malloc(4);
	int_pointer2 = (int*) realloc((void*) int_pointer2, 16);
	delete (int_pointer2);
}

void releaseTest()
{
	int* zero_pointer = 0;
	free(zero_pointer);

	zero_pointer = 0;
	delete (zero_pointer);
}

void requestZeroMemory()
{
	unsigned int size = 0;

	bool *bool_pointer0 = new bool[size];
	delete[] (bool_pointer0);

	bool_pointer0 = new bool[size];
	delete[] (bool_pointer0);

	int *int_pointer0 = (int*) malloc(size);
	free(int_pointer0);

	int *int_pointer1 = (int*) calloc(size, size);
	free(int_pointer1);
}

void releaseUnallocatedData()
{
	unsigned int size = 3;

	bool *bool_pointer0 = new bool[size];
	delete[] (&bool_pointer0[1]);

	int x = 2;
	delete (&x);

}

void memoryOverwrite()
{
	unsigned int size = 4;
	char* char_pointer = new char[size];
	char_pointer[size - 1] = '\0';

	char_pointer[size] = 'k';
	delete[] (char_pointer);

}

void runTests()
{
	newTest();
	mallocTest();

	//
	// uncomment the following
	//

	//	releaseTest();
	//	requestZeroMemory();
	//	repeateTest();
	//	mismatchTest_0();
	//	mismatchTest_1();
	//	releaseUnallocatedData();
	//memoryOverwrite();
}

void stackH()
{
	runTests();
}
void stackG()
{
	stackH();
}
void stackF()
{
	stackG();
}
void stackE()
{
	stackF();
}
void stackD()
{
	stackE();
}
void stackC()
{
	stackD();
}
void stackB()
{
	stackC();
}
void stackA()
{
	stackB();
}
void beginTests()
{
	stackA();
}

int main()
{
	cout << "Application Start." << endl;
	for (unsigned int i = 0; i != 1; ++i)
	{
		beginTests();
	}
	cout << "Application End." << endl;

	return 0;
}
