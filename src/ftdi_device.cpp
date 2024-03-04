/*
	==========================================
	uviemon: free(TM) replacement for grmon

	I/O functions per (FTDI) device handle.

	Uses the standard FT2232H linux drivers to
	communicate to the processor via JTAG.

	Can be used to instantiate multiple JTAG devices
	at the same time, in the same program.
	==========================================
*/

#include "ftdi_device.hpp"

#include "address_map.h"
#include <stdio.h>
#include <string.h>
//#include <iostream>
#include <unistd.h> // Unix lib for sleep
#include <cmath>	// For std::ceil in ioread/write32()

#include "leon3_dsu.h" // Interface to the GR712 debug support unit

static ftdi_device device;

static FT_STATUS init_MPSSE_mode();
static FT_STATUS reset_JTAG_state_machine();
static void init_core_1();
static void set_other_cores_idle();


FT_STATUS open_device(DWORD device_index, int cpu_type)
{
	device.device_index = device_index;
	device.cpu_type = cpu_type;
	device.first_run = true;
	
	// Open FTDI device handle
	FT_STATUS ftStatus = FT_Open(device_index, &device.ft_handle);
	if (ftStatus != FT_OK) {
		fprintf(stderr, "Cannot open the device number %d\n", device_index);
		return ftStatus;
	}

	// Get chip driver version
	DWORD driver_version;
	ftStatus = FT_GetDriverVersion(device.ft_handle, &driver_version);

	if (ftStatus == FT_OK) {
		unsigned long majorVer = (driver_version >> 16) & 0xFF;
		unsigned long minorVer = (driver_version >> 8) & 0xFF;
		unsigned long buildVer = driver_version & 0xFF;

		printf("Device driver version: %lu.%lu.%lu", majorVer, minorVer, buildVer);
	} else {
		fprintf(stderr, "Cannot get driver version for device %d\n", device_index);
		return ftStatus;
	}

	ftStatus = init_MPSSE_mode(); // Initialize MPSSE mode on the FTDI chip and get ready for JTAG usage
	if (ftStatus != FT_OK) {
		fprintf(stderr, "Could not intialize MPSSE mode on device %d\n", device_index);
		return ftStatus;
	}

	ftStatus = reset_JTAG_state_machine();
	if (ftStatus != FT_OK) {
		fprintf(stderr, "Could not reset JTAG state machine on device %d\n", device_index);
		return ftStatus;
	}

	// Move this to another step?
	init_core_1(); // Initialize core 1 (this will run all the programs)
	//_initCore2();  // Initialize core 2 (this will be idle)
	set_other_cores_idle(); // can be the second core for leon3 or core 2,3,4 for leon4

	return ftStatus;
}

void close_device()
{
	// Reset device before closing handle, good practice
	FT_SetBitMode(device.ft_handle, 0x0, 0x00);
	FT_ResetDevice(device.ft_handle);

	// Close device
	FT_Close(device.ft_handle);
	printf("Goodbye\n");
}

int get_connected_cpu_type()
{
	return device.cpu_type;
}

DWORD get_devices_count()
{
	//FT_STATUS ftStatus;
	DWORD num_devs;

	if (!FT_SUCCESS(FT_CreateDeviceInfoList(&num_devs))) {
		fprintf(stderr, "Failed to grab number of attached devices\n");
		return 0;
	}

	return num_devs;
}

void get_device_list()
{
	DWORD num_devs = get_devices_count();

	printf("Number of devices: %d\n\n", num_devs);

	for (DWORD i = 0; i < num_devs; i++)
	{
		DWORD flags;
		DWORD type;
		DWORD id;
		DWORD locId;
		char serial_number[16];
		char description[64] = {};

		FT_STATUS ftStatus = FT_GetDeviceInfoDetail(i, &flags, &type, &id,
													&locId, serial_number, description, NULL);
		if (ftStatus == FT_OK) {

			if (strlen(description) != 0) {
				printf("%d) %s (S/N: %s | ID: %#010x)\n", i, description, serial_number, id);
			} else {
				printf("%d) -- unable to claim device --\n", i);
			}
				
		} else {
			fprintf(stderr, "Failed to get device info for device %d", i);
		}
	}

	printf("\nUse -jtag <num> to select a device\n");	
}


