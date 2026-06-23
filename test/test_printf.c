#include "printf.h"

void test_printf()
{
	printf("hello1\n");
	printf("%s\n", "hello2");
	printf("%s\n", "");
	printf("%s\n", 0);
	printf("%d\n", 100);
	printf("%x\n", 16);
	printf("%o\n", 8);
	printf("%p\n", 0x8fffffff);
	printf("%p\n", 0x8fffffffL);
}