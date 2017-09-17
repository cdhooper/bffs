#include <exec/memory.h>
#include <intuition/intuition.h>

#include "debug.h"
#include "request.h"


struct IntuiText youmust = { 1, 2, 3,   3,  7, NULL, "You must replace volume",  NULL};
struct IntuiText blkmess = { 1, 2, 3,   3,  7, NULL, "Block 000000 of volume",   NULL};
struct IntuiText dircorr = { 1, 2, 3,   3,  7, NULL, "Run fsck!  Directory",     NULL};
struct IntuiText fscorru = { 1, 2, 3,   3,  7, NULL, "Run fsck!  Filesystem",    NULL};
struct IntuiText bffsbug = { 1, 2, 3,   3,  7, NULL, "BFFS Internal error on",   NULL};
/*
struct IntuiText warning = { 1, 2, 3,   3,  7, NULL, "WARNING: This is a BETA",  NULL};
struct IntuiText betapre = { 1, 2, 3,   3, 15, NULL, "prerelease of BFFS 1.3",   NULL};
*/
struct IntuiText txtname = { 1, 2, 3,   3, 15, NULL, "                        ", NULL};
/*
struct IntuiText version = { 1, 2, 3,   3, 23, NULL, "Please Do Not Distribute", NULL};
*/
struct IntuiText indrive = { 1, 2, 3,   3, 23, NULL, "in trackdisk,0          ", NULL};
struct IntuiText rwerror = { 1, 2, 3,   3, 23, NULL, "has a Read/Write error",   NULL};
struct IntuiText corrcon = { 1, 2, 3,   3, 23, NULL, "corrupt. Continue anyway?",NULL};
struct IntuiText contath = { 1, 2, 3,   3, 23, NULL, "Author code [          ]", NULL};
struct IntuiText postext = { 1, 2, 3,  10, 64, NULL, "Retry",                    NULL};
struct IntuiText negtext = { 1, 2, 3, 150, 64, NULL, "Cancel",                   NULL};
struct IntuiText okytext = { 1, 2, 3, 150, 64, NULL, "Okay",                     NULL};

#define inittext(text, str, pos)					\
	text.FrontPen = 1;			text.BackPen = 2;	\
	text.DrawMode = 3;			text.LeftEdge = 3;	\
	text.TopEdge = 7 + 8 * pos;		text.ITextFont = NULL;	\
	text.IText = str;			text.NextText = NULL;

do_request(type, data1, data2)
ULONG data1;
ULONG data2;
{
/*
	warning.NextText = &betapre;
	betapre.NextText = &version;

	return(AutoRequest(NULL, &warning, NULL,
		&okytext, 0, 0, 180, 80));
	return(AutoRequest(NULL, &blkmess, &postext,
		&negtext, 0, 0, 180, 80));
*/
}