static FT_STATUS init_MPSSE_mode()
{
	/*
	 * Configure port for MPSSE use
	 */

	printf("Configureing port... ");

	/* Reset FTDI chip */
	FT_STATUS ft_status = FT_ResetDevice(device.ft_handle);
	if (ft_status != FT_OK) {
		fprintf(stderr, "Failed to reset device %i", device.device_index);
		FT_Close(device.ft_handle);
		return ft_status;
	}

	/* Set in an out transfer size to 16KB  */
	ft_status = FT_SetUSBParameters(device.ft_handle, 16384, 16384);
	if (ft_status != FT_OK) {
		fprintf(stderr, "Failed to set USB params on device %d\n", device.device_index);
		FT_Close(device.ft_handle);
		return ft_status;
	}

	/* Purge the RX and TX buffers */
	ft_status = FT_Purge(device.ft_handle, FT_PURGE_RX | FT_PURGE_TX);
	if (ft_status != FT_OK) {
		fprintf(stderr, "Failed to purge buffers on device %d\n", device.device_index);
		FT_Close(device.ft_handle);
		return ft_status;
	}

	/* Set read and write timeouts to 10ms  */
	ft_status = FT_SetTimeouts(device.ft_handle, 10, 10);
	if (ft_status != FT_OK) {
		fprintf(stderr, "Failed to set timeoutson device %d\n", device.device_index);
		FT_Close(device.ft_handle);
		return ft_status;
	}

	/* Enable MPSSE mode */
	ft_status = FT_SetBitMode(device.ft_handle, 0x0, FT_BITMODE_RESET);
	if (ft_status != FT_OK) {
		fprintf(stderr, "Failed to set bit mode reset on device %d\n", device.device_index);
		FT_Close(device.ft_handle);
		return ft_status;
	}

	ft_status = FT_SetBitMode(device.ft_handle, 0x0, FT_BITMODE_MPSSE);
	if (ft_status != FT_OK) {
		fprintf(stderr, "Failed to set bit mode MPSSE on device %d\n", device.device_index);
		FT_Close(device.ft_handle);
		return ft_status;
	}
	/*
	 * Wait for all the USB stuff to complete and work.
	 * THIS DELAY IS CRUCIAL! IF YOU REMOVE IT, WEIRD THINGS <WILL> HAPPEN INCLUDING RUN FAILING UNEXPECTEDLY
	 */
	sleep(1); 

	printf("Done!\n");
	printf("Configuring MPSSE... ");

	/*
	 *	===============
	 *	Configure MPSSE
	 *	===============
	 *	--> Synchronization & Bad Command Detection
	 */

	BYTE in_buf[8] = {};
	/* enable loop-back  */
	BYTE out_buf[8] = { 0x84 };
	/* length of the buffer to send */
	DWORD buf_len = 1;
	/* number of bytes actually sent (result from command) */
	DWORD len_sent = 0;
	DWORD bytes_read = 0;
	DWORD bytes_to_read = 0;

	/* enable internal loop-back */
	ft_status = FT_Write(device.ft_handle, out_buf,
						 buf_len, &len_sent);

	/* check receive buffer - it should be empty */
	ft_status |= FT_GetQueueStatusEx(device.ft_handle, &bytes_read);

	if (bytes_read != 0) {
		fprintf(stderr, "Error - MPSSE receive buffer should be empty: %d\n", ft_status);
		/* reset port to disable MPSSE */
		FT_SetBitMode(device.ft_handle, 0x0, 0x0);
		FT_Close(device.ft_handle);
		return 1;
	}

	/*
	 * Synchronize the MPSSE by sending a bogus opcode (0xAB),
	 * the MPSSE will respond with "Bad Command" (0xFA) followed by
	 * the bogus opcode itself.
	 */

	out_buf[0] = 0xAB;

	/* send the bad command */
	ft_status |= FT_Write(device.ft_handle, out_buf,
						  buf_len, &len_sent);


	do {
		ft_status |= FT_GetQueueStatus(device.ft_handle, &bytes_to_read);
	} while( bytes_to_read == 0 && ft_status == FT_OK);


	/* Read out the data from input buffer  */
	ft_status |= FT_Read(device.ft_handle, in_buf,
						 bytes_to_read, &bytes_read);

	/*
	 * johnny TODO: not sure if the loop is actually neccessary
	 * need to check
	 */
	bool command_echoed = false;
	for (DWORD i = 0; i < bytes_read; i++) {
		if (in_buf[i] == 0xFA && in_buf[i + 1] == 0xAB) {
			command_echoed = true;
			break;
		}
	}

	if (!command_echoed) {
		fprintf(stderr, "Error in synchronizing the MPSSE\n");
		FT_Close(device.ft_handle);
		return 1;
	}

	/* Disable internal loop-back */
	out_buf[0] = 0x85;
	ft_status |= FT_Write(device.ft_handle, out_buf,
						  buf_len, &len_sent);

	ft_status |= FT_GetQueueStatus(device.ft_handle, &bytes_to_read);

	if (bytes_to_read != 0) {
		fprintf(stderr, "Error - MPSSE receive buffer should be empty: %d", ft_status);
		FT_SetBitMode(device.ft_handle, 0x0, 0x0);
		FT_Close(device.ft_handle);
		return 1;
	}


	/*
	 * Configure the MPSSE settings for JTAG
	 * Multiple commands can be sent to the MPSSE with one FT_Write
	 */

	buf_len = 0;
	/* set up the Hi-speed specific commands  */
	/* Use 60MHz master clock (disable divide by 5) */
	out_buf[buf_len++] = 0x8A;
	/* Turn off adaptive clocking (my be needed for ARM) */
	out_buf[buf_len++] = 0x97;
	/* Disable three-phase clocking  */
	out_buf[buf_len++] = 0x8D;

	ft_status |= FT_Write(device.ft_handle, out_buf,
						  buf_len, &len_sent);

	/*
	 * Set TCK frequency
	 * TCK = 60MHz / ((1 + [(1 + 0xValueH*256) OR 0xValueL]) * 2)
	 */
	DWORD clock_divisor = 0x0004;
	buf_len = 0;
	/* Command to set clock divisor */
	out_buf[buf_len++] = '\x86';
	/* Set 0xValueL of clock divisor */
	out_buf[buf_len++] = clock_divisor & 0xFF;
	/* Set 0xValueH of clock divisor (should be 0) */
	out_buf[buf_len++] = (clock_divisor >> 8) & 0xFF;

	ft_status |= FT_Write(device.ft_handle, out_buf,
						  buf_len, &len_sent);
	
	
	/* Set initial states of the MPSSE interface - low byte, both pin directions and output values
	 * Pin name 	Signal 	Direction 	Config 	Initial State 	Config
	 * ADBUS0 		TCK 	output 		1 		low 			0
	 * ADBUS1 		TDI 	output 		1 		low 			0
	 * ADBUS2 		TDO 	input 		0 						0
	 * ADBUS3 		TMS 	output 		1 		high 			1
	 * ADBUS4 		GPIOL0 	input 		0 						0
	 * ADBUS5 		GPIOL1 	input 		0 						0
	 * ADBUS6 		GPIOL2 	input 		0 						0
	 * ADBUS7 		GPIOL3 	input 		0 						0
	 */

	buf_len = 0;
	/* Configure data bits low-byte of MPSSE port */
	out_buf[buf_len++] = 0x80;
	/* Initial state config above */
	out_buf[buf_len++] = 0b00001000;
	/* Direction config above */
	out_buf[buf_len++] = 0b00001011;

	/* Send of the low GPIO config commands */
	ft_status |= FT_Write(device.ft_handle, out_buf,
						  buf_len, &len_sent);

	
	/* Note that since the data in subsequent sections will be clocked on the rising edge, the
	 * inital clock state of high is selected. Clocks will be generated as high-low-high.
	 *
	 * For example, in this case, data changes on the rising edge to give it enough time
	 * to have it available at the device, which will accept data *into* the target device
	 * on the falling edge.
	 *
	 * Set initial states of the MPSSE interface
	 * - high byte, both pin directions and output values
	 * Pin name 	Signal 	Direction 	Config 	Initial State 	Config
	 * ACBUS0 		GPIOH0 	input 		0 						0
	 * ACBUS1 		GPIOH1 	input 		0 						0
	 * ACBUS2 		GPIOH2 	input 		0 						0
	 * ACBUS3 		GPIOH3 	input 		0 						0
	 * ACBUS4 		GPIOH4 	input 		0 						0
	 * ACBUS5 		GPIOH5 	input 		0 						0
	 * ACBUS6 		GPIOH6 	input 		0 						0
	 * ACBUS7 		GPIOH7 	input 		0 						0
	 */

	buf_len = 0;
	/* Configure data bits low-byte of MPSSE port */
	out_buf[buf_len++] = 0x82;
	/* Initial state config above */
	out_buf[buf_len++] = 0x00;
	/* Direction config above */
	out_buf[buf_len++] = 0x00;

	ft_status |= FT_Write(device.ft_handle, out_buf,
						  buf_len, &len_sent);

	if (ft_status != FT_OK) {
		fprintf(stderr, "Failed to config MPSSE on device %d\n", device.device_index);
		FT_Close(device.ft_handle);
		return ft_status;
	}

	printf("Done!\n");

	return ft_status;
}

static void init_core_1()
{
	/* make sure the memcfg regs are set properly
	 * XXX should be part of a board_init(), for now these are values which
	 * are guaranteed to work with the GR712RC eval board only!
	 *
	 * NOTE: this is done by probing the SRAM/SDRAM range via the BANK_SIZE
	 * setting in the MEMCFG regs; there you adjust the total size and write
	 * to one of the last few words in the bank, then shrink it by one power
	 * of 2 and repeat; when the minimum bank size is reached, do the
	 * reverse and read back the values. these should be unique, e.g.
	 * the actual address i.e. always store 0x4ffffffc to 0x4ffffffc
	 * and so on. If you read back the expected value, that is a verified
	 * upper limit of the bank; adjust up one power of 2 and repeat until
	 * you get nonsense back or the maximum bank size is reached.
	 * In other words:
	 *	1) BS = 0x200000, write 0x1fffc to last dword at 0x1fffc
	 *	2) reduce SRAM BANK SIZE config by 1
	 *	3) BS is now 0x10000, write 0xfffc to 0xfffc
	 *	4) repeat until bank size config is 0 => 8 kiB bank
	 *	5) now read back from 0x1ffc
	 *	6) if result == 0x1ffc, range is valid, increase bank size pwr by 1
	 *	7) now read back from 0x3ffc
	 *	8) if result == 0x3ffc, range is valid, increase bank size pwr by 1
	 *	   else end of physical ram was reached, decrement bank size by one
	 */
	
	uint32_t base_address = ADDRESSES[device.cpu_type][UART0_START_ADDRESS];
	
	iowrite32(base_address, 0x0003c0ff);
	iowrite32(base_address + 0x4, 0x9a20546a);
	iowrite32(base_address + 0x8, 0x0826e028);
	iowrite32(base_address + 0xc, 0x00000028);
	
	/* make sure all timers are stopped, in particular timer 4 (watchdog) */
	iowrite32(base_address + 0x318, 0x0);
	iowrite32(base_address + 0x328, 0x0);
	iowrite32(base_address + 0x338, 0x0);
	iowrite32(base_address + 0x348, 0x0);
}

