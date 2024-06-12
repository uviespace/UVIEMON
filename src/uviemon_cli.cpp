/*
	============================================
	uviemon: free(TM) replacement for grmon

	Functions used in the command line interface
	of uviemon. All these functions corrrespond
	to their command in the console to be called
	by the user.
	============================================
*/

#include "uviemon_cli.hpp"

#include "address_map.h"
#include "uviemon_reg.h"
//#include "uviemon_opcode.h"

#include <iostream> // cout & cerr
#include <iomanip>	// Used for setfill and setw (cout formatting)
#include <fstream>	// For loading a file in load()
#include <cmath>	// For std::ceil in load()
#include <stdlib.h>
#include <unistd.h> // fork
#include <fcntl.h>  // open
#include <sys/wait.h> // wait

#include "leon3_dsu.h"

static const char *opcode_filename = "/tmp/opcode.bin";
static const char *objdump_output = "/tmp/obj_dump_out";
static const char *obj_dump_cmd[] = { "sparc-elf-objdump", "-b", "binary", "-m", "sparc", "--adjust-vma=0x40000000", "-D", "/tmp/opcode.bin", NULL };

using namespace std;

//static int command_count;

static const command commands[] =  {
	{ "help", &help },
	{ "scan", &scan },
	{ "reset", &reset },
	
	{ "mem", &memx },
    { "memh", &memx },
	{ "memb", &memx },
	
	{ "wmem", &wmemx },
	{ "wmemh", &wmemx },
	{ "wmemb", &wmemx },

	{ "bdump", &bdump },

	{ "inst", &inst },
	{ "reg", &reg },
	{ "cpu", &cpu },

	{ "wash", &washc },

	{ "load", &load },
	{ "verify", &verify },
	{ "run", &run }
}; 


static DWORD parse_parameter(char *param);

static void parse_opcode(char *buffer, uint32_t opcode, uint32_t address);
static int readline(FILE *file, char *buffer, int buffer_length, int *read_length);
static void print_register_error_msg(const char * const reg);
static void print_value_error_msg(const char * const value);
static const char * const get_tt_error_desc(uint32_t error_code);

static void hex_to_string_8(const uint8_t * const data, char *output, size_t size);
static void hex_to_string_16(const uint16_t * const data, char *output, size_t size);
static void hex_to_string_32(const uint32_t * const data, char *output, size_t size);

struct tt_error tt_errors[] = {
	{0x0, "[reset]: Power-on reset"},
	{0x2b, "[write_error]: write buffer error"},
	{0x01, "[instruction_access_error]: Error during instruction fetch"},
	{0x02, "[illegal_instruction]: UNIMP or other un-implemented instruction"},
	{0x03, "[privileged_instruction]: Execution of privileged instruction in user mode"},
	{0x04, "[fp_disabled]: FP instruction while FPU disabled"},
	{0x24, "[cp_disabled]: CP instruction while Co-processor disabled. The GR712RC does not implement a co-processor and CP instructions will trigger this trap "},
	{0x0B, "[watchpoint_detected]: Hardware breakpoint match"},
	{0x05, "[window_overflow]: SAVE into invalid window"},
	{0x06, "[window_underflow]: RESTORE into invalid window"},
	{0x20, "[register_hadrware_error]: Uncorrectable register file EDAC error"},
	{0x07, "[mem_address_not_aligned]: Memory access to un-aligned address"},
	{0x08, "[fp_exception]: FPU exception"},
	{0x09, "[data_access_exception]: Access error during load or store instruction"},
	{0x0A, "[tag_overflow]: Tagged arithmetic overflow"},
	{0x2A, "[divide_exception]: Divide by zero"},
	{0x11, "[interrupt_level_1]: Asynchronous interrupt 1"},
	{0x12, "[interrupt_level_2]: Asynchronous interrupt 2"},
	{0x13, "[interrupt_level_3]: Asynchronous interrupt 3"},
	{0x14, "[interrupt_level_4]: Asynchronous interrupt 4"},
	{0x15, "[interrupt_level_5]: Asynchronous interrupt 5"},
	{0x16, "[interrupt_level_6]: Asynchronous interrupt 6"},
	{0x17, "[interrupt_level_7]: Asynchronous interrupt 7"},
	{0x18, "[interrupt_level_8]: Asynchronous interrupt 8"},
	{0x19, "[interrupt_level_9]: Asynchronous interrupt 9"},
	{0x1A, "[interrupt_level_10]: Asynchronous interrupt 10"},
	{0x1B, "[interrupt_level_11]: Asynchronous interrupt 11"},
	{0x1C, "[interrupt_level_12]: Asynchronous interrupt 12"},
	{0x1D, "[interrupt_level_13]: Asynchronous interrupt 13"},
	{0x1E, "[interrupt_level_14]: Asynchronous interrupt 14"},
	{0x1F, "[interrupt_level_15]: Asynchronous interrupt 15"},
	{0x80, "[trap_instruction]: OK"}
	/* Anything larger than 0x80 will be some other Software trap instruction (TA) */
};


