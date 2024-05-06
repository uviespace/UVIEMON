/*
	========================================
	uviemon: free(TM) replacement for grmon

	This is the main routine running when
	uviemon gets executed. It consists of
	the command line params and the console.
	========================================
*/

#include "uviemon.hpp"

#include "ftdi_device.hpp"
#include "uviemon_cli.hpp"

#include <iostream>			   // cout and cerr
#include <cstring>			   // Needed for strcmp
#include <string>			   // string for user input/output
//#include <sstream>			   // Used in console user input parsing
#include <readline/readline.h> // Unix only, needs "libreadline-dev" installed to compile!
#include <readline/history.h>  // Unix only, needs "libreadline-dev" installed to compile!
#include <stdlib.h>
#include <stdint.h>

using namespace std; // Makes my life easier

//FTDIDevice device; // Device handle for the FTDI chip

void console()
{
	char *raw_input = NULL;
	int parse_result = 0;
	
	read_history(".uviemon_history"); // Load the history file
	rl_bind_key('\t', rl_complete);	  // Tab completion for readline

	while (parse_result != -1)
	{
		
		raw_input = readline("uviemon> "); // Read input from the user

		if (!raw_input)
			break;

		if (raw_input && *raw_input)
			add_history(raw_input);

		parse_result = parse_input(raw_input);
		free(raw_input);
	}
	   
	write_history(".uviemon_history"); // Save the history file
}

void showInfo()
{
	cout << "Replacement Tool for grmon used in SMILE mission debugging." << endl;
	cout << "March 2023 and later." << endl;
	cout << endl;
	cout << "Source Code: https://github.com/NuclearPhoenixx/uviemon" << endl;
	cout << endl;

	FT_STATUS ftStatus;
	DWORD dwLibraryVer;

	// Get FTDI library version
	ftStatus = FT_GetLibraryVersion(&dwLibraryVer);
	if (ftStatus == FT_OK)
	{
		unsigned int majorVer = (dwLibraryVer >> 16) & 0xFF;
		unsigned int minorVer = (dwLibraryVer >> 8) & 0xFF;
		unsigned int buildVer = dwLibraryVer & 0xFF;

		cout << "FTDI library version: " << hex << majorVer << "." << minorVer << "." << buildVer << endl;
	}
	else
	{
		cout << "Error reading library version" << endl;
	}

	cout << "uviemon version: " << VERSION << endl;
	cout << endl;
}

void showHelp()
{
	printf("Usage:\n\n");

	printf("\t -help: \t This list of all available commands\n");
	printf("\t -info: \t Version numbers and driver info\n");
	printf("\t -list: \t List all available FTDI devices\n");
	printf("\t -cpu_tye <num>: \t 0 for LEON 3 and 1 for LEON4 autodetection used of omitted \n");
	printf("\t -jtag <num>: \t Open console with jtag device\n\n");
}

int main(int argc, char *argv[])
{
	cout << "\n  ** uviemon v" << VERSION << " **\n"
		 << endl;
	cout << "  LEON SPARC V8 Processor debugging monitor using" << endl;
	cout << "  the FTDI FT2232H chipset for communication.\n"
	<< endl;


	printf("\n  ** uviemon v%s **\n", VERSION);
	printf("  LEON SPARC V8 Processor debugging monitor using\n");
	printf("  the FTDI FT2232H chipset for communication.\n\n");

	if (argc < 2) {
		fprintf(stderr, "Need a command to work!\n\n");
		showHelp();
		return 1;
	}

	int i = 1;
	int cpu_type = -1;
	int device_index = 0;

	while(i < argc) {
		if (strcmp(argv[i], "-list") == 0) {
			get_device_list();
			return 0;
		} else if (strcmp(argv[i], "-info") == 0) {
			showInfo();
			return 0;
		} else if (strcmp(argv[i], "-help") == 0) {
			showHelp();
			return 0;
		} else if (strcmp(argv[i], "-cpu_type") == 0) {
			if ( (i + 1) >= argc) {
				fprintf(stderr, "-cpu_type requires a parameter 0 for leon3 and 1 for leon4\n");
				return 1;
			}

			// this will be removed soon anyway so no point on robust int parsing
			cpu_type = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-jtag") == 0) {
			if ( (i + 1) >= argc ) {
				fprintf(stderr, "-jtag requires device index");
				return 1;
			}

			device_index = strtol(argv[++i], NULL, 10);

			if (errno != 0) {
				fprintf(stderr, "Device number: %s could not be parsed\n", argv[i]);
				return 1;
			}

			
		} else {
			fprintf(stderr, "Uknown command '%s'\n\n", argv[i]);
			showHelp();
			return 1;
		}


		i++;
	}

	int count = get_devices_count();

	if (device_index < 0 || device_index >= count) {
		fprintf(stderr, "Device index cannot be smaller than 0 or larger than %d\n", count);
		return 1;
	}

	if (!FT_SUCCESS(ftdi_open_device(device_index, cpu_type))) {
		fprintf(stderr, "Unable to use device %d. Aborting...\n", device_index);
		return 1;
	}

	uint32_t number_jtags = get_JTAG_count();

	if (number_jtags == 0) {
		fprintf(stderr, "No devices connected on the JTAG chain! Exiting.\n");
		return 1;
	}

	if (number_jtags > 1) {
		fprintf(stderr, "More than one device found on the JTAG chain. uviemon can only interface a single GR712!\n");
		return 1;
	}

	uint32_t id = read_idcode();
	uint32_t irl = scan_IR_length();

	// Must be a 6-bit IR, otherwise something's very wrong!
	if (irl != 6) {
		fprintf(stderr, "%d-bit length, bad value!\n", irl);
		fprintf(stderr, "IR length is unequal to 6 bits. Can only work with the 6-bit GR712 IR! Exiting.\n");
		return 1;
	}



	uint32_t length1 = scan_DR_length(CODE_DATA); // Grab DR Data register length

	// Must be a 33-bit DR, cannot work otherwise, everything's hard-coded
	if (length1 != 33) {
		fprintf(stderr, "%d-bit length, bad value!\n", length1);
		fprintf(stderr, "Data register not working correctly. Need 33-bit GR712 register! Exiting.\n");
		return 1;
	}

	uint32_t length2 = scan_DR_length(CODE_ADDR_COMM); // Grab DR Command/Address register

	// Must be a 35-bit DR, cannot work otherwise, everything's hard-coded
	if (length2 != 35)  {
		fprintf(stderr, "%d-bit length, bad value!\n", length2);
		fprintf(stderr, "Address/command register not working correctly. Need 35-bit GR712 register! Exiting.");
		return 1;
	}

	printf("Number of JTAG devices on chain: %d\n", number_jtags);
	printf("Device IDCODE: %#x010x\n", id);
	printf("IR length: %d bits\n", irl);
	printf("Data register length: %#010x, %d bits\n", CODE_DATA, length1);
	printf("Command/Address register length: %#010x, %d bits\n", CODE_ADDR_COMM, length2);
	printf("OK. Ready!\n\n");
	
	console();

	ftdi_close_device();

	return 0;
}