static void set_other_cores_idle()
{
	/* Initialize the debug support unit for one CPU core
	 * Taken from setup.c: sxi_dpu_setup_cpu_entry()
	 */
	

	/* XXX this is needed for SXI DPU; the boot sw cannot start
	 * in SMP mode, so the second CPU is still at 0x0 initially
	 *
	 * we temporarily do this here to get ready for the EMC test
	 *
	 * we use the LEON3 debug support unit for this
	 */

	// Set trap base register to be the same as on CPU0 and point %pc and %npc there
	uint32_t tmp = dsu_get_reg_tbr(0) & ~0xfff;

	/*
	 * Set core count depending on the cpu_type
	 * this will probably need a proper function at some point
	 */
	int core_count = device.cpu_type == LEON3 ? 2 : 4;

	for (int i = 1; i < core_count; i++) {
		printf("Configuring CPU core %d idle... ", i + 1);
		
		dsu_set_noforce_debug_mode(i);
		dsu_set_cpu_break_on_iu_watchpoint(i);

		dsu_set_force_debug_on_watchpoint(i);

		dsu_set_reg_tbr(i, tmp);
		dsu_set_reg_pc(i, tmp);
		dsu_set_reg_npc(i, tmp + 0x4);

		dsu_clear_iu_reg_file(i);
		// Default invalid mask
		dsu_set_reg_wim(i, 0x2);

		// Set CWP to 7
		dsu_set_reg_psr(i, 0xf34010e1);

		dsu_clear_cpu_break_on_iu_watchpoint(i);
		// Resume cpu 1
		dsu_clear_force_debug_on_watchpoint(i);

		dsu_clear_cpu_error_mode(i);

		printf("Done!\n");
	}

	
}

static FT_STATUS reset_JTAG_state_machine()
{
	BYTE out_buf[] = {
		0x4B,        // Clock data to TMS pin (no read), clock out negative edge
		0x04,        // Number of clock pulses = Length + 1 (5 clocks here)
		0b00111111   // Bit 7 is used to hold TDI/DO before the first clk of TMS
	};

	const DWORD buf_len = 3;
	DWORD bytes_sent;

	FT_STATUS ft_status = FT_Write(device.ft_handle, out_buf,
								   buf_len, &bytes_sent);

	if (ft_status != FT_OK || buf_len != bytes_sent)
		printf("Could not reset JTAG state machine on device %d\n", device.device_index);

	return ft_status;
}


void reset(BYTE cpuID)
{
	DWORD base_address = ADDRESSES[device.cpu_type][DSU];
	
	iowrite32(base_address + 0x400024, 0x00000002); // Reset DSU ASI register
	iowrite32(base_address + 0x700000, 0x00eb800f); // Reset ASI diagnostic access

	dsu_set_reg_y(cpuID, 0x0);	  // Clear Y register
	dsu_set_reg_psr(cpuID, 0x0);  // Clear PSR register
	dsu_set_reg_wim(cpuID, 0x0);  // Clear WIM register
	dsu_set_reg_tbr(cpuID, 0x0);  // Clear TBR register
	dsu_set_reg_pc(cpuID, 0x0);	  // Clear PC register
	dsu_set_reg_npc(cpuID, 0x0);  // Clear NPC register
	dsu_set_reg_fsr(cpuID, 0x0);  // Clear FSR register
	dsu_set_reg_cpsr(cpuID, 0x0); // Clear CPSR register

	dsu_clear_iu_reg_file(cpuID); //  Clear IU register file

	// Optional: Clear FPU register file, not actually strictly needed
	dsu_clear_cpu_error_mode(cpuID); // Clear PE bit of CPU 1
}

BYTE runCPU(BYTE cpuID)
{
	reset(cpuID); // Reset CPU first in case a crash happened in a previous execution

	const uint32_t addr = ADDRESSES[device.cpu_type][SDRAM_START_ADDRESS];
 
	dsu_set_noforce_debug_mode(cpuID);
	dsu_set_cpu_break_on_iu_watchpoint(cpuID);
	dsu_set_cpu_halt_mode(cpuID);

	dsu_set_force_debug_on_watchpoint(cpuID);

	dsu_set_reg_tbr(cpuID, addr);
	dsu_set_reg_pc(cpuID, addr);
	dsu_set_reg_npc(cpuID, addr + 0x4);

	dsu_clear_iu_reg_file(cpuID);

	// Default invalid mask
	dsu_set_reg_wim(cpuID, 0x2);
	// Set CWP to 7
	dsu_set_reg_psr(cpuID, 0xf34010e1); 

	// Set to start of RAM + 8 MiB
	const uint32_t start = ADDRESSES[device.cpu_type][SDRAM_START_ADDRESS] + 8 * 1024 * 1024; 

	dsu_set_reg_sp(cpuID, 1, start);
	dsu_set_reg_fp(cpuID, 1, start);

	// CPU wake from setup.c
	dsu_set_cpu_wake_up(cpuID);
	// Not strictly needed with the iowrite down below
	dsu_clear_cpu_break_on_iu_watchpoint(cpuID);
	// Needed to resume cpu
	dsu_clear_force_debug_on_watchpoint(cpuID);
	// Not strictly needed with the iowrite down below
	dsu_clear_cpu_error_mode(cpuID);			 

	// Set TE, RE, DB, LB bits 1 and clear all other parameters on UART0
	iowrite32( ADDRESSES[device.cpu_type][UART0_START_ADDRESS] + UART0_CTRL_REG,
					 0x00000883); 

	// ACTUALLY RESUMES CPU
	iowrite32(ADDRESSES[device.cpu_type][DSU], 0x0000022f); 

	bool stopped = false;
	// Create a mask with bits 20 to 25 set to 1 (0b11111100000000000000000000) to get TCNT
	const unsigned int mask = 0x3F00000; 

	while (!stopped)
	{
		// Extract the number of data frames in the transmitter FIFO from the UART status register
		const DWORD status = ADDRESSES[device.cpu_type][UART0_START_ADDRESS] + UART0_STATUS_REG;
		unsigned int TCNT_bits = (ioread32(status) & mask) >> 20;

		if (TCNT_bits > 0)
		{
			// Grab all data from UART if available
			for (size_t i = 0; i < TCNT_bits; i++)
			{
				const DWORD fifo = ADDRESSES[device.cpu_type][UART0_START_ADDRESS] + UART0_FIFO_REG;
				// TODO: ioread32 reads a DWORD but is casted to a char, why?
				printf("%c", ioread32(fifo));
			}
		}
		else
		{
			// UART is empty, don't know if it's done or it crashed, check debug mode
			stopped = dsu_get_cpu_in_debug_mode(cpuID);
		}
	}

	// Get bits 4 to 11
	unsigned int bitmask = (1 << (11 - 4 + 1)) - 1;
	// Shift the bitmask to align with the start position
	bitmask <<= 4;
	// Use bitwise AND to extract the desired bits
	unsigned int tt = dsu_get_reg_trap(cpuID) & bitmask;
	// Shift the result back to the rightmost position
	tt >>= 4;											 

	// Use bitwise AND to extract the desired bits
	unsigned int tbr_tt = dsu_get_reg_tbr(cpuID) & bitmask;
	// Shift the result back to the rightmost position
	tbr_tt >>= 4;											

	// Sometimes fixes an issue that can throw an error on first run
	if (device.first_run && (tt != 0x80 || tbr_tt != 0x80))
	{
		device.first_run = false;
		// Just run it again and it'll probably work
		return runCPU(cpuID); 
	}

	if (tt == 0x80 && tbr_tt != 0x80)
	{
		return tbr_tt;
	}

	/* Actually not needed because we're monitoring both DSU reg and TBR reg
	if (dsu_get_global_reg(cpuID, 1) != 1 && tt == 0x80) // Check if global failed when tt is OK
	{
		return 0x82; // Return a Software trap instruction
	}
	*/

	return tt;
}