static void hex_to_string_8(const uint8_t * const data, char *output, size_t size)
{
	char value;
	size_t i;
	
	for(i = 0; i < size; i++) {
		value = (char)data[i];

		if (value >= 32 && value <= 126)
			output[i] = value;
		else
			output[i] = '.';
	}

	output[i] = '\0';
}

static void hex_to_string_16(const uint16_t * const data, char *output, size_t size)
{
	char value[2];
	size_t i;
	
	for(i = 0; i < size; i++) {
		value[0] = (char)((data[i] >> 8) & 0xFF);
		value[1] = (char)(data[i] & 0xFF);

		for(uint32_t j = 0; j < 2; j++) {
			if (value[j] >= 32 && value[j] <= 126)
				output[i * 2 + j] = value[j];
			else
				output[i * 2 + j] = '.';
		}
		
		
	}

	output[i * 2] = '\0';
}


static void hex_to_string_32(const uint32_t * const data, char *output, size_t size)
{
	char value[4];
	size_t i;
	
	for(i = 0; i < size; i++) {
		value[0] = (char)((data[i] >> 24) & 0xFF);
		value[1] = (char)((data[i] >> 16) & 0xFF);
		value[2] = (char)((data[i] >> 8) & 0xFF);
		value[3] = (char)(data[i] & 0xFF);

		for(uint32_t j = 0; j < 4; j++) {
			if (value[j] >= 32 && value[j] <= 126)
				output[i * 4 + j] = value[j];
			else
				output[i * 4 + j] = '.';
		}
		
		
	}

	output[i * 4] = '\0';
}

int parse_input(char *input)
{
	char command[MAX_PARAM_LENGTH], params[MAX_PARAMETERS][MAX_PARAM_LENGTH];

	int param_count = sscanf(input, "%49s %49s %49s %49s", command, params[0], params[1], params[2]);
	bool command_found = false;

	if (param_count == 0) {
		printf("No command was recognized.\n");
		return 0;
	}

	if (strcmp(command, "exit") == 0)
		return -1;

	/*
	 * if the number of commands ever becomes so large that a simple
	 * naive search won't be enough. This part has to be rewritten. 
	 */
	for(uint32_t i = 0; i < (sizeof(commands) / sizeof(commands[0])); i++) {
		if (strcmp(command, commands[i].command_name) == 0) {
			commands[i].function(command, param_count - 1, params);
			command_found = true;
		}
	}

	if (!command_found)
		printf("Command '%s' not recognized. Type 'help' to get a list of commands.\n", command);

	return 0;
}

/* returns 0 on failure */
static DWORD parse_parameter(char *param)
{
	int base = 10;
	errno = 0;

	if (param[0] == '0' && param[1] == 'x') {
		base = 16;
		param = param + 2;
	}
	
	DWORD result = strtol(param, NULL, base);

	if (errno != 0)
		return 0;

	return result;
}


void help(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	printf("Usage:\n");
	printf("  command <param#1> <param#2> ... <param#X>\n\n");

	printf("List of commands:\n");
	printf("  help: \t This list of all available commands\n");
	printf("  scan: \t Scan for all possible IR opcodes\n");
	printf("  reset: \t Resets CPU core 1 that also handles all 'run' calls\n\n");

	printf("  mem: \t\t Read <length#2> 32-bit DWORDs from a starting <address#1> out of the memory\n");
	printf("  memh: \t Read <length#2> 16-bit WORDs from a starting <address#1> out of the memory\n");
	printf("  memb: \t Read <length#2> 8-bit BYTEs from a starting <address#1> out of the memory\n");
	printf("  wmem: \t Write <data#2> 32-bit DWORD to a memory <address#1>\n");
	printf("  wmemh: \t Write <data#2> 16-bit WORD to a memory <address#1>\n");
	printf("  wmemb: \t Write <data#2> 8-bit BYTE to a memory <address#1>\n\n");

	printf("  bdump:\t Read <length#2> BYTEs of data from memory starting at an <address#1>, saving the data to a <filePath#1>\n\n");

	printf("  cpu:\t\t Prints cpu status or enables/disables/activates a specific cpu\n");
	printf("  inst:\t\t Prints the last <instruction_cnt#1> instruction to stdout\n");
	printf("  reg:\t\t Prints or sets registers\n\n");

	printf("  load: \t Write a file with <filePath#1> to the device memory\n");
	printf("  verify: \t Verify a file written to the device memory with <filePath#1>\n");
	printf("  run: \t\t Run an executable that has recently been uploaded to memory\n");
	printf("  wash: \t Wash memory with a certain DWORD <length#1> of hex DWORD <characters#3> starting at an <address#2>\n\n");

	printf("  exit: \t Exit uviemon\n");
}

