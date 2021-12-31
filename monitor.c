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

#include "uWFG.h"
#include "gen.h"
#include "monitor.h"


#define CR			13
#define LF			10
#define SP			32
#define CMD_LEN		80
#define CMD_ARGS	16

char mon_cmd[CMD_LEN+1];							// Command string buffer
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
	mon_cmd[CMD_LEN] = '\0';						// Termination to be sure
	printf("\n");
	printf("=============\n");
	printf(" uWFG-Pico   \n");
	printf("  PE1ATM     \n");
	printf(" 2021, Udjat \n");
	printf("=============\n");
	printf("Pico> ");								// prompt
}

wfg_t mon_wave;

/*** ------------------------------------------------------------- ***/
/*** Below the definitions of the shell commands, add where needed ***/
/*** ------------------------------------------------------------- ***/

/* 
 * Dumps a defined range of Si5351 registers 
 */
void mon_si(void)
{
	if (nargs>1) mon_wave.freq = atof(argv[2]);
	mon_wave.buf = (uint32_t *)sine16;
	mon_wave.len = 16/4;
	if (((*argv[1]) == 'A') || ((*argv[1]) == 'a'))
		wfg_play(OUTA, &mon_wave);
	else
		wfg_play(OUTB, &mon_wave);		
}


/* 
 * Dumps the entire built-in and programmed characterset on the LCD 
 */
void mon_sq(void)
{
	if (nargs>1) mon_wave.freq = atof(argv[2]);
	mon_wave.buf = (uint32_t *)block16;
	mon_wave.len = 16/4;
	if (((*argv[1]) == 'A') || ((*argv[1]) == 'a'))
		wfg_play(OUTA, &mon_wave);
	else
		wfg_play(OUTB, &mon_wave);		
}


/* 
 * Checks for inter-core fifo overruns 
 */
void mon_sa(void)
{
	if (nargs>1) mon_wave.freq = atof(argv[2]);
	mon_wave.buf = (uint32_t *)saw256;
	mon_wave.len = 256/4;
	if (((*argv[1]) == 'A') || ((*argv[1]) == 'a'))
		wfg_play(OUTA, &mon_wave);
	else
		wfg_play(OUTB, &mon_wave);		
}


/* 
 * Sets sample clock 
 */
void mon_cl(void)
{
	if (nargs>1) mon_wave.freq = atof(argv[2]);
	if (((*argv[1]) == 'A') || ((*argv[1]) == 'a'))
		wfg_play(OUTA, &mon_wave);
	else
		wfg_play(OUTB, &mon_wave);		
}


/*
 * Command shell table, organize the command functions above
 */
#define NCMD	4
shell_t shell[NCMD]=
{
	{"si", 2, &mon_si, "si {A|B} <clk>", "sine wave at sample rate clk"},
	{"sq", 2, &mon_sq, "sq {A|B} <clk>", "square wave at sample rate clk"},
	{"sa", 2, &mon_sa, "sa {A|B} <clk>", "sawtooth at sample rate clk"},
	{"cl", 2, &mon_cl, "cl {A|B} <clk>", "set sample rate clk"}
};



/*** ---------------------------------------- ***/
/*** Commandstring parser and monitor process ***/
/*** ---------------------------------------- ***/

/*
 * Command line parser
 */
void mon_parse(char* s)
{
	char *p;
	int  i;

	p = s;											// Set to start of string
	nargs = 0;
	while (*p!='\0')								// Assume stringlength >0 
	{
		while (*p==' ') p++;						// Skip whitespace
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
void mon_evaluate(void)
{
	static int i = 0;
	int c = getchar_timeout_us(10L);				// NOTE: this is the only SDK way to read from stdin
	if (c==PICO_ERROR_TIMEOUT) return;				// Early bail out
	
	switch (c)
	{
	case CR:										// CR : need to parse command string
		putchar('\n');								// Echo character, assume terminal appends CR
		mon_cmd[i] = '\0';							// Terminate command string		
		if (i>0)									// something to parse?
			mon_parse(mon_cmd);						// --> process command
		i=0;										// reset index
		printf("Pico> ");							// prompt
		break;
	case LF:
		break;										// Ignore, assume CR as terminator
	default:
		if ((c<32)||(c>=128)) break;				// Only allow alfanumeric
		putchar((char)c);							// Echo character
		mon_cmd[i] = (char)c;						// store in command string
		if (i<CMD_LEN) i++;							// check range and increment
		break;
	}
}
