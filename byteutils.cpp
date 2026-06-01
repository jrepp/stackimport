/*
 *  byteutils.cpp
 *  stackimport
 *
 *  Created by Mr. Z. on 03/31/10.
 *  Copyright 2010 Mr Z. All rights reserved.
 *
 */

#include "byteutils.h"
#include <string.h>


char * __hex(int x)
{
	const char	*	hex = "0123456789ABCDEF";
	static char		buf[4] = { 0 };
	unsigned int	value = static_cast<unsigned int>(x) & 0xFF;
	
	buf[0] = hex[(value / 16) % 16];
	buf[1] = hex[value % 16];
	buf[2] = 0;
	
	return buf;
}


// X-Ors the bytes in dest with those in src:
void xornstr(char * dest, const char * src, size_t n)
{
	for (size_t i = 0; i<n; i++)
	{
		const auto lhs = static_cast<unsigned char>(dest[i]);
		const auto rhs = static_cast<unsigned char>(src[i]);
		dest[i] = static_cast<char>(lhs ^ rhs);
	}
}

void shiftnstr(char * s, size_t n, int sh)
{
	int p = 1;
	int x = 0;
	for (int i=0; i<sh; i++) { p += p; }	// Bitshift p by sh bits?
	for (size_t i=0; i<n; i++)
	{
		x += (static_cast<unsigned char>(s[i]) * 65536) / p;		// Bitshift by 2 bytes?
		s[i] = static_cast<char>(x / 65536);						// Store low byte?
		x = (x % 65536) * 256;						// Keep high byte?
	}
}