void scan(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	/* Grab IR length, it's always (needs to be) 6 so I could skip this technically */
	BYTE irl = scan_IR_length();

	/* Debug function: Scan for the number of and all available opcodes on the device */
	scan_instruction_codes(irl);
}

void run(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	BYTE tt = runCPU(ftdi_get_active_cpu()); // Execute on CPU Core 1

	if (tt < 0x80) // Hardware traps
	{
		printf(" => Error: Hardware trap!\n\n");
		printf("tt 0x%02x, %s\n", (uint8_t)tt, get_tt_error_desc(tt));
	}
	else if (tt == 0x80) // Successfull trap
	{
		printf(" => OK!\n");	
	}
	else if (tt > 0x80 && tt <= 0xFF) // Software trap
	{
		printf(" => Error: Software trap!\n\n");
		printf("tt 0x%02x, [trap_instruction]: Software trap instruction (TA)\n", (uint8_t)tt);
	}
	else // Something else that's not documented
	{
		printf(" => Error!\n\n");
		printf("tt 0x%02x, [unknown]: unknown trap!", (uint8_t)tt);
	}
}

static const char * const get_tt_error_desc(uint32_t error_code)
{
	for (uint32_t i = 0; i < sizeof(tt_errors) / sizeof(tt_errors[0]); i++) {
		if (tt_errors[i].error_code == error_code)
			return tt_errors[i].error_desc;
	}

	return "Error code not found";
}

void reset(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	printf("Resetting...");
	reset(0);
	printf(" Done!\n");
}

void load(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	FILE *fp;
	const uint32_t cutoff_size = 64 * 1024;
	int64_t file_size, bytes_read = 0;
	size_t current_read;
	uint8_t byte_buffer[4096];
	uint32_t buffer[1024];
	uint32_t write_address = ADDRESSES[ftdi_get_connected_cpu_type()][SDRAM_START_ADDRESS];
	
	if (param_count != 1) {
		printf("load needs the path to the file to load.\n");
		return;
	}

	//load(params[0]);
	
	fp = fopen(params[0], "rb");

	if (fp == NULL) {
		fprintf(stderr, "File could not be opnend!\n");
		return;
	}

	/* check filesize */
	fseek(fp, 0L, SEEK_END);
	file_size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	if (file_size == 0) {
		fprintf(stderr, "File is empty!\n");
		return;
	}

	if (file_size < cutoff_size) {
		fprintf(stderr, "FILE size is too small! Needs to be at least 64 KiB...\n");
		return;
	}

	/* Ignore the the elf header for now */
	fseek(fp, cutoff_size, SEEK_SET);

	printf("Uploading file '%s' ...\n", params[0]);
	printf("File size: %ld B\n", file_size);

	while(!feof(fp)) {
		current_read = fread(byte_buffer, sizeof(uint8_t), 4096, fp);

		for(uint64_t i = 0; i < current_read / 4; i++) {
			buffer[i] = byte_buffer[i * 4] << 24
						| byte_buffer[i * 4 + 1] << 16
						| byte_buffer[i * 4 + 2] << 8
						| byte_buffer[i * 4 + 3];
		}
		
		iowrite32(write_address, buffer, current_read / 4, false);
		
		bytes_read += current_read;
		write_address += current_read;
		printf("Writing data to memory... %ld %%\n", (bytes_read * 100) / (file_size - cutoff_size));
	}

	fclose(fp);

	printf("Bytes read: %ld B\n", bytes_read);
	printf("Loading file complete!\n");
}


/*void load(string path)
{
	ifstream file;
	file.open(path, ios::binary | ios::ate);

	const streamsize size = file.tellg();
	const uint32_t cutoffSize = 64 * 1024; // Cut off the first 64 kiB of data (ELF-header + alignment section)

	if (size == -1)
	{
		cerr << "File not found!" << endl;
		return;
	}
	else if (size == 0)
	{
		cerr << "File is empty!" << endl;
		return;
	}
	else if (size < cutoffSize)
	{
		cerr << "File size is too small! Needs to be at least 64 kiB..." << endl;
		return;
	}

	// Create buffer for individual BYTES of binary data
	char buffer[size] = {}; 

	file.seekg(0, ios::beg);

	if (!file.read(buffer, size))
	{
		cerr << "Error loading file '" << path << "'..." << endl;
		return;
	}

	const streamsize bytesRead = file.gcount(); // Number of bytes actually read
	file.close();

	cout << "Uploading File '" << path << "'..." << endl;
	cout << "File Size: " << dec << size << " B" << endl;
	cout << "Size Read: " << dec << bytesRead << " B" << endl;
	// TODO: Check for compatability or something?
	cout << endl;

	
	// Save cut-off header to an extra file to restore the full binary when downloading again
	ofstream wfile;
	wfile.open(path + "-header.tmp", ios::out | ios::binary);
	wfile.write(buffer, cutoffSize);
	wfile.close();
	*/
