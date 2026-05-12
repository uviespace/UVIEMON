UVIEMON(1)

# NAME

uviemon - replacement tool for grmon

# SYNOPSIS

*uviemon* [options...]

# OPTIONS

*-list*
	Displays a list of connected jtag devices

*-info*
	Displays version information

*-cpu-type <number>*
	Manually specifies the cpu type 0 for LEON 3 and 1 for LEON 4.
	In most cases this will be autodetected and unnecessary.

*-jtag <number>*
	Connects to the specified jtag device. Obtain the correct number from the list command

*-autostart <file>*
	Opens a command file and runs each command specified in the file.
	Only works in conjunction with the -jtag flag

# DESCRIPTION

uviemon acts as a replacement tool for grmon and implements a subset of its commands, but
the implemented commands are generally faster. Notable omission of grmon's features is the
inclusion of the TCL scripting language.

# COMMANDS

*help*
	Displays a short overview off all implemented commands

*scan*
	Scans for all possible IR opcodes

*reset*
	Resets the currently active cpu core

*mem <address> <length>*
	Reads <length> 32-bit DWORDs starting at address <address>, grouping by DWORDs

*memh <address> <length>*
	Reads <length> 16-bit WORDs starting at address <address>, grouping by WORDs

*memb <address> <length>*
	Reads <length> bytes starting at address <address>, grouping by bytes

*wmem <data> <address>*
	Writes a 32-bit DWORD to address <address>

*wmemh <data> <address>*
	Writes a 16-bit WORD to address <address>

*wmemb <data> <address>*
	Writes a byte to address <address>

*bdump <length> <address> <file>*
	Write <length> bytes starting from address <address> into the file <file>

*cpu [num] [enable|disable|active]*
	Without parameters prints all cpu cores along with their status. It can also be used
	to enable disable a specific cpu core and to set a core active, which in turn has
	cosequences for reset, inst, reg, run commands

*inst [num]*
	Prints the last [num] instructions on the currently active cpu core. With no parameter
	the number of instructions defaults to 10

*wash <length> <address> <value>*
	Washes memory with <value> starting at address <address> and writing the next <length> 32-bit DWORDs

*reg [register] [value]*
	Running without any parameter will print a summary of the registers with their current values. [register]
	can be any of the special purpose register psr, tbr, wim, y, pc, npc, fsr, sp, fp. Also it can be one of
	the standard register g, i, l, o, f, d followed by a number. It is also possible to prefix a register window
	with 'w', e.g. w5l2 for the third local register in the sixth window. This will of course only work for
	register that exist in different windows. 

	Supplying a value will set the register with this value.

	This command will also be effected by the cpu command and show register for the currently active cpu core.

*load <file>*
	Loads a binary file into memory as preparation to run it

*verify <file>*
	Verifies that the file <file> was correctly written to memory

*run*
	Runs the previously loaded binary file


# AUTHORS

Maintained by Johann Böhm <johann.boehm@univie.ac.at>. Originally developed by Matthias Rosezky
as part of his Bachelor thesis.