BYTE get_JTAG_count()
{
	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return 0;

	DWORD bytes_sent;
	
	/* GOTO shift-ir */
	BYTE out_buf[100] = {
		0x4B,       // Clock out TMS without read
		0x05,       // Number of clock pulses = Length + 1 (6 bits here)
		0b00001101, // Data is shifted LSB first, so actually 101100
		/* Write one byte of OxFF, but leave immediately to Exit-IR on this last byte with TMS */
		0x1B,       // Clock bits out with read
		0x06,       // Length + 1 (7 bits here), only 7 because the las one will be clocked out by the next TMS command 
		0xFF,       // Ones only
		/* Clock out last bit of the 0xFF byte and leave Exit-IR */
		0x4B,       // Clock bits out to TMS with read, move out of Shift-IR and clock out the last TDI/DO bit 
		0x00,       // Length + 1 (1 bit here)
		0b10000011, // One only
		/* We are in Exit1-IR, go to Shift-DR */
		0x4B,       // Clock out TMS without read
		0x03,      // Number of clock pulses = Length + 1 (4 bits here)
		0b00000011,// Data is shifted LSB first
	};

	DWORD buf_len = 12;

	
	for (int i = 0; i < 10; i++) {
		out_buf[buf_len++] = 0x1B; // Clock data bits out with read
		out_buf[buf_len++] = 0x07; // Length + 1 (8 bits here)
		out_buf[buf_len++] = 0x00; // Zeros only
	}

	out_buf[buf_len++] = 0x2A; // Clock bits in by reading
	out_buf[buf_len++] = 0x07; // Length + 1 (8 bits here);

	FT_STATUS ft_status = FT_Write(device.ft_handle, out_buf,
								   buf_len, &bytes_sent);


	if (ft_status != FT_OK && bytes_sent != 42) {
		fprintf(stderr, "Communication error with JTAG device!\n");
		return 0;
	}
	

	DWORD bytes_to_read;
	DWORD bytes_read;
	BYTE in_buf[100];
	// Do a read to flush the read buffer, the data is not needed...
	do {
		ft_status = FT_GetQueueStatus(device.ft_handle, &bytes_to_read);					   // Get the number of bytes in the device input buffer
	} while ((bytes_to_read == 0) && (ft_status == FT_OK)); // or Timeout

	/* Read out the data from input buffer */
	ft_status |= FT_Read(device.ft_handle, &in_buf,
						 bytes_to_read, &bytes_read); 

	BYTE numberOfJTAGs = 0;
	buf_len = 0;

	// Now clock out 100 1s max with read until we receive one back
	for (int i = 0; i < 100; i++) {
		/* Clock bits out with read */
		out_buf[buf_len++] = 0x3B;
		/* Length + 1 (1 bit here) */
		out_buf[buf_len++] = 0x00;
		/* Ones only */
		out_buf[buf_len++] = 0xFF;

		ft_status |= FT_Write(device.ft_handle, out_buf,
							 buf_len, &bytes_sent); 
		buf_len = 0;

		if (ft_status != FT_OK || bytes_sent != 3) {
			fprintf(stderr, "Error while scanning for number of JTAG devices with device %d", device.device_index);
			return 0;
		}

		do {
			ft_status = FT_GetQueueStatus(device.ft_handle, &bytes_to_read);					   // Get the number of bytes in the device input buffer
		} while ((bytes_to_read == 0) && (ft_status == FT_OK));

		ft_status |= FT_Read(device.ft_handle, &in_buf,
							 bytes_to_read, &bytes_read);

		if (ft_status != FT_OK || bytes_read == 0) {
			fprintf(stderr, "Error while reading number of JTAG devices with device  %d\n", device.device_index);
				
			return 0;
		}

		if (in_buf[0] != 0x00) {
			numberOfJTAGs = i;
			break;
		}
	}

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return 0;

	return numberOfJTAGs;
}

DWORD read_idcode()
{
	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return 0;


	BYTE out_buf[] = {
		/* Goto Shift-DR */
		0x4B,       // Clock out TMS without read
		0x04,       // Number of clock pulses = Length + 1 (5 bits here)
		0b00000101, // Data is shifted LSB first, so actually 10100
		/* Clocl out read command */
		0x28,       // Read Bytes back
		0x03,       // 3 + 1 Bytes = 32 bit IDCODe
		0x00
	};

	DWORD bytes_sent;

	FT_STATUS ft_status = FT_Write(device.ft_handle, out_buf,
								   6, &bytes_sent);

	if (ft_status != FT_OK || bytes_sent != 6) {
		fprintf(stderr, "Error while querying ID for device %d\n", device.device_index);
		return 0;
	}

	DWORD bytes_to_read, bytes_read;
	BYTE in_buf[10];
	
	do {
		ft_status = FT_GetQueueStatus(device.ft_handle, &bytes_to_read);					   // Get the number of bytes in the device input buffer
	} while ((bytes_to_read == 0) && (ft_status == FT_OK));						   // or Timeout

	// Read out the data from input buffer
	ft_status |= FT_Read(device.ft_handle, &in_buf,
						 bytes_to_read, &bytes_read); 

	if (ft_status != FT_OK) {
		fprintf(stderr, "Error while reading ID for device %d\n",
				device.device_index);
		return 0;
	}

	if (bytes_read != 4) {
		fprintf(stderr, "Device did not return the correct number of bytes for IDCODE! \n");
		return 0;
	}

	// Some formatting to get the right order of bytes for IDCODE
	DWORD id = (DWORD)((unsigned char)(in_buf[3]) << 24 |
					   (unsigned char)(in_buf[2]) << 16 |
					   (unsigned char)(in_buf[1]) << 8  |
					   (unsigned char)(in_buf[0]));

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return 0;

	return id;
}




BYTE scan_IR_length()
{
	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR first
		return 0;

	BYTE out_buf[10] = {
		/* Goto Shift-IR */
		0x4B,      // Clock out TMS without read
		0x05,      // Number of clock pulses = Length + 1 (6 clocks here)
		0b00001101,// Data is shifted LSB first, so the TMS pattern is 101100
		/* Flush IR with a byte of zeros, more than enough for 6 bit IR */
		0x1B,      // Clock bits out with read
		0x07,      // Length + 1 (8 bits here)
		0x00,      // Zeros only
		/*  Clock out Read: FIXES SOME ISSUES; I DONT KNOW WHY?! */
		0x2A,      // Clock bits out with read
		0x07       // Length + 1 (8 bits here)
	};

	DWORD bytes_sent;

	FT_STATUS ft_status = FT_Write(device.ft_handle, out_buf,
								   8, &bytes_sent);
	
	if (ft_status != FT_OK || bytes_sent != 8) {
		fprintf(stderr, "Communication error with JTAG device!\n");
		return 0;
	}

	DWORD bytes_to_read, bytes_read;
	BYTE in_buf[10];

	// Do a read to flush the read buffer, the data is not needed...
	do {
		// Get the number of bytes in the device input buffer
		ft_status |= FT_GetQueueStatus(device.ft_handle, &bytes_to_read);				   
	} while ((bytes_to_read == 0) && (ft_status == FT_OK));						   // or Timeout

	// Read out the data from input buffer
	ft_status |= FT_Read(device.ft_handle, &in_buf,
						 bytes_to_read, &bytes_read); 

	BYTE lengthIR = 0;
	DWORD buf_len = 0;

	// Clock out 100 bits of 1s max with read back enabled, scan until a 1 gets back
	for (BYTE i = 0; i < 100; i++) {
		/* Clock bits out with read */
		out_buf[buf_len++] = 0x3B;
		/* Length + 1 (1 bit here) */
		out_buf[buf_len++] = 0x00;
		/* Ones only */
		out_buf[buf_len++] = 0xFF;											

		// Send off the TMS command
		ft_status |= FT_Write(device.ft_handle, out_buf,
							  buf_len, &bytes_sent); 

		// Check if everything has been sent!
		if (ft_status != FT_OK || bytes_sent != buf_len)  {
			fprintf(stderr, "Error in IR length scan for device %d\n",
					device.device_index);
			return 0;
		}

		buf_len = 0;

		do {
			// Get the number of bytes in the device input buffer
			ft_status = FT_GetQueueStatus(device.ft_handle, &bytes_to_read);					   
		} while ((bytes_to_read == 0) && (ft_status == FT_OK));

		// Read out the data from input buffer
		ft_status |= FT_Read(device.ft_handle, &in_buf,
							 bytes_to_read, &bytes_read); 

		if (ft_status != FT_OK || bytes_read != bytes_to_read) {
			fprintf(stderr, "Error while reading length of IR with device %d\n",
					device.device_index);
			return 0;
		}

		// A 1 has finally come through, this is the limit, abort.
		if (in_buf[0] != 0x00) {
			lengthIR = i;
			break;
		}
	}

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return 0;


	return lengthIR; // Exit with success
}