/*
	// Divide to convert BYTES to DWORDS
	const unsigned int offset = cutoffSize * sizeof(BYTE) / sizeof(DWORD);

	// Divide to convert BYTES to DWORDS and subtract the offset
	const unsigned int writeSize = ceil(float(bytesRead) * sizeof(BYTE) / sizeof(DWORD)) - offset; 
	DWORD writeBuffer[writeSize];

	for (DWORD i = offset; i < writeSize + offset; i++) // Start after cutoff
	{
		const DWORD wdword = ((unsigned char)(buffer[i * 4]) << 24) |
			((unsigned char)(buffer[i * 4 + 1] & 0xFF) << 16) |
			((unsigned char)(buffer[i * 4 + 2] & 0xFF) << 8)  |
			((unsigned char)(buffer[i * 4 + 3] & 0xFF));
		writeBuffer[i - offset] = wdword;
	}

	// Begin writing to this address was 0x40000000
	const DWORD addr = ADDRESSES[ftdi_get_connected_cpu_type()][SDRAM_START_ADDRESS];
	iowrite32(addr, writeBuffer, writeSize, true);

	cout << "Loading file complete!" << endl;
}*/

void bdump(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	DWORD param_1, param_2;
	
	if (param_count != 3) {
		printf("bdump needs 3 parameters start address, length, filename.\n");
		return;
	}

	if ( (param_1 = parse_parameter(params[0])) == 0 ) {
		printf("Parameter 1 must be a positive integer.\n");
		return;
	}

	if ( (param_2 = parse_parameter(params[1])) == 0 ) {
			printf("Parameter 2 must be a positive integer.\n");
			return;
	}

	bdump(param_1, param_2, params[2]);
}

void verify(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	const uint64_t cutoff_size = 64 * 1024;

	DWORD read_address = ADDRESSES[ftdi_get_connected_cpu_type()][SDRAM_START_ADDRESS];
	FILE *fp;
	uint64_t file_size, bytes_read = 0;
	size_t current_read;
	/* byte buffer for file reading */
	uint8_t byte_buffer[4096];
	/* uint32_t buffer to upload to leon machine */
	uint32_t buffer[1024], buffer_cmp, error_found = 0;
	
	if (param_count != 1) {
		printf("verify needs the path to the file to load.\n");
		return;
	}

	fp = fopen(params[0], "rb");

	if (fp == NULL) {
		fprintf(stderr, "Error loading file!\n");
		return;
	}

	/* check filesize */
	fseek(fp, 0L, SEEK_END);
	file_size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	if (file_size == 0) {
		fprintf(stderr, "File is empty!\n");
		return;
	}

	if (file_size < cutoff_size) {
		fprintf(stderr, "FILE size is too small! Needs to be at least 64 KiB...\n");
		return;
	}

	/* Ignore the the elf header for now */
	fseek(fp, cutoff_size, SEEK_SET);

	printf("Verifying file '%s'...\n", params[0]);
	printf("File size: %ld\n", file_size);
	printf("Verifying file...\n");

	while(!feof(fp)) {
		current_read = fread(byte_buffer, sizeof(uint8_t), 4096, fp);
		ioread32(read_address, buffer, current_read / 4, false);

		for (uint64_t i = 0; i < current_read / 4; i++) {
			buffer_cmp = byte_buffer[i * 4] << 24
						 | byte_buffer[i * 4 + 1] << 16
						 | byte_buffer[i * 4 + 2] << 8
						 | byte_buffer[i * 4 + 3];
			

			if (buffer_cmp != buffer[i]) {
				printf("Verifying file... ERROR! Byte %ld incorrect!\n", bytes_read + (i * 4));
				error_found = 1;
			}
		}
		bytes_read += current_read;
		read_address += current_read;

		printf("Verifying file... %ld %%\n", bytes_read * 100 / (file_size - cutoff_size));
	}

	fclose(fp);

	if (error_found)
		printf("Verifying file... Errors found!\n");
	else
		printf("Verifying file... OK!\n");
	
}

