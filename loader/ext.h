/*
 * Modified for use with MPlayer, detailed changelog at
 * http://svn.mplayerhq.hu/mplayer/trunk/
 * $Id: ext.h,v 1.2 2006/09/13 08:13:07 nickols_k Exp $
 */

#ifndef loader_ext_h
#define loader_ext_h

#include "wine/windef.h"

extern LPVOID FILE_dommap( int unix_handle, LPVOID start,
			   DWORD size_high, DWORD size_low,
			   DWORD offset_high, DWORD offset_low,
			   int prot, int flags );
extern int FILE_munmap( LPVOID start, DWORD size_high, DWORD size_low );
extern int wcsnicmp(const unsigned short* s1, const unsigned short* s2, int n);
extern int __vprintf( const char *format, ... );

#endif