void scan_instruction_codes(BYTE bitLengthIR)
{
	printf("Scanning for IR opcodes that return a non-zero DR length. This might take a while...\n");

	BYTE maxIRLength = 0; // Get the highest opcode possible for the IR length

	for (BYTE i = 0; i < bitLengthIR; i++) {
		maxIRLength |= 1 << i;
	}

	unsigned int num_instructions = 0;

	for (BYTE i = 0b00000000; i <= maxIRLength; i++) {
		unsigned int length = scan_DR_length(i); // Grab individual DR Data register length
		if (length != 0)
		{
			//cout << "- DR length for address 0x" << hex << uppercase << (unsigned int)i << ": " << dec << length << " bit" << endl;
			printf("- DR length for address %#010x: %d bit", (unsigned int) i, length);
			num_instructions ++;
		}
	}

	printf("Scan complete! Found %d instructions.", num_instructions);
}

BYTE scan_DR_length(BYTE opcode)
{
	BYTE byOutputBuffer[100];	// Buffer to hold MPSSE commands and data to be sent to the FT2232H
	BYTE byInputBuffer[100];	// Buffer to hold data read from the FT2232H
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0;	// Count of actual bytes sent - used with FT_Write
	DWORD dwNumBytesToRead = 0; // Number of bytes available to read in the driver's input buffer
	DWORD dwNumBytesRead = 0;	// Count of actual bytes read - used with FT_Read

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return 0;

	// Goto Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05;		 // Number of clock pulses = Length + 1 (6 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00001101; // Data is shifted LSB

	// Clock out Command/Address register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;	 // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;	 // Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = opcode; // First 5 bits of the opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;				// Clock bits out to TMS, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;				// Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (opcode << 2) | 1; // Last bit of the opcode and leave Shift-IR

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// TMS is currently low.
	// State machine is in Shift-DR, so now use the TDI/TDO command to shift 0's out TDI/DO while reading TDO/DI
	// Clock out 10 x 8 bits of 0s only to clear any other values
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x09; // Length + 1 (10 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	for (BYTE i = 0; i < 10; i++)
	{
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Zeros only
	}
	// Clock out Read: FIXES SOME ISSUES; I DONT KNOW WHY?!
	byOutputBuffer[dwNumBytesToSend++] = 0x2A;													 // Read back bits
	byOutputBuffer[dwNumBytesToSend++] = 0x07;													 // Length + 1 (8 bits here)

	FT_STATUS ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
								  dwNumBytesToSend, &dwNumBytesSent);

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend)
	{
		fprintf(stderr, "Communication error with JTAG device!\n");
		return 0;
	}
	
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Do a read to flush the read buffer, the data is not needed...
	do {
		// Get the number of bytes in the device input buffer
		ftStatus = FT_GetQueueStatus(device.ft_handle, &dwNumBytesToRead);					   
	} while ((dwNumBytesToRead == 0) && (ftStatus == FT_OK));						   // or Timeout

	ftStatus |= FT_Read(device.ft_handle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead);

	BYTE lengthDR = 0;

	// Clock out 100 bits of 1s with read back enabled until a 1 comes back out
	for (BYTE i = 0; i < 100; i++) {
		byOutputBuffer[dwNumBytesToSend++] = 0x3B;											// Clock bits out with read
		byOutputBuffer[dwNumBytesToSend++] = 0x00;											// Length + 1 (1 bit here)
		byOutputBuffer[dwNumBytesToSend++] = 0xFF;											// Ones only

		ftStatus |= FT_Write(device.ft_handle, byOutputBuffer,
							 dwNumBytesToSend, &dwNumBytesSent);

		if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
			fprintf(stderr, "Error in DR length scan for device %d (opcode %#10x)\n", device.device_index, CODE_DATA);
			return 0;
		}

		dwNumBytesToSend = 0; // Reset output buffer pointer

		do {
			// Get the number of bytes in the device input buffer
			ftStatus = FT_GetQueueStatus(device.ft_handle, &dwNumBytesToRead);					   
		} while ((dwNumBytesToRead == 0) && (ftStatus == FT_OK));						   // or Timeout

		ftStatus |= FT_Read(device.ft_handle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead); 

		if (ftStatus != FT_OK || dwNumBytesRead != dwNumBytesToRead) {
			fprintf(stderr, "Error while reading length of DR with device %d (opcode %#10x)\n", device.device_index, CODE_DATA);
			return 0;
		}

		// Received a one back
		if (byInputBuffer[0] != 0x00) {
			lengthDR = i;
			break;
		}
	}

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return 0;

	return lengthDR; // Exit with success
}

BYTE ioread8(DWORD addr)
{
	DWORD bigData = ioread32(addr);

	BYTE byte0 = (bigData & 0xFF);		   // First Byte of DWORD data
	BYTE byte1 = ((bigData >> 8) & 0xFF);  // Second Byte of DWORD data
	BYTE byte2 = ((bigData >> 16) & 0xFF); // Third Byte of DWORD data
	BYTE byte3 = ((bigData >> 24) & 0xFF); // Last Byte of DWORD data

	BYTE data;
	BYTE lsb = addr & 0xFF;

	if (lsb % 4 == 0) // First byte of a 4-byte DWORD data block
	{
		data = byte3;
	}
	else if ((lsb - 1) % 4 == 0) // Second byte of a 4-byte DWORD data block
	{
		data = byte2;
	}
	else if ((lsb - 2) % 4 == 0) // Third byte of a 4-byte DWORD data block
	{
		data = byte1;
	}
	else // Fourth byte of a 4-byte DWORD data block
	{
		data = byte0;
	}

	return data;
}

WORD ioread16(DWORD addr)
{
	DWORD bigData = ioread32(addr);

	WORD byte0 = (bigData & 0xFFFF);		 // First WORD of DWORD data
	WORD byte1 = ((bigData >> 16) & 0xFFFF); // Second WORD of DWORD data

	WORD data;
	BYTE lsb = addr & 0xFF;

	if (lsb % 4 == 0 || (lsb - 1) % 4 == 0) // First WORD of a 4-byte DWORD data block
	{
		data = byte1;
	}
	else // Second WORD of a 4-byte DWORD data block
	{
		data = byte0;
	}

	return data;
}

