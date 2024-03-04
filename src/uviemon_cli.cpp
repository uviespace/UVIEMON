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

#include <iostream> // cout & cerr
#include <iomanip>	// Used for setfill and setw (cout formatting)
#include <fstream>	// For loading a file in load()
#include <cmath>	// For std::ceil in load()


using namespace std;

//static int command_count;

#define COMMAND_COUNT 13

static command commands[COMMAND_COUNT] =  {
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

	{ "load", &load },
	{ "verify", &verify },
	{ "run", &run }
}; 


//static void register_command(command com, const char *name, void (*function)(const char*, int, char [MAX_PARAMETERS][MAX_PARAM_LENGTH]));
static DWORD parse_parameter(char *param);

std::unordered_map<int, std::string> tt_errors = {
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
	// Anything larger than 0x80 will be some other Software trap instruction (TA)
};


string _hexToString(DWORD *data, size_t size)
{
	BYTE byteArray[size * sizeof(data[0])];

	for (size_t i = 0; i < size; i++)
	{
		byteArray[i * 4] = (BYTE)((data[i] >> 24) & 0xFF);
		byteArray[i * 4 + 1] = (BYTE)((data[i] >> 16) & 0xFF);
		byteArray[i * 4 + 2] = (BYTE)((data[i] >> 8) & 0xFF);
		byteArray[i * 4 + 3] = (BYTE)(data[i] & 0xFF);
	}

	string s = "";

	for (size_t i = 0; i < sizeof(byteArray); i++)
	{
		char newChar = char(byteArray[i]);

		if (newChar >= 32 && newChar <= 126)
		{
			s += newChar;
		}
		else
		{
			s += ".";
		}
	}

	return s;
}

string _hexToString(WORD *data, size_t size)
{
	BYTE byteArray[size * sizeof(data[0])];

	for (size_t i = 0; i < size; i++)
	{
		byteArray[i * 2] = (BYTE)((data[i] >> 8) & 0xFF);
		byteArray[i * 2 + 1] = (BYTE)(data[i] & 0xFF);
	}

	string s = "";

	for (size_t i = 0; i < sizeof(byteArray); i++)
	{
		char newChar = char(byteArray[i]);

		if (newChar >= 32 && newChar <= 126)
		{
			s += newChar;
		}
		else
		{
			s += ".";
		}
	}

	return s;
}

string _hexToString(BYTE *data, size_t size)
{
	string s = "";

	for (size_t i = 0; i < size; i++)
	{
		char newChar = char(data[i]);

		if (newChar >= 32 && newChar <= 126)
		{
			s += newChar;
		}
		else
		{
			s += ".";
		}
	}

	return s;
}

/*void cleanup()
{
	free(commands);
}
 */

/*static void register_command(command com, const char *name, void (*function)(const char*, int, char [MAX_PARAMETERS][MAX_PARAM_LENGTH]))
{
	com.command_name = name;
	com.function = function;
}
*/

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
	for(int i = 0; i < COMMAND_COUNT; i++) {
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

	printf("  bdump: \t Read <length#2> BYTEs of data from memory starting at an <address#1>, saving the data to a <filePath#1>\n\n");

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
	BYTE tt = runCPU(0); // Execute on CPU Core 1

	if (tt < 0x80) // Hardware traps
	{
		cerr << " => Error: Hardware trap!" << endl;
		cerr << endl;
		cerr << "tt 0x" << hex << (unsigned int)tt << ", " << tt_errors[tt] << endl;
	}
	else if (tt == 0x80) // Successfull trap
	{
		cout << " => OK!" << endl;
	}
	else if (tt > 0x80 && tt <= 0xFF) // Software trap
	{
		cerr << " => Error: Software trap!" << endl;
		cerr << endl;
		cerr << "tt 0x" << hex << (unsigned int)tt << ", [trap_instruction]: Software trap instruction (TA)" << endl;
	}
	else // Something else that's not documented
	{
		cerr << " => Error!" << endl;
		cerr << endl;
		cerr << "tt 0x" << hex << (unsigned int)tt << ", [unknown]: unknown trap!" << endl;
	}
}

void reset(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	printf("Resetting...");
	reset(0);
	printf(" Done!\n");
}

void load(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	if (param_count != 1) {
		printf("load needs the path to the file to load.\n");
		return;
	}

	load(params[0]);
}

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
	if (param_count != 1) {
		printf("verify needs the path to the file to load.\n");
		return;
	}

	verify(params[0]);
}

void wash(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	WORD size = 16;
	// was 0x40000000
	DWORD address = ADDRESSES[get_connected_cpu_type()][SDRAM_START_ADDRESS], c = 0;

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
		param_count == 1 ? memh(param_1) : memh(param_1, param_2);
	} else if(strcmp(command, "memb") == 0) {
		param_count == 1 ? memb(param_1) : memb(param_1, param_2);
	} else if (strcmp(command, "mem") == 0) {
		param_count == 1 ? mem(param_1) : mem(param_1, param_2);
	}
}

