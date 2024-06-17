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
#include <stdint.h>

extern const unsigned int CODE_ADDR_COMM;
extern const DWORD CODE_DATA;

typedef struct {
	FT_HANDLE ft_handle;
	DWORD device_index;
	int cpu_type;
	bool first_run;
	uint32_t active_cpu;
} ftdi_device;

FT_STATUS ftdi_open_device(DWORD device_index, int cpu_type);
void ftdi_close_device();

int ftdi_get_connected_cpu_type();

void ftdi_set_active_cpu(uint32_t cpu);
uint32_t ftdi_get_active_cpu();
void ftdi_set_cpu_idle(uint32_t cpu);

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

void ioread32_buffer(DWORD startAddr, DWORD *data, WORD size);
void iowrite32_buffer(DWORD startAddr, DWORD *data, WORD size);
void ioread32_progress(DWORD startAddr, DWORD *data, WORD size, bool progress);
void iowrite32_progress(DWORD startAddr, DWORD *data, WORD size, bool progress);


void pr_err(const char * const output);

#endif /* FTDI_DEVICE_HPP */