DWORD ioread32(DWORD addr)
{
	BYTE byOutputBuffer[100];	// Buffer to hold MPSSE commands and data to be sent to the FT2232H
	BYTE byInputBuffer[100];	// Buffer to hold data read from the FT2232H
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0;	// Count of actual bytes sent - used with FT_Write
	DWORD dwNumBytesToRead = 0; // Number of bytes available to read in the driver's input buffer
	DWORD dwNumBytesRead = 0;	// Count of actual bytes read - used with FT_Read

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
	{
		return 0;
	}

	// Goto Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05;		 // Number of clock pulses = Length + 1 (6 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00001101; // Data is shifted LSB first, so actually 101100

	// Clock out Command/Address register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;			 // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;			 // Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_ADDR_COMM; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;						// Clock bits out to TMS without read, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;						// Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_ADDR_COMM << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// Clock out 6 x 8 bits of 0s only to clear out any other values
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05; // Length + 1 (6 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	for (BYTE i = 0; i < 6; i++)
	{
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Zeros only
	}

	/*
	// Clock out Read: FIXES SOME ISSUES; I DONT KNOW WHY?!
	byOutputBuffer[dwNumBytesToSend++] = 0x2A;													 // Read back bits
	byOutputBuffer[dwNumBytesToSend++] = 0x07;													 // Length + 1 (8 bits here)
	FT_STATUS ftStatus = FT_Write(_ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend)
	{
		cerr << "Communication error with JTAG device!" << endl;
		return 0;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Do a read to flush the read buffer, the data is not needed...
	do
	{
		ftStatus = FT_GetQueueStatus(_ftHandle, &dwNumBytesToRead);					   // Get the number of bytes in the device input buffer
	} while ((dwNumBytesToRead == 0) && (ftStatus == FT_OK));						   // or Timeout
	ftStatus |= FT_Read(_ftHandle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead); // Read out the data from input buffer
	*/

	// Clock out come clock cycles: FIXES SOME ISSUES WITH READING; I DONT KNOW WHY?!
	// Much faster way of doing the above with the added (very low) risk of producing garbage output apparently?
	byOutputBuffer[dwNumBytesToSend++] = 0x8E; // Clock output
	byOutputBuffer[dwNumBytesToSend++] = 0x07; // Length + 1 (8 bits here)

	// Shift out AHB address DWORD (4 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byOutputBuffer[dwNumBytesToSend++] = (addr & 0xFF);			// First Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 8) & 0xFF);	// Second Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 16) & 0xFF); // Third Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 24) & 0xFF); // Last Byte of DWORD address

	// Shift out 2-bit AHB transfer size: 10 for WORD
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;	   // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x01;	   // Length + 1 (2 bits here)
	byOutputBuffer[dwNumBytesToSend++] = RW_DWORD; // Read 32-bit DWORD

	// Shift out 1-bit Read/Write Instruction: 0x0 for READ while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;		 // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000001; // Ones only

	// Go to Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b0000111; // 11100

	// Clock out Data register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_DATA; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;				   // Clock bits out to TMS, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_DATA << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// Clock out read command
	byOutputBuffer[dwNumBytesToSend++] = 0x28; // Read Bytes
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // 3 + 1 Bytes = 32 bit AHB Data -> Does not read SEQ Bit!
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	FT_STATUS ftStatus = FT_Write(device.ft_handle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Communication error with JTAG device %d!\n", device.device_index);
		return 0;
	}

	dwNumBytesToSend = 0; // Reset output buffer pointer

	do {
		// Get the number of bytes in the device input buffer
		ftStatus = FT_GetQueueStatus(device.ft_handle, &dwNumBytesToRead);					   
	} while ((dwNumBytesToRead == 0) && (ftStatus == FT_OK));						   // or Timeout

	// Read out the data from input buffer
	ftStatus |= FT_Read(device.ft_handle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead); 

	if (ftStatus != FT_OK) {
		fprintf(stderr, "Error while reading data register for device %d\n", device.device_index);
		return 0;
	}

	if (dwNumBytesRead != dwNumBytesToRead) {
		fprintf(stderr, "Bytes read: %d\n", dwNumBytesRead);
		fprintf(stderr, "Device did not return the correct number of bytes for the data register.\n");
		return 0;
	}

	// Format data
	return (DWORD)((unsigned char)(byInputBuffer[3]) << 24
				   | (unsigned char)(byInputBuffer[2]) << 16
				   | (unsigned char)(byInputBuffer[1]) << 8
				   | (unsigned char)(byInputBuffer[0]));
}

void ioread32raw(DWORD startAddr, DWORD *data, WORD size)
{

	// Check 1kB boundary for SEQ transfers
	if (size > 256)
		fprintf(stderr, "Warning: Size is bigger than recommended 1 kB maximum (GR712RC-UM)!");

	BYTE byOutputBuffer[100];	// Buffer to hold MPSSE commands and data to be sent to the FT2232H
	BYTE byInputBuffer[100];	// Buffer to hold data read from the FT2232H
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0;	// Count of actual bytes sent - used with FT_Write
	DWORD dwNumBytesToRead = 0; // Number of bytes available to read in the driver's input buffer
	DWORD dwNumBytesRead = 0;	// Count of actual bytes read - used with FT_Read

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
	{
		return;
	}

	// Goto Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05;		 // Number of clock pulses = Length + 1 (6 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00001101; // Data is shifted LSB first, so actually 101100

	// Clock out Command/Address register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;			 // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;			 // Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_ADDR_COMM; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;						// Clock bits out to TMS without read, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;						// Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_ADDR_COMM << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// Clock out 6 x 8 bits of 0s only to clear out any other values
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05; // Length + 1 (6 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;
	for (BYTE i = 0; i < 6; i++)
	{
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Zeros only
	}

	/*
	// Clock out Read: FIXES SOME ISSUES; I DONT KNOW WHY?!
	byOutputBuffer[dwNumBytesToSend++] = 0x2A;													 // Read back bits
	byOutputBuffer[dwNumBytesToSend++] = 0x07;													 // Length + 1 (8 bits here)
	FT_STATUS ftStatus = FT_Write(_ftHandle, byOutputBuffer, dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend)
	{
		cerr << "Communication error with JTAG device!" << endl;
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Do a read to flush the read buffer, the data is not needed...
	do
	{
		ftStatus = FT_GetQueueStatus(_ftHandle, &dwNumBytesToRead);					   // Get the number of bytes in the device input buffer
	} while ((dwNumBytesToRead == 0) && (ftStatus == FT_OK));						   // or Timeout
	ftStatus |= FT_Read(_ftHandle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead); // Read out the data from input buffer
	*/

	// Clock out come clock cycles: FIXES SOME ISSUES WITH READING; I DONT KNOW WHY?!
	// Much faster way of doing the above with the added (very low) risk of producing garbage output apparently?
	byOutputBuffer[dwNumBytesToSend++] = 0x8E; // Clock output
	byOutputBuffer[dwNumBytesToSend++] = 0x07; // Length + 1 (8 bits here)

	// Shift out AHB address DWORD (4 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byOutputBuffer[dwNumBytesToSend++] = (startAddr & 0xFF);		 // First Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((startAddr >> 8) & 0xFF);	 // Second Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((startAddr >> 16) & 0xFF); // Third Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((startAddr >> 24) & 0xFF); // Last Byte of DWORD address

	// Shift out 2-bit AHB transfer size: 10 for WORD
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;	   // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x01;	   // Length + 1 (2 bits here)
	byOutputBuffer[dwNumBytesToSend++] = RW_DWORD; // Read 32-bit DWORD

	// Shift out 1-bit Read/Write Instruction: 0x0 for READ while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;		 // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000001; // Ones only

	// Go to Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b0000111; // 11100

	// Clock out Data register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_DATA; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;				   // Clock bits out to TMS, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_DATA << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;													 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;													 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011;											 // Data is shifted LSB first, so the TMS pattern is 1100
	FT_STATUS ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
								  dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Communication error with JTAG device %d!\n", device.device_index);
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	for (WORD i = 0; i < size; i++) {
		// Clock out read command
		byOutputBuffer[dwNumBytesToSend++] = 0x28; // Read Bytes
		byOutputBuffer[dwNumBytesToSend++] = 0x03; // 3 + 1 Bytes = 32 bit AHB Data -> Does not read SEQ Bit!
		byOutputBuffer[dwNumBytesToSend++] = 0x00;

		// Send off the TMS command
		ftStatus |= FT_Write(device.ft_handle, byOutputBuffer,
							 dwNumBytesToSend, &dwNumBytesSent); 

		if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
			fprintf(stderr, "Error while shifting out read instruction for device %d\n", device.device_index);
			break;
		}

		dwNumBytesToSend = 0; // Reset output buffer pointer

		do {
			// Get the number of bytes in the device input buffer
			ftStatus = FT_GetQueueStatus(device.ft_handle, &dwNumBytesToRead);					   
		} while ((dwNumBytesToRead == 0) && (ftStatus == FT_OK));						   // or Timeout

		// Read out the data from input buffer
		ftStatus |= FT_Read(device.ft_handle, &byInputBuffer, dwNumBytesToRead, &dwNumBytesRead); 

		if (ftStatus != FT_OK) {
			fprintf(stderr, "Error while reading data register for device %d\n", device.device_index);
			break;
		}

		if (dwNumBytesRead != dwNumBytesToRead) {
			fprintf(stderr, "Bytes read: %d\n", dwNumBytesRead);
			fprintf(stderr, "Device did not return the correct number of bytes for the data register.\n");
			break;
		}

		// Format data
		DWORD newData = (DWORD)((unsigned char)(byInputBuffer[3]) << 24 |
								(unsigned char)(byInputBuffer[2]) << 16 |
								(unsigned char)(byInputBuffer[1]) << 8 |
								(unsigned char)(byInputBuffer[0]));
		data[i] = newData;

		// Loop around once through Update-DR and then go back to Shift-DR
		byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock bits out without read
		byOutputBuffer[dwNumBytesToSend++] = 0x04;										   // Length + 1 (4 bits here)
		byOutputBuffer[dwNumBytesToSend++] = 0b10000111;								   // 11100

		// Send off the TMS command
		ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
							dwNumBytesToSend, &dwNumBytesSent); 

		if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
			fprintf(stderr, "Communication error with JTAG device!\n");
			break;
		}

		dwNumBytesToSend = 0; // Reset output buffer pointer
	}
}

