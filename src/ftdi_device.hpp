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

#ifndef FTDI_DEVICE_HPP
#define FTDI_DEVICE_HPP

#include "lib/ftdi/ftd2xx.h"

#include <stdbool.h>

const unsigned int CODE_ADDR_COMM = 0x2; // address/command register opcode, 35-bit length
const DWORD CODE_DATA = 0x3;			 // data register opcode, 33-bit length

const BYTE RW_DWORD = 0b0000010; // 10 for 32-bit DWORD
const BYTE RW_WORD = 0b0000001;	 // 01 for 16-bit WORD
const BYTE RW_BYTE = 0b00000000; // 00 for 8-bit BYTE

//const DWORD UART0_STATUS_REG = 0x80000104;
//const DWORD UART0_CTRL_REG = 0x80000108;
//const DWORD UART0_FIFO_REG = 0x80000110;

const DWORD UART0_STATUS_REG = 0x4;
const DWORD UART0_CTRL_REG = 0x8;
const DWORD UART0_FIFO_REG = 0x10;


typedef struct {
	FT_HANDLE ft_handle;
	DWORD device_index;
	int cpu_type;
	bool first_run;
} ftdi_device;

FT_STATUS open_device(DWORD device_index, int cpu_type);
void close_device();

int get_connected_cpu_type();

DWORD get_devices_count();
void get_device_list();

/*
 * Scans and checks
 */

DWORD read_idcode();
BYTE get_JTAG_count();
BYTE scan_IR_length();
BYTE scan_DR_length(BYTE opcode);
void scan_instruction_codes(BYTE max_ir_length);

/*
 * DSU operations for runnning programs
 */

BYTE runCPU(BYTE cpuID);	
void reset(BYTE cpuID); 


/*
 * Memory RW operations
 */

BYTE ioread8(DWORD addr);
WORD ioread16(DWORD addr);
DWORD ioread32(DWORD addr);

void iowrite8(DWORD addr, BYTE data);
void iowrite16(DWORD addr, WORD data);
void iowrite32(DWORD addr, DWORD data);

// Sequential RW w/ optional progress output
void ioread32raw(DWORD startAddr, DWORD *data, WORD size); // Extremely slow, it's actually faster to write data...
void iowrite32raw(DWORD startAddr, DWORD *data, WORD size);
void ioread32(DWORD startAddr, DWORD *data, WORD size, bool progress = false);
void iowrite32(DWORD startAddr, DWORD *data, WORD size, bool progress = false);


void pr_err(const char * const output);

#endif /* FTDI_DEVICE_HPP */