void wmemx(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH])
{
	DWORD param_1, param_2;
	
	if (param_count != 2) {
		printf("Command %50s needs 2 parameters", command);
		return;
	}

	if ( (param_1 = strtol(params[0], NULL, 10)) == 0) {
		printf("Parameter 1 must be a positive integer");
		return;
	}

	if ( (param_2 = strtol(params[1], NULL, 10)) == 0) {
		printf("Parameter 2 must be a positive integer");
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

	ioread32(startAddr, arr, length, length > 256); // Use sequential reads

	for (DWORD i = 0; i < length; i++)
	{
		if (i % showWidth == 0)
		{
			if (i > 0)
			{
				cout << _hexToString(chars, arrayIndex);

				arrayIndex = 0;

				cout << endl;
			}

			cout << hex << nouppercase << "0x" << setfill('0') << setw(8) << startAddr << "  " << flush;
			startAddr += 16;
		}

		cout << setfill('0') << setw(8) << hex << nouppercase << arr[i] << "  " << flush;

		chars[arrayIndex] = arr[i];
		arrayIndex++;
	}

	if (arrayIndex < showWidth)
	{
		cout << _hexToString(chars, arrayIndex) << endl;
	}
	else
	{
		cout << _hexToString(chars, showWidth) << endl;
	}
}

void memh(DWORD startAddr, DWORD length)
{
	const DWORD maxAddr = startAddr + 2 * length;
	const BYTE arrSize = 8;

	WORD index = 0;
	WORD arrayIndex = 0;
	WORD arr[arrSize];

	// Loop through all the addresses and read out individual DWORDs
	for (DWORD addr = startAddr; addr < maxAddr; addr += 2)
	{
		if (index % arrSize == 0)
		{
			if (index > 0)
			{
				cout << _hexToString(arr, arrSize);
				arrayIndex = 0;

				cout << endl;
			}

			cout << hex << nouppercase << "0x" << setfill('0') << setw(8) << addr << "  " << flush;
		}

		WORD data = ioread16(addr);
		arr[arrayIndex] = data;
		arrayIndex++;

		cout << setfill('0') << setw(4) << hex << nouppercase << data << "  " << flush;

		index++;
	}

	if (index < arrSize)
	{
		cout << _hexToString(arr, arrayIndex) << endl;
	}
	else
	{
		cout << _hexToString(arr, arrSize) << endl;
	}
}

void memb(DWORD startAddr, DWORD length)
{
	const DWORD maxAddr = startAddr + 1 * length;
	const BYTE arrSize = 16;

	WORD index = 0;
	WORD arrayIndex = 0;
	BYTE arr[arrSize];

	// Loop through all the addresses and read out individual DWORDs
	for (DWORD addr = startAddr; addr < maxAddr; addr++)
	{
		if (index % arrSize == 0)
		{
			if (index > 0)
			{
				cout << _hexToString(arr, arrSize);
				arrayIndex = 0;

				cout << endl;
			}

			cout << hex << nouppercase << "0x" << setfill('0') << setw(8) << addr << "  " << flush;
		}

		WORD data = ioread8(addr);
		arr[arrayIndex] = data;
		arrayIndex++;

		cout << setfill('0') << setw(2) << hex << nouppercase << data << "  " << flush;

		index++;
	}

	if (index < arrSize)
	{
		cout << _hexToString(arr, arrayIndex) << endl;
	}
	else
	{
		cout << _hexToString(arr, arrSize) << endl;
	}
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
	DWORD data[size] = {};

	for (WORD i = 0; i < size; i++)
	{
		data[i] = c;
	}

	cout << "Writing 0x" << hex << (unsigned int)c << " to " << dec << (unsigned int)size << " DWORD(s) in memory, starting at 0x" << hex << addr << "..." << endl;

	iowrite32(addr, data, size, true);

	cout << "Wash of " << (unsigned int)size << " DWORD(s) complete!" << endl;
}

void load(string path)
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

	/*
	// Save cut-off header to an extra file to restore the full binary when downloading again
	ofstream wfile;
	wfile.open(path + "-header.tmp", ios::out | ios::binary);
	wfile.write(buffer, cutoffSize);
	wfile.close();
	*/

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
	const DWORD addr = ADDRESSES[get_connected_cpu_type()][SDRAM_START_ADDRESS];
	iowrite32(addr, writeBuffer, writeSize, true);

	cout << "Loading file complete!" << endl;
}

void verify(std::string path)
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
	const DWORD addr = ADDRESSES[get_connected_cpu_type()][SDRAM_START_ADDRESS];
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
}