/*void verify(std::string path)
{
	ifstream file;
	file.open(path, ios::binary | ios::ate);

	const streamsize size = file.tellg();
	const uint32_t cutoffSize = 64 * 1024; // Cut off the first 64 kiB of data (ELF-header + alignment section)

	if (size < cutoffSize)
	{
		cerr << "File size is too small! Needs to be at least 64 kiB..." << endl;
		return;
	}

	char buffer[size] = {}; // Create buffer for individual BYTES of binary data

	file.seekg(0, ios::beg);

	if (!file.read(buffer, size))
	{
		cerr << "Error loading file '" << path << "'..." << endl;
		return;
	}

	const streamsize bytesRead = file.gcount(); // Number of bytes actually read
	file.close();

	cout << "Verifying File '" << path << "'..." << endl;
	cout << "File Size: " << dec << size << " B" << endl;
	cout << "Size Read: " << dec << bytesRead << " B" << endl;
	// TODO: Check for compatability or something?
	cout << endl;

	const unsigned int offset = cutoffSize * sizeof(BYTE) / sizeof(DWORD);						  // Divide to convert BYTES to DWORDS
	const unsigned int readSize = ceil(float(bytesRead) * sizeof(BYTE) / sizeof(DWORD)) - offset; // Divide to convert BYTES to DWORDS and subtract the offset
	DWORD readBuffer[readSize];

	// Begin writing from this address was 0x40000000; 
	const DWORD addr = ADDRESSES[ftdi_get_connected_cpu_type()][SDRAM_START_ADDRESS];
	ioread32(addr, readBuffer, readSize, true);

	cout << "Verifying file... " << flush;

	for (DWORD i = offset; i < readSize; i++) // Start after cutoff
	{
		const DWORD wdword = ((unsigned char)(buffer[i * 4]) << 24) |
			((unsigned char)(buffer[i * 4 + 1] & 0xFF) << 16) |
			((unsigned char)(buffer[i * 4 + 2] & 0xFF) << 8) |
			((unsigned char)(buffer[i * 4 + 3] & 0xFF));

		if (readBuffer[i - offset] != wdword)
		{
			cerr << "\rVerifying file... ERROR! Byte " << dec << i << " incorrect." << endl;
			return;
		}
		cout << "\rVerifying file... " << dec << (unsigned int)((float)i / (float)(readSize - 1) * 100.0) << "%    " << flush;
	}

	cout << "\rVerifying file... OK!    " << endl;
}*/

void washc(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	WORD size = 16;
	// was 0x40000000
	DWORD address = ADDRESSES[ftdi_get_connected_cpu_type()][SDRAM_START_ADDRESS], c = 0;

	if (param_count > 0) {
		if ( (size = parse_parameter(params[0])) == 0 ) {
			printf("Paramter 1 size must be a positive integer.\n");
			return;
		}
	}

	if (param_count > 1) {
		if ( (address = parse_parameter(params[1])) == 0 ) {
			printf("Paramter 2 address must be a positive integer.\n");
			return;
		}
	}

	if (param_count > 2) {
		if ( (c = parse_parameter(params[2])) == 0 && errno != 0) {
			printf("Parameter 3 value must be a positive integer.\n");
			return;
		}
	}

	wash(size, address, c);
}


void memx(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	DWORD param_1, param_2 = 0;

	if (param_count < 1 || param_count > 2) {
		printf("Command %50s needs between 1 and 2 parameters", command);
		return;
	}

	errno = 0;

	if (param_count == 2)  {
		if ( (param_2 = parse_parameter(params[1])) == 0) {
			printf("Parameter 2 must be a positive integer");
			return;
		}
	}

	if ( (param_1 = parse_parameter(params[0])) == 0) {
		printf("Parameter 1 must be a positive integer");
		return;
	}

	if (strcmp(command, "memh") == 0) {
		param_count == 1 ? memh(param_1, 32) : memh(param_1, param_2);
	} else if(strcmp(command, "memb") == 0) {
		param_count == 1 ? memb(param_1, 64) : memb(param_1, param_2);
	} else if (strcmp(command, "mem") == 0) {
		param_count == 1 ? mem(param_1, 16) : mem(param_1, param_2);
	}
}

void wmemx(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	DWORD param_1, param_2;
	
	if (param_count != 2) {
		printf("Command %50s needs 2 parameters", command);
		return;
	}

	if ( (param_1 = parse_parameter(params[0])) == 0) {
		printf("Parameter 1 must be a positive integer\n");
		return;
	}

	param_2 = parse_parameter(params[1]);
	if (errno != 0) {
		printf("Parameter 2 a 32 bit int\n");
		return;
	}


	if (strcmp(command, "wmemh") == 0) {
		wmemh(param_1, param_2);
	} else if ( strcmp(command, "wmemb") == 0) {
		wmemb(param_1, param_2);
	} else if (strcmp(command, "wmem") == 0) {
		wmem(param_1, param_2);
	}
}

