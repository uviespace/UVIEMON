/*
	========================================
	uviemon: free(TM) replacement for grmon

	This is the main routine running when
	uviemon gets executed. It consists of
	the command line params and the console.
	========================================
*/

#include "uviemon.h"
#include "uviemon_io.h"

#include "ftdi_device.h"
#include "uviemon_cli.h"

#include <string.h>			   // Needed for strcmp, strlcpy
#include <bsd/string.h>
#include <errno.h>
#include <readline/readline.h> // Unix only, needs "libreadline-dev" installed to compile!
#include <readline/history.h>  // Unix only, needs "libreadline-dev" installed to compile!
#include <stdlib.h>
#include <stdint.h>

// FTDIDevice device; // Device handle for the FTDI chip

static char autostart_file_buffer[512];

void console()
{
	char *raw_input = NULL;
	char *single_cmd;
	char *save_ptr = NULL;
	int parse_result = 0;

	if (*autostart_file_buffer) {
		FILE *autostart_file = fopen(autostart_file_buffer, "r");
		char buffer[512];
		int read_length;
		

		if (autostart_file) {
			while(uvie_readline(autostart_file, buffer, sizeof(buffer), &read_length)) {
				parse_result = parse_input(buffer);

				if (parse_result == -1) {
					/* The autostart file contained an "exit" command, so we will exit */
					write_history(".uviemon_history"); // Save the history file
					return;
				}
			}
		}
	}
	
	read_history(".uviemon_history"); // Load the history file
	rl_bind_key('\t', rl_complete);	  // Tab completion for readline

	while (parse_result != -1)
	{
		
		raw_input = readline("uviemon> "); // Read input from the user

		if (!raw_input)
			break;

		if (raw_input && *raw_input)
			add_history(raw_input);

		single_cmd = strtok_r(raw_input, ";", &save_ptr);
		while (single_cmd) {
			parse_result = parse_input(single_cmd);
			if (parse_result == -1)
				break;

			single_cmd = strtok_r(NULL, ";", &save_ptr);
		}

		//parse_result = parse_input(raw_input);
		free(raw_input);
	}
	   
	write_history(".uviemon_history"); // Save the history file
}

void showInfo()
{
	printf("Replacement Tool for grmon used in SMILE mission debugging.\n");
	printf("March 2023 and later.\n\n");
	printf("Source Code: https://github.com/uviespace/UVIEMON\n\n");
	
	FT_STATUS ftStatus;
	DWORD dwLibraryVer;

	// Get FTDI library version
	ftStatus = FT_GetLibraryVersion(&dwLibraryVer);
	if (ftStatus == FT_OK)
	{
		unsigned int majorVer = (dwLibraryVer >> 16) & 0xFF;
		unsigned int minorVer = (dwLibraryVer >> 8) & 0xFF;
		unsigned int buildVer = dwLibraryVer & 0xFF;

		//cout << "FTDI library version: " << hex << majorVer << "." << minorVer << "." << buildVer << endl;
		printf("FTDI library version: %x.%x.%x\n", majorVer, minorVer, buildVer);
	}
	else
	{
		printf("Error reading library version\n");
	}

	printf("uviemon version: %s\n\n", VERSION);
}

void showHelp()
{
	printf("Usage:\n\n");

	printf("\t -help: \t This list of all available commands\n");
	printf("\t -info: \t Version numbers and driver info\n");
	printf("\t -list: \t List all available FTDI devices\n");
	printf("\t -cpu_tye <num>: \t 0 for LEON 3 and 1 for LEON4 autodetection used of omitted \n");
    printf("\t -jtag <num>: \t Open console with jtag device\n\n");
    printf("\t -autostart <file>: \t Opens file and runs each command after establishing a connection\n");
}

int main(int argc, char *argv[])
{
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

        } else if (strcmp(argv[i], "-autostart") == 0) {
			if ((i + 1) >= argc) {
                fprintf(stderr, "-autostart require a file with commands");
                return 1;
			}
			
			strlcpy(autostart_file_buffer, argv[++i], sizeof(autostart_file_buffer));
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
