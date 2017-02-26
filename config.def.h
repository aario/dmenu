/* See LICENSE file for copyright and license details. */
/* Default settings; can be overriden by command line. */

static int topbar = 1;                      /* -b  option; if 0, dmenu appears at bottom     */
/* -fn option overrides fonts[0]; default X11 font or font set */
static const char *fonts[] = {
	"monospace:size=15"
};
static const char *prompt      = NULL;      /* -p  option; prompt to the left of input field */
static const char *firstbgcolor = "#000000"; /* -fb option; normal background for the first line */
static const char *firstfgcolor = "#eeeeee"; /* -ff option; normal foreground for the first line */
static const char *normbgcolor = "#009090"; /* -nb option; normal background                 */
static const char *normfgcolor = "#000000"; /* -nf option; normal foreground                 */
static const char *selbgcolor  = "#003355"; /* -sb option; selected background               */
static const char *selfgcolor  = "#ffffff"; /* -sf option; selected foreground               */
static const char *outbgcolor  = "#00ffff";
static const char *outfgcolor  = "#000000";
/* -l option; if nonzero, dmenu uses vertical list with given number of lines */
static unsigned int lines      = 10;
/* intensity of blur level*/
static unsigned int blurlevel  = 20;
/* output selected number instead of text */
static unsigned int output_number = 0;
/* default selected item number */
static unsigned int default_number = 0;
/* keep sending selection to output on move */
static unsigned int output_on_move = 0;
/* Reverse up down keys */
static unsigned int reverse_updown = 0;
/* Stay until Escape key pressed */
static unsigned int stay_after_select = 0;

//Used for multi-threaded blur effect
#define CPU_THREADS 4 

/*
 * Characters not considered part of a word while deleting words
 * for example: " /?\"&[]"
 */
static const char worddelimiters[] = " ";