void inst(const char* command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	uint32_t instr_count = 11;
	uint32_t cpu = ftdi_get_active_cpu();

	if (param_count > 1) {
		printf("Inst only needs 1 parameter: the number of lines");
		return;
	}

	if (param_count == 1) {
		if ( (instr_count = strtol(params[0], NULL, 10)) == 0) {
			printf("Parameter 1 must be a positive integer");
			return;
		}
	}

	
	struct instr_trace_buffer_line *buffer = (instr_trace_buffer_line*) malloc(sizeof(instr_trace_buffer_line) * instr_count * 2);

	/* TODO: manage active cpu */
	dsu_get_instr_trace_buffer(cpu, buffer, instr_count * 2, 0);


	uint32_t i_cntr = 0;
	int i, page = 0;

	/* go back instr_count number of instructions counting multi line instructions  */
	/* there must be a better way to do this */
	while(i_cntr < instr_count) {
		for(i = instr_count * 2; i > -1; i--) {
			/* count all non multi-line instruction as unique instructions  */
			if ((buffer[i].field[0] & 0x40000000) == 0)
				i_cntr++;

			if (i_cntr == instr_count)
				break;
		}

		if (i_cntr < instr_count) {
			page++;
			dsu_get_instr_trace_buffer(cpu, buffer, instr_count * 2, page * instr_count * 2);
		}
	}

	uint32_t first_line = i;
	char operation[31];
 
	/* print header */
	printf("    %9s  %8s  %30s  %10s  %10s\n", "TIME    ", "ADDRESS ", "INSTRUCTION        ", "RESULT  ", "SYMBOL");
	while(page > -1) {
		for(uint32_t j = first_line; j < instr_count * 2; j++) {
			/* Check for non multi-line instruction second bit is zero  */
			if ((buffer[j].field[0] & 0x40000000) == 0) {
				if (j != first_line) {
					printf("]  -\n");
				}

				/* first is the time tag with first 2 bits set to zero
				 * second is program counter with last two bits set to zero
				 */
				parse_opcode(operation, buffer[j].field[3], buffer[j].field[2] & 0xFFFFFFFC); 
				printf("    %9u  %08x  %-30s",
					   buffer[j].field[0] & 0x3FFFFFFF,
					   buffer[j].field[2] & 0xFFFFFFFC,
					   operation);
				printf(" [");


				/* Check for instruction trap bit 2 is set */
				if ((buffer[j].field[2] & 0x2) == 0x2)
					printf("  TRAP  ");
				else
					printf("%08x", buffer[j].load_store_param);

			} else {
				printf(" %08x", buffer[j].load_store_param);
			}
		}

		if (--page < 0)
			break;
		
		dsu_get_instr_trace_buffer(0, buffer, instr_count * 2, page * instr_count * 2);
	}

	printf("]  -\n");


	free(buffer);
}

void reg (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	uint32_t cpu = ftdi_get_active_cpu();
	uint32_t input, reg_value;
	int base;
	char *input_str;

	union float_value reg_value_float, float_input;
	union double_value reg_value_double, double_input;

	struct register_desc desc;
	struct register_func *func_handler;

	if (param_count == 0) {
		uint32_t cwp = dsu_get_reg_psr(cpu) & 0x1F;
		register_print_summary(cpu, cwp);

	} else {
		desc = parse_register(params[0], cpu);

		/* Register could not be parsed */
		if (strcmp(desc.name, "inv") == 0) {
			print_register_error_msg(params[0]);
			return;
		}

		/*  Print register summary for the requested window */
		if (strcmp(desc.name, "w") == 0) {
			register_print_summary(cpu, desc.window);
			return;
		}

		/* Get function pointer for getting and setting requsted register */
		func_handler = get_register_functions(desc);

		if (strcmp(func_handler->name, "inv") == 0) {
			print_register_error_msg(params[0]);
			return;
		}

		/* Parse value from second parameter if available */
		if (param_count == 2) {
			input_str = params[1];
			base = 10;
			errno = 0;
			
			switch(desc.type) {
			case standard_reg:
				if (input_str[0] == '0' && input_str[1] == 'x') {
					base = 16;
					input_str = input_str + 2;
				}

				input = strtol(input_str, NULL, base);

				if (errno != 0) {
					print_value_error_msg(params[1]);
					return;
				}

				((register_func_standard*)func_handler)->set_value(desc, input);
				break;

			case float_reg:
				float_input.f = strtof(input_str, NULL);

				if (errno != 0) {
					print_value_error_msg(params[1]);
					return;
				}

				((register_func_float*)func_handler)->set_value(desc, float_input);
				break;

			case double_reg:
				double_input.d = strtod(input_str, NULL);

				if (errno != 0) {
					print_value_error_msg(params[1]);
					return;
				}

				((register_func_double*)func_handler)->set_value(desc, double_input);
				break;
				
			default:
				break;
			}
		}

		switch(desc.type) {
		case standard_reg:
			reg_value = ((register_func_standard*)func_handler)->get_value(desc);
			printf("   %3s = %d (0x%08x)\n", params[0], reg_value, reg_value);
			break;

		case float_reg:
			reg_value_float = ((register_func_float*)func_handler)->get_value(desc);
			printf("   %3s = %f (0x%08x)\n", params[0], reg_value_float.f, reg_value_float.u);
			break;

		case double_reg:
			reg_value_double = ((register_func_double*)func_handler)->get_value(desc);
			printf("   %3s = %lf (0x%016lx)\n", params[0], reg_value_double.d, reg_value_double.u);
			break;

		case none:
			print_register_error_msg(params[0]);
			break;
		}
	}

}


