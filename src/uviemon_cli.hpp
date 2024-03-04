/*
	============================================
	uviemon: free(TM) replacement for grmon

	Functions used in the command line interface
	of uviemon. All these functions corrrespond
	to their command in the console to be called
	by the user.
	============================================
*/

#ifndef UVIEMON_CLI_HPP
#define UVIEMON_CLI_HPP

#include "ftdi_device.hpp"

#include <string>
#include <unordered_map>

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#define MAX_PARAMETERS 3
#define MAX_PARAM_LENGTH 50

typedef struct {
	const char *command_name;
	void (*function)(const char *, int, char [MAX_PARAMETERS][MAX_PARAM_LENGTH]);
} command;

typedef struct {
	
} command_parameter;


extern std::unordered_map<int, std::string> tt_errors;

std::string _hexToString(DWORD *data, size_t size);
std::string _hexToString(WORD *data, size_t size);
std::string _hexToString(BYTE *data, size_t size);

int parse_input(char *input);
void print_help_text();
//void cleanup();


/* Command functions  */
void help  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void scan  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void memx  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void wmemx (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void run   (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void reset (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void load  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void verify(const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void bdump (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);
void wash  (const char *command, int param_count, char params[MAX_PARAMETERS][MAX_PARAM_LENGTH]);

void wmem(DWORD addr, DWORD data);
void wmemh(DWORD addr, WORD data);
void wmemb(DWORD addr, BYTE data);

void mem(DWORD startAddr, DWORD length = 16);
void memh(DWORD startAddr, DWORD length = 32);
void memb(DWORD startAddr, DWORD length = 64);

void bdump(DWORD startAddr, DWORD length, std::string path);

void wash(WORD size = 16, DWORD addr = 0x40000000, DWORD c = 0);
void load(std::string path);
void verify(std::string path); // Really slow because sequential reads are dead slow


#endif /* UVIEMON_CLI_HPP */
