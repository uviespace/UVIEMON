#include "uviemon_io.h"

int uvie_readline(FILE *file, char *buffer, int buffer_length, int *read_length)
{
	int i = 0;
	while(!feof(file) && i < (buffer_length - 1)) {
		fread(&buffer[i++], sizeof(char), 1, file);
		if (buffer[i-1] == '\n') {
			buffer[i-1] = '\0';
			*read_length = i - 1;
			return 1;
		}
	}
	buffer[i] = '\0';
	*read_length = i;
	return 0;
}