void cpu (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
    /* TODO: refactor to put this information in ftdi device */
	uint32_t cpu_count = ftdi_get_connected_cpu_type() == LEON3 ? 2 : 4;
	uint32_t cpu;
	
	if (param_count == 0) {

		for(uint32_t i = 0; i < cpu_count; i++) {
			printf("   cpu %d: %-8s %s\n", i,
				   dsu_get_cpu_state(i) ? "disabled" : "enabled",
				   ftdi_get_active_cpu() == i ? "active" : "");
		}
		
	} else if (param_count == 2) {
		errno = 0;

		cpu = strtol(params[1], NULL, 10);

		if (errno != 0) {
			printf("Could not parse cpu number: %s", params[1]);
			return;
		}
		
		
		if (strcmp(params[0], "enable") == 0) {
			dsu_set_cpu_wake_up(cpu);
			printf("Cpu %d enabled\n", cpu);
		} else if (strcmp(params[0], "disable") == 0)  {
			ftdi_set_cpu_idle(cpu);
			printf("Cpu %d disabled\n", cpu);
		} else if (strcmp(params[0], "active") == 0) {
			if (dsu_get_cpu_state(cpu)) {
				dsu_set_cpu_wake_up(cpu);
				printf("Cpu %d enabled\n", cpu);
			}
			
			ftdi_set_active_cpu(cpu);
			printf("Set cpu %d active\n", cpu);
		}
	}
}

static void print_register_error_msg(const char * const reg)
{
	printf("No such register %s\n", reg);
}

static void print_value_error_msg(const char * const value)
{
	printf("Could not parse value: %s\n", value);
}

static int readline(FILE *file, char *buffer, int buffer_length, int *read_length)
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