void ioread32(DWORD startAddr, DWORD *data, WORD size, bool progress)
{
	if (progress) // Optional terminal progress output
		printf("Reading data from memory... \n");

	WORD readChunks = ceil((float)size / 256.0); // How many individual read chunks are needed

	DWORD tempData[256]; // Allocate the tempData array outside the loop with max size

	for (WORD i = 0; i < readChunks; i++) {
		if (progress) // Optional terminal progress output
			printf("Reading data from memory... %d %%", (unsigned int)((i * 100.0) / (readChunks - 1.0)));

		DWORD addr = startAddr + 1024 * i;
		WORD readSize; // Number of DWORDS to read

		if (i == readChunks - 1 && size % 256 != 0) // Last read is not a full 1024B chunk
			readSize = size % 256; // Remainder
		else
			readSize = 256;

		ioread32raw(addr, tempData, readSize);

		for (WORD j = 0; j < readSize; j++)
			data[i * 256 + j] = tempData[j];
	}

	if (progress) // Optional terminal progress output
		printf("Reading data from memory... Complete!   \n");
}

void iowrite8(DWORD addr, BYTE data)
{
	BYTE byOutputBuffer[100];	// Buffer to hold MPSSE commands and data to be sent to the FT2232H
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0;	// Count of actual bytes sent - used with FT_Write

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return;

	// Goto Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05;		 // Number of clock pulses = Length + 1 (6 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00001101; // Data is shifted LSB first, so actually 101100

	// Clock out Command/Address register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;			 // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;			 // Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_ADDR_COMM; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;						// Clock bits out to TMS without read, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;						// Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_ADDR_COMM << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// Shift out AHB address DWORD (4 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byOutputBuffer[dwNumBytesToSend++] = (addr & 0xFF);			// First Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 8) & 0xFF);	// Second Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 16) & 0xFF); // Third Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 24) & 0xFF); // Last Byte of DWORD address

	// Shift out 2-bit AHB transfer size: 10 for WORD
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;	  // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x01;	  // Length + 1 (2 bits here)
	byOutputBuffer[dwNumBytesToSend++] = RW_BYTE; // Write 8-bit BYTE

	// Shift out 1-bit Read/Write Instruction: 0x1 for WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;													 // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;													 // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b10000001;											 // Ones only
	FT_STATUS ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
								  dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Error while shifting out WRITE command for device %d\n", device.device_index);
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Go to Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b0000111; // 11100

	// Clock out Data register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_DATA; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;				   // Clock bits out to TMS, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_DATA << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;										   // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011;								   // Data is shifted LSB first, so the TMS pattern is 1100
	ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
						dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		printf("Communication error with JTAG device!\n");
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Shift out AHB data DWORD (1 byte)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	BYTE lsb = addr & 0xFF;

	if (lsb % 4 == 0) // First byte of a 4-byte DWORD data block
	{
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // data; // Only byte of transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = data; // Dummy byte for transmission data
	}
	else if ((lsb - 1) % 4 == 0) // Second byte of a 4-byte DWORD data block
	{
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // data; // Only byte of transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = data; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
	}
	else if ((lsb - 2) % 4 == 0) // Third byte of a 4-byte DWORD data block
	{
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // data; // Only byte of transmission data
		byOutputBuffer[dwNumBytesToSend++] = data; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
	}
	else // Fourth byte of a 4-byte DWORD data block
	{
		byOutputBuffer[dwNumBytesToSend++] = data; // data; // Only byte of transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00; // Dummy byte for transmission data
	}

	// Shift out 1-bit SEQ Transfer Instruction: 0x0 for single WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;										   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000001;								   // Only 1 to leave Shift-DR
	ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
						dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Error while shifting out data for device %d\n", device.device_index);
		return;
	}
	// dwNumBytesToSend = 0; // Reset output buffer pointer
}

void iowrite16(DWORD addr, WORD data)
{
	BYTE byOutputBuffer[100];	// Buffer to hold MPSSE commands and data to be sent to the FT2232H
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0;	// Count of actual bytes sent - used with FT_Write

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return;

	// Goto Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05;		 // Number of clock pulses = Length + 1 (6 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00001101; // Data is shifted LSB first, so actually 101100

	// Clock out Command/Address register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;			 // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;			 // Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_ADDR_COMM; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;						// Clock bits out to TMS without read, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;						// Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_ADDR_COMM << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// Shift out AHB address DWORD (4 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byOutputBuffer[dwNumBytesToSend++] = (addr & 0xFF);			// First Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 8) & 0xFF);	// Second Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 16) & 0xFF); // Third Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 24) & 0xFF); // Last Byte of DWORD address

	// Shift out 2-bit AHB transfer size: 10 for WORD
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;	  // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x01;	  // Length + 1 (2 bits here)
	byOutputBuffer[dwNumBytesToSend++] = RW_WORD; // Write 16-bit WORD

	// Shift out 1-bit Read/Write Instruction: 0x1 for WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;													 // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;													 // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b10000001;											 // Ones only
	FT_STATUS ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
								  dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Error while shifting out WRITE command for device %d\n", device.device_index);
		return;
	}
	
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Go to Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b0000111; // 11100

	// Clock out Data register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_DATA; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;				   // Clock bits out to TMS, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_DATA << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;										   // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011;								   // Data is shifted LSB first, so the TMS pattern is 1100
	ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
						dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Communication error with JTAG device!\n");
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Shift out AHB data WORD (2 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	BYTE lsb = addr & 0xFF;

	// First byte of a 4-byte DWORD data block
	if (lsb % 4 == 0 || (lsb - 1) % 4 == 0) {
		byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = (data & 0xFF);		   // First Byte of WORD data
		byOutputBuffer[dwNumBytesToSend++] = ((data >> 8) & 0xFF); // Second Byte of WORD data
	} else { // Third byte of a 4-byte DWORD data block
		byOutputBuffer[dwNumBytesToSend++] = (data & 0xFF);		   // First Byte of WORD data
		byOutputBuffer[dwNumBytesToSend++] = ((data >> 8) & 0xFF); // Second Byte of WORD data
		byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Dummy byte for transmission data
		byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Dummy byte for transmission data
	}

	// Shift out 1-bit SEQ Transfer Instruction: 0x0 for single WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;										   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000001;								   // Only 1 to leave Shift-DR
	ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
						dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Error while shifting out data for device %d\n", device.device_index);
		return;
	}
	// dwNumBytesToSend = 0; // Reset output buffer pointer
}

void iowrite32(DWORD addr, DWORD data)
{
	BYTE byOutputBuffer[100];	// Buffer to hold MPSSE commands and data to be sent to the FT2232H
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0;	// Count of actual bytes sent - used with FT_Write

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return;

	// Goto Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05;		 // Number of clock pulses = Length + 1 (6 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00001101; // Data is shifted LSB first, so actually 101100

	// Clock out Command/Address register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;			 // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;			 // Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_ADDR_COMM; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;						// Clock bits out to TMS without read, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;						// Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_ADDR_COMM << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// Shift out AHB address DWORD (4 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byOutputBuffer[dwNumBytesToSend++] = (addr & 0xFF);			// First Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 8) & 0xFF);	// Second Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 16) & 0xFF); // Third Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((addr >> 24) & 0xFF); // Last Byte of DWORD address

	// Shift out 2-bit AHB transfer size: 10 for WORD
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;	   // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x01;	   // Length + 1 (2 bits here)
	byOutputBuffer[dwNumBytesToSend++] = RW_DWORD; // Write 32-bit DWORD

	// Shift out 1-bit Read/Write Instruction: 0x1 for WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;													 // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;													 // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b10000001;											 // Ones only
	FT_STATUS ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
								  dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Error while shifting out WRITE command for device %d\n", device.device_index);
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Go to Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b0000111; // 11100

	// Clock out Data register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_DATA; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;				   // Clock bits out to TMS, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_DATA << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;										   // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011;								   // Data is shifted LSB first, so the TMS pattern is 1100
	ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
						dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Communication error with JTAG device!\n");
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Shift out AHB data DWORD (4 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byOutputBuffer[dwNumBytesToSend++] = (data & 0xFF);			// First Byte of DWORD data
	byOutputBuffer[dwNumBytesToSend++] = ((data >> 8) & 0xFF);	// Second Byte of DWORD data
	byOutputBuffer[dwNumBytesToSend++] = ((data >> 16) & 0xFF); // Third Byte of DWORD data
	byOutputBuffer[dwNumBytesToSend++] = ((data >> 24) & 0xFF); // Last Byte of DWORD data

	// Shift out 1-bit SEQ Transfer Instruction: 0x0 for single WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;										   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000001;								   // Only 1 to leave Shift-DR
	ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
						dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Error while shifting out data for device %d\n", device.device_index);
		return;
	}
	// dwNumBytesToSend = 0; // Reset output buffer pointer
}

