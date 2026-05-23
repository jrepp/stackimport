/*
 *  byteutils.h
 *  stackimport
 *
 *  Created by Mr. Z. on 03/31/10.
 *  Copyright 2010 Mr Z. All rights reserved.
 *
 */

#pragma once

#include <cstddef>

char *		__hex( int x );
void		xornstr( char * dest, const char * src, size_t n );
void		shiftnstr( char * s, size_t n, int sh );