static void parse_opcode(char *buffer, uint32_t opcode, uint32_t address)
{
	/* This process will be extremely slow, due to multiple file operations */

	int fd = -1;
	char vma_buffer[OBJ_DUMP_STRING_LENGTH];
	char stdout_buffer[256];
	char *opcode_result;
	int lines = 0;
	int read_length;
	char **command;

	/* Write opcode to file for object dump  */
	FILE *opcode_file;
	FILE *stdout_file;

	opcode_file = fopen(opcode_filename, "w");
	fwrite(&opcode, sizeof(uint32_t), 1, opcode_file);
	fclose(opcode_file);

	/* Set up command with correct vma for object dump */
	sprintf(vma_buffer, "--adjust-vma=%#08x", address);
	command = (char**)malloc(sizeof(char*) * (OBJ_DUMP_PARAM_LENGTH + 1));
	for(int i = 0; i < (OBJ_DUMP_PARAM_LENGTH + 1); i++) {
		if (i == VMA_PARAM) {
			command[i] = vma_buffer;
		} else {
			command[i] = ((char**)obj_dump_cmd)[i];
		}
	}

	/* run objdump and capture output  */
	if (fork() == 0) {
		/* open file for stdout redirection */
		fd = open(objdump_output, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
		/* redirect stdout to created file */
		dup2(fd, 1);
		execvp(((char**)command)[0], (char**)command);
		close(fd);
		exit(0);
	}

	/* wait for objdump and read stdout that was piped into a file */
	wait(0);
	free(command);
	stdout_file = fopen(objdump_output, "r");

	while(lines < 7) {
		if (readline(stdout_file, stdout_buffer, 256, &read_length))
			lines++;
	}
	readline(stdout_file, stdout_buffer, 256, &read_length);
	if (read_length == 0)
		sprintf(buffer, "unknown error");

	/*
	 * format is something like this:
	 * <address>:\t<opcode in hex>\t<disassembly>
	 */
	opcode_result = strtok(stdout_buffer, "\t");
	opcode_result = strtok(NULL, "\t");
	opcode_result = strtok(NULL, "\t");

	strcpy(buffer, opcode_result);
}

void wmem(DWORD addr, DWORD data)
{
	printf("Writing to memory... ");
	iowrite32(addr, data);
	printf("OK!\n");
}

void wmemh(DWORD addr, WORD data)
{
	printf("Writing to memory... ");
	iowrite16(addr, data);
	printf("OK!\n");
}

void wmemb(DWORD addr, BYTE data)
{
	printf("Writing to memory... ");
	iowrite8(addr, data);
	printf("OK!\n");
}

void mem(DWORD startAddr, DWORD length)
{
	BYTE showWidth = 4;
	DWORD arr[length];

	DWORD chars[showWidth];
	WORD arrayIndex = 0;
	char string_output[4 * showWidth + 1];

	ioread32(startAddr, arr, length, length > 256); // Use sequential reads
	
	for (DWORD i = 0; i < length; i++) {
		if (i % showWidth == 0) {
			if (i > 0) {
				hex_to_string_32((uint32_t*)chars, string_output, arrayIndex);	
				printf("%s\n", string_output);
				//cout << _hexToString(chars, arrayIndex);

				arrayIndex = 0;

				//cout << endl;
			}

			printf("%#08x  ", startAddr);
			//cout << hex << nouppercase << "0x" << setfill('0') << setw(8) << startAddr << "  " << flush;
			startAddr += 16;
		}

		printf("%08x  ", arr[i]);
		//cout << setfill('0') << setw(8) << hex << nouppercase << arr[i] << "  " << flush;

		chars[arrayIndex++] = arr[i];
	}

	hex_to_string_32((uint32_t*)chars, string_output, (arrayIndex < showWidth) ? arrayIndex : showWidth);
	printf("%s\n", string_output);
	
	/*if (arrayIndex < showWidth) {
		hex_to_string_32((uint32_t*)chars, string_output, arrayIndex);
		cout << _hexToString(chars, arrayIndex) << endl;
	} else {
		hex_to_string_32((uint32_t*)chars, string_output, showWidth);
		cout << _hexToString(chars, showWidth) << endl;
	}*/

	
}

void memh(DWORD startAddr, DWORD length)
{
	const DWORD maxAddr = startAddr + 2 * length;
	const BYTE arrSize = 8;
	char string_output[17];

	WORD index = 0;
	WORD arrayIndex = 0;
	WORD arr[arrSize];

	// Loop through all the addresses and read out individual DWORDs
	for (DWORD addr = startAddr; addr < maxAddr; addr += 2) {
		if (index % arrSize == 0) {
			if (index > 0) {
				hex_to_string_16((uint16_t*)arr, string_output, arrSize);
				printf("%s\n", string_output);
				
				//cout << _hexToString(arr, arrSize);
				arrayIndex = 0;

				//cout << endl;
			}

			printf("%#08x  ", addr);
			//cout << hex << nouppercase << "0x" << setfill('0') << setw(8) << addr << "  " << flush;
		}

		WORD data = ioread16(addr);
		arr[arrayIndex++] = data;

		printf("%04x ", data);
		//cout << setfill('0') << setw(4) << hex << nouppercase << data << "  " << flush;

		index++;
	}

	hex_to_string_16((uint16_t*)arr, string_output, (index < arrSize) ? arrayIndex : arrSize);
	printf("%s\n", string_output);

	/*if (index < arrSize) {
		cout << _hexToString(arr, arrayIndex) << endl;
	} else {
		cout << _hexToString(arr, arrSize) << endl;
	}*/
	
}

void memb(DWORD startAddr, DWORD length)
{
	const DWORD maxAddr = startAddr + 1 * length;
	const BYTE arrSize = 16;
	char string_output[17];

	WORD index = 0;
	WORD arrayIndex = 0;
	BYTE arr[arrSize];

	// Loop through all the addresses and read out individual DWORDs
	for (DWORD addr = startAddr; addr < maxAddr; addr++) {
		if (index % arrSize == 0) {
			if (index > 0) {
				hex_to_string_8((uint8_t*) arr, string_output, arrSize);
				printf("%s\n", string_output);
				//cout << _hexToString(arr, arrSize);
				arrayIndex = 0;

				//cout << endl;
			}

			printf("%#08x  ", addr);
			//cout << hex << nouppercase << "0x" << setfill('0') << setw(8) << addr << "  " << flush;
		}

		WORD data = ioread8(addr);
		arr[arrayIndex++] = data;

		printf("%02x ", data);
		//cout << setfill('0') << setw(2) << hex << nouppercase << data << "  " << flush;

		index++;
	}

	hex_to_string_8((uint8_t*)arr, string_output, (index < arrSize) ? arrayIndex : arrSize);
	printf("%s\n", string_output);

	/*if (index < arrSize)
	{
		cout << _hexToString(arr, arrayIndex) << endl;
	}
	else
	{
		cout << _hexToString(arr, arrSize) << endl;
	}*/
}

void bdump(DWORD startAddr, DWORD length, string path)
{
	DWORD readBuffer[length];
	const DWORD charArraySize = length * sizeof(readBuffer[0]);
	char charArray[charArraySize];

	DWORD dwordLength = ceil(length / sizeof(DWORD)); // Convert byte length to DWORD length

	ioread32(startAddr, readBuffer, dwordLength, true);

	for (DWORD i = 0; i < dwordLength; i++)
	{
		DWORD temp = readBuffer[i];
		charArray[i * 4] = (char)(temp >> 24);
		charArray[i * 4 + 1] = ((char)(temp >> 16) & 0xFF);
		charArray[i * 4 + 2] = ((char)(temp >> 8) & 0xFF);
		charArray[i * 4 + 3] = ((char)(temp)&0xFF);
	}

	ofstream file;

	file.open(path, ios::out | ios::binary);
	file.write(charArray, charArraySize);
	file.close();
}

void wash(WORD size, DWORD addr, DWORD c)
{
	DWORD data[size];

	for (WORD i = 0; i < size; i++)
		data[i] = c;

	printf("Writing %#x to %d DWORD(s) in memory, starting at %#08x ...\n", c, size, addr);

	iowrite32(addr, data, size, true);

	printf("Wash of %d DWORD(s) complete!\n", size);
}