void iowrite32raw(DWORD startAddr, DWORD *data, WORD size)
{
	if (size > 256) // Check 1kB boundary for SEQ transfers
		fprintf(stderr, "Warning: Size is bigger than recommended 1 kB maximum (GR712RC-UM)!\n");

	BYTE byOutputBuffer[100];	// Buffer to hold MPSSE commands and data to be sent to the FT2232H
	DWORD dwNumBytesToSend = 0; // Index to the output buffer
	DWORD dwNumBytesSent = 0;	// Count of actual bytes sent - used with FT_Write

	if (reset_JTAG_state_machine() != FT_OK) // Reset back to TLR
		return;

	// Goto Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x05;		 // Number of clock pulses = Length + 1 (6 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00001101; // Data is shifted LSB first, so actually 101100

	// Clock out Command/Address register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;			 // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;			 // Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_ADDR_COMM; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;						// Clock bits out to TMS without read, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;						// Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_ADDR_COMM << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // Data is shifted LSB first, so the TMS pattern is 1100

	// Shift out AHB address DWORD (4 bytes)
	byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
	byOutputBuffer[dwNumBytesToSend++] = 0x00;

	byOutputBuffer[dwNumBytesToSend++] = (startAddr & 0xFF);		 // First Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((startAddr >> 8) & 0xFF);	 // Second Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((startAddr >> 16) & 0xFF); // Third Byte of DWORD address
	byOutputBuffer[dwNumBytesToSend++] = ((startAddr >> 24) & 0xFF); // Last Byte of DWORD address

	// Shift out 2-bit AHB transfer size: 10 for WORD
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;	   // Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x01;	   // Length + 1 (2 bits here)
	byOutputBuffer[dwNumBytesToSend++] = RW_DWORD; // Write 32-bit DWORD

	// Shift out 1-bit Read/Write Instruction: 0x1 for WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;													 // Clock bits out with read
	byOutputBuffer[dwNumBytesToSend++] = 0x00;													 // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b10000001;											 // Ones only
	FT_STATUS ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
								  dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Error while shifting out WRITE command for device %d\n", device.device_index);
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Go to Shift-IR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here)
	byOutputBuffer[dwNumBytesToSend++] = 0b0000111; // 11100

	// Clock out Data register opcode
	byOutputBuffer[dwNumBytesToSend++] = 0x1B;		// Clock bits out without read
	byOutputBuffer[dwNumBytesToSend++] = 0x04;		// Length + 1 (5 bits here), only 5 because the last one will be clocked by the next TMS command
	byOutputBuffer[dwNumBytesToSend++] = CODE_DATA; // First 5 bits of the 6-bit-long opcode

	// Clock out last bit of the opcode and leave to Exit-IR immediately
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;				   // Clock bits out to TMS, move out of Shift-IR and clock out the last TDI/DO bit
	byOutputBuffer[dwNumBytesToSend++] = 0x00;				   // Length + 1 (1 bits here)
	byOutputBuffer[dwNumBytesToSend++] = (CODE_DATA << 2) | 1; // Move the MSB of the opcode to the MSB of the BYTE, then make the first bit a 1 for TMS

	// Goto Shift-DR
	byOutputBuffer[dwNumBytesToSend++] = 0x4B;										   // Clock out TMS without read
	byOutputBuffer[dwNumBytesToSend++] = 0x03;										   // Number of clock pulses = Length + 1 (4 clocks here)
	byOutputBuffer[dwNumBytesToSend++] = 0b00000011;								   // Data is shifted LSB first, so the TMS pattern is 1100
	ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
						dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

	if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
		fprintf(stderr, "Communication error with JTAG device!\n");
		return;
	}
	dwNumBytesToSend = 0; // Reset output buffer pointer

	// Iterate over data package and write individual DWORDs to memory
	for (WORD i = 0; i < size; i++) {
		DWORD dataWord = data[i];

		// Shift out AHB data DWORD (4 bytes)
		byOutputBuffer[dwNumBytesToSend++] = 0x19; // Clock bytes out without read
		byOutputBuffer[dwNumBytesToSend++] = 0x03; // Length + 1 (4 bytes here)
		byOutputBuffer[dwNumBytesToSend++] = 0x00;

		byOutputBuffer[dwNumBytesToSend++] = (dataWord & 0xFF);			// First Byte of DWORD data
		byOutputBuffer[dwNumBytesToSend++] = ((dataWord >> 8) & 0xFF);	// Second Byte of DWORD data
		byOutputBuffer[dwNumBytesToSend++] = ((dataWord >> 16) & 0xFF); // Third Byte of DWORD data
		byOutputBuffer[dwNumBytesToSend++] = ((dataWord >> 24) & 0xFF); // Last Byte of DWORD data

		// Shift out 1-bit SEQ Transfer Instruction: 0x1 for sequential WRITE while simultaneously leaving Shift-DR via TMS Exit-DR
		byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock bits out with read
		byOutputBuffer[dwNumBytesToSend++] = 0x00;		 // Length + 1 (1 bits here)
		byOutputBuffer[dwNumBytesToSend++] = 0b10000001; // Only 1 to leave Shift-DR with SEQ Bit

		if (i < size - 1) { // Fixes an issue with subsequent _resetJTAGStateMachine clocking out another data point
			// Loop around once through Update-DR and then go back to Shift-DR
			byOutputBuffer[dwNumBytesToSend++] = 0x4B;		 // Clock bits out without read
			byOutputBuffer[dwNumBytesToSend++] = 0x03;		 // Length + 1 (3 bits here)
			byOutputBuffer[dwNumBytesToSend++] = 0b00000011; // 1100
		}
		ftStatus = FT_Write(device.ft_handle, byOutputBuffer,
							dwNumBytesToSend, &dwNumBytesSent); // Send off the TMS command

		if (ftStatus != FT_OK || dwNumBytesSent != dwNumBytesToSend) {
			fprintf(stderr, "Communication error with JTAG device!\n");
			break;
		}
		dwNumBytesToSend = 0; // Reset output buffer pointer
	}
}

void iowrite32(DWORD startAddr, DWORD *data, WORD size, bool progress)
{
	if (progress) // Optional terminal progress output
		printf("Writing data to memory...");

	WORD writeChunks = ceil((float)size / 256.0); // How many individual write chunks are needed

	DWORD tempData[256]; // Allocate the tempData array outside the loop with max size

	for (WORD i = 0; i < writeChunks; i++) {
		if (progress) // Optional terminal progress output
			printf("Writing data to memory... %d %%\n", (unsigned int)((i * 100.0) / (writeChunks - 1.0))); 

		DWORD addr = startAddr + 1024 * i;
		WORD writeSize; // Number of DWORDS to write

		if (i == writeChunks - 1 && size % 256 != 0) // Last write is not a full 1024B chunk
			writeSize = size % 256; // Remainder
		else
			writeSize = 256;

		for (WORD j = 0; j < writeSize; j++)
			tempData[j] = data[i * 256 + j];

		iowrite32raw(addr, tempData, writeSize);
	}

	if (progress) // Optional terminal progress output
		printf("Writing data to memory... Complete!   \n");
}


void pr_err(const char * const output)
{
	printf("[!] DSU ERROR: %s\n", output);
}
