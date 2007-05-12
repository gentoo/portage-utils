#include <stdio.h>

int hello_main(int argc, char **argv) {
	printf("hi there. Somebody called us %s(%d, %p)\n", __FUNCTION__, argc, argv);
	return 0;
}
