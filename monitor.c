/*
 * monitor.c
 *
 * Created: Dec 2021
 * Author: Arjan te Marvelde
 * 
 * Command shell on stdin/stdout.
 * Collects characters and parses commandstring.
 * Additional commands can easily be added.
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pll.h"

#include "uWFG.h"
#include "gen.h"
#include "monitor.h"


#define BS			 8
#define LF			10
#define CR			13
#define SP			32
#define CMD_LEN		80
#define CMD_ARGS	16

char *argv[CMD_ARGS];								// Argument pointers, first is command
int nargs;											// Nr of arguments, including command

typedef struct 
{
	char *cmdstr;									// Command string
	int   cmdlen;									// Command string length
	void (*cmd)(void);								// Command executive
	char *cmdsyn;									// Command syntax
	char *help;										// Command help text
} shell_t;




/*** Initialisation, called at startup ***/
void mon_init()
{
    stdio_init_all();								// Initialize Standard IO
	printf("\n");
	printf("=============\n");
	printf(" uWFG-Pico   \n");
	printf("  PE1ATM     \n");
	printf(" 2021, Udjat \n");
	printf("=============\n");
	printf("Pico> ");								// prompt
}


/*** ------------------------------------------------------------- ***/
/*** Below the definitions of the shell commands, add where needed ***/
/*** ------------------------------------------------------------- ***/

wfg_t mon_wave;


/*
 * Print Fsys
 */
void mon_fsys(void)
{
	float f = 1.2e7;														// Assume 12MHz XOSC
	f *= pll_sys_hw->fbdiv_int&0xfff;										// Feedback divider
	f /= (pll_sys_hw->prim&0x00070000)>>16;									// Primary divider 1
	f /= (pll_sys_hw->prim&0x00007000)>>12;									// Primary divider 2
	printf("System clock: %9.0f Hz\n", f);
}

/*
 * Command shell table, organize the command functions above
 */
#define NCMD	1
shell_t shell[NCMD]=
{
	{"fsys", 4, &mon_fsys, "fsys", "Print system clock frequency"}
};



/*** ---------------------------------------- ***/
/*** Commandstring parser and monitor process ***/
/*** ---------------------------------------- ***/

/*
 * Command line parser
 * Fills an array of argument substrings (char *argv[])
 * Total number of arguments is stored (int nargs)
 */
void mon_parse(char* s)
{
	char *p;
	int  i;

	printf("%s\n", s);								// Echo string for debugging purposes
	p = s;											// Set to start of string
	nargs = 0;
	while (*p!='\0')								// Assume stringlength >0 
	{
		while (*p==' ') *p++='\0';					// Replace & skip whitespace
		if (*p=='\0') break;						// String might end in spaces
		argv[nargs++] = p;							// Store first valid char loc after whitespace
		while ((*p!=' ')&&(*p!='\0')) p++;			// Skip non-whitespace
	}
	if (nargs==0) return;							// No command or parameter
	for (i=0; i<NCMD; i++)							// Lookup shell command
		if (strncmp(argv[0], shell[i].cmdstr, shell[i].cmdlen) == 0) break;
	if (i<NCMD)
		(*shell[i].cmd)();
	else											// Unknown command
	{
		for (i=0; i<NCMD; i++)						// Print help if no match
			printf("%s\n   %s\n", shell[i].cmdsyn, shell[i].help);
	}
}

/*
 * Monitor process 
 * This function collects characters from stdin until CR
 * Then the command is send to a parser and executed.
 */
char mon_cmd[CMD_LEN+1];										// Command string buffer
int  mon_pos = 0;												// Current position in command string
void mon_evaluate(void)
{
	int c;

	c = getchar_timeout_us(10L);								// NOTE: this is the only SDK way to read from stdin
	if (c==PICO_ERROR_TIMEOUT) return;							// Early bail out
	
	switch (c)
	{
	case BS:
		if (mon_pos>0)
		{
			putchar(BS);										// Echo backspace
			mon_cmd[mon_pos--] = '\0';							// Reset character
		}
		break;
	case LF:
		break;													// Ignore LF, assume CR as terminator
	case CR:													// CR : need to parse command string
		putchar('\n');											// Echo character, assume terminal appends CR
		mon_cmd[mon_pos] = '\0';								// Terminate command string		
		if (mon_pos>0)											// something to parse?
			mon_parse(mon_cmd);									// --> process command
		mon_pos=0;												// reset index
		printf("Pico> ");										// prompt
		break;	
	default:
		if ((c<32)||(c>=128)) break;							// Only allow alfanumeric
		putchar((char)c);										// Echo character
		mon_cmd[mon_pos] = (char)c;								// store in command string
		if (mon_pos<CMD_LEN) mon_pos++;							// check range and increment
		break;
	}
}
