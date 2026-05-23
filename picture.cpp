/*

 Picture Class for C++
 (c) 2005 Rebecca Bettencourt / Kreative Korporation

 This is a class that lets us manipulate bitmapped graphics in memory.


 This code is under the MIT license.

 Permission is hereby granted, free of charge, to any person obtaining a copy of
 this software and associated documentation files (the "Software"), to deal in the
 Software without restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies
 or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <cstring>
#include <fstream>
#include <algorithm>
#include <climits>
#include "picture.h"
#include "woba.h"
#include "CBuf.h"

using namespace std;

static size_t size_from_nonnegative_int(int value)
{
	return value > 0 ? static_cast<size_t>(value) : 0;
}

static std::streamsize stream_size_from_nonnegative_int(int value)
{
	return value > 0 ? static_cast<std::streamsize>(value) : 0;
}

static char char_from_byte_value(unsigned int value)
{
	return static_cast<char>(static_cast<unsigned char>(value));
}

static void bounded_copy_in(char * dest, int destLength, int destStart, const char * src, int count)
{
	if( !dest || !src || count <= 0 || destLength <= 0 )
		return;
	int srcStart = 0;
	if( destStart < 0 )
	{
		srcStart = -destStart;
		count -= srcStart;
		destStart = 0;
	}
	if( count <= 0 || destStart >= destLength )
		return;
	count = std::min( count, destLength -destStart );
	memcpy( dest +destStart, src +srcStart, static_cast<size_t>(count) );
}

static void bounded_copy_out(char * dest, int destLength, const char * src, int srcLength, int srcStart, int count)
{
	if( !dest || count <= 0 || destLength <= 0 )
		return;
	memset( dest, 0, static_cast<size_t>(std::min( count, destLength )) );
	if( !src || srcLength <= 0 )
		return;
	int destStart = 0;
	if( srcStart < 0 )
	{
		destStart = -srcStart;
		count -= destStart;
		srcStart = 0;
	}
	if( count <= 0 || srcStart >= srcLength || destStart >= destLength )
		return;
	count = std::min( count, srcLength -srcStart );
	count = std::min( count, destLength -destStart );
	memcpy( dest +destStart, src +srcStart, static_cast<size_t>(count) );
}

static void bounded_fill(char * dest, int destLength, int destStart, char ch, int count)
{
	if( !dest || count <= 0 || destLength <= 0 )
		return;
	if( destStart < 0 )
	{
		count += destStart;
		destStart = 0;
	}
	if( count <= 0 || destStart >= destLength )
		return;
	count = std::min( count, destLength -destStart );
	memset( dest +destStart, ch, static_cast<size_t>(count) );
}

int __bitmap_row_width(int width, int, int depth)
{
	if( width <= 0 || depth <= 0 || width > (INT_MAX / depth) )
		return 0;
	return ( ((width * depth) / 8) + ( ((width * depth) % 8)?1:0 ) );
}

int __bitmap_size(int width, int height, int depth)
{
	int rowWidth = __bitmap_row_width(width,height,depth);
	if( height <= 0 || rowWidth <= 0 || height > (INT_MAX / rowWidth) )
		return 0;
	return rowWidth * height;
}

unsigned int __pow2(int p)
{
	/* finds 2^p */
	/* also returns bit p set */
	int i;
	unsigned int m = 1;
	for (i=p; i>=0; i--) {
		m += m;
	}
	return m;
}

unsigned int __pow21(int p)
{
	/* finds 2^p - 1 */
	/* also returns bits 0 - p-1 set */
	/* or the least significant p bits set */
	int i;
	unsigned int m = 0;
	for (i=p; i>=0; i--) {
		m += m+1;
	}
	return m;
}

picture::picture(void)
	: width(0), height(0), depth(0), greyscalemask(false),
	rowlength(0), maskrowlength(0), bitmaplength(0), bitmap(nullptr),
	masklength(0), mask(nullptr)
{
}

picture::picture(int w, int h, int d, bool greymask)
	: width(0), height(0), depth(0), greyscalemask(false),
	rowlength(0), maskrowlength(0), bitmaplength(0), bitmap(nullptr),
	masklength(0), mask(nullptr)
{
	reinit( w, h, d, greymask );
}

picture::~picture(void)
{
	delete [] bitmap;
	delete [] mask;
}

void picture::reinit(int w, int h, int d, bool greymask)
{
	delete [] bitmap;
	delete [] mask;
	bitmap = nullptr;
	mask = nullptr;
	width = w;
	height = h;
	depth = d;
	greyscalemask = greymask;
	rowlength = __bitmap_row_width(w,h,d);
	maskrowlength = __bitmap_row_width(w,h,greymask?8:1);
	bitmaplength = __bitmap_size(w,h,d);
	masklength = __bitmap_size(w,h,greymask?8:1);
	if( bitmaplength <= 0 || masklength <= 0 )
	{
		width = height = depth = 0;
		rowlength = maskrowlength = bitmaplength = masklength = 0;
		return;
	}
	bitmap = new char[static_cast<size_t>(bitmaplength)];
	mask = new char[static_cast<size_t>(masklength)];
	memset(bitmap, 0, static_cast<size_t>(bitmaplength));
	memset(mask, 0xFF, static_cast<size_t>(masklength));
}

int picture::gwidth(void) { return width; }
int picture::gheight(void) { return height; }
int picture::gdepth(void) { return depth; }
int picture::gmaskdepth(void) { return greyscalemask?8:1; }
int picture::bitmapsize(void) { return bitmaplength; }
int picture::masksize(void) { return masklength; }

int picture::coordbyteoffset(int x, int y)
{
	return (rowlength * y) + ((x * depth) / 8);
}

unsigned int picture::coordbitmask( int x, int )
{
	int				i;
	unsigned int	m = __pow21(depth);
	if( depth < 8 )
	{
		switch (depth)
		{
			case 1:
				m = 1 << (7 -(x % 8));
				break;
			case 2:
				i=2*(3-(x%4));
				m <<= i;
				break;
			case 4:
				i=4*(1-(x%2));
				m <<= i;
				break;
		}
	}
	return m;
}

int picture::maskcoordbyteoffset(int x, int y)
{
	return (maskrowlength * y) + (greyscalemask?x:(x/8));
}

unsigned int picture::maskcoordbitmask(int x, int)
{
	unsigned int m = greyscalemask ? 0xFF : 1;
	if( !greyscalemask )
		m <<= 7 -(x % 8);
	return m;
}



void picture::memcopyin(const char * src, int start, int count)
{
	bounded_copy_in(bitmap, bitmaplength, start, src, count);
}

void picture::memcopyin(const char * src, int x, int y, int count)
{
	bounded_copy_in(bitmap, bitmaplength, coordbyteoffset(x,y), src, count);
}

void picture::maskmemcopyin(const char * src, int start, int count)
{
	bounded_copy_in(mask, masklength, start, src, count);
}

void picture::maskmemcopyin(const char * src, int x, int y, int count)
{
	bounded_copy_in(mask, masklength, maskcoordbyteoffset(x,y), src, count);
}



void picture::memcopyout(char * dest, int start, int count)
{
	bounded_copy_out(dest, count, bitmap, bitmaplength, start, count);
}

void picture::memcopyout(char * dest, int x, int y, int count)
{
	bounded_copy_out(dest, count, bitmap, bitmaplength, coordbyteoffset(x,y), count);
}

void picture::maskmemcopyout(char * dest, int start, int count)
{
	bounded_copy_out(dest, count, mask, masklength, start, count);
}

void picture::maskmemcopyout(char * dest, int x, int y, int count)
{
	bounded_copy_out(dest, count, mask, masklength, maskcoordbyteoffset(x,y), count);
}



void picture::memcopyout(CBuf& dest, int start, int count)
{
	bounded_copy_out(dest.buf(0, size_from_nonnegative_int(count)), count, bitmap, bitmaplength, start, count);
}

void picture::memcopyout(CBuf& dest, int x, int y, int count)
{
	bounded_copy_out(dest.buf(0, size_from_nonnegative_int(count)), count, bitmap, bitmaplength, coordbyteoffset(x,y), count);
}

void picture::maskmemcopyout(CBuf& dest, int start, int count)
{
	bounded_copy_out(dest.buf(0, size_from_nonnegative_int(count)), count, mask, masklength, start, count);
}

void picture::maskmemcopyout(CBuf& dest, int x, int y, int count)
{
	bounded_copy_out(dest.buf(0, size_from_nonnegative_int(count)), count, mask, masklength, maskcoordbyteoffset(x,y), count);
}



void picture::memfill(char ch, int start, int count)
{
	bounded_fill(bitmap, bitmaplength, start, ch, count);
}

void picture::memfill(char ch, int x, int y, int count)
{
	bounded_fill(bitmap, bitmaplength, coordbyteoffset(x,y), ch, count);
}

void picture::maskmemfill(char ch, int start, int count)
{
	bounded_fill(mask, masklength, start, ch, count);
}

void picture::maskmemfill(char ch, int x, int y, int count)
{
	bounded_fill(mask, masklength, maskcoordbyteoffset(x,y), ch, count);
}


void picture::buildmaskfromsurroundings()
{
	//debugprint();
	
	maskmemfill( 0xFF, 0, rowlength * height );		// All black.
	
	//debugprint();
	
	int x = 0, y = 0;
	for( x = 0; x < width; x++ )
		scanstartingatpixel( x, y );
	
	//debugprint();
	
	y = height -1;
	for( x = 0; x < width; x++ )
		scanstartingatpixel( x, y );
	
	//debugprint();
	
	x = 0;
	for( y = 0; y < height; y++ )
		scanstartingatpixel( x, y );
	
	//debugprint();
	
	x = width -1;
	for( y = 0; y < height; y++ )
		scanstartingatpixel( x, y );
	
	//debugprint();
}


void picture::scanstartingatpixel( int x, int y )
{
	// We've not fallen off the edge, have we?
	if( x < 0 || y < 0 || x >= width || y >= height )
		return;
	
	// This is a white pixel and the mask for it is still opaque?
	//	i.e. we haven't processed it yet?
	if( 0 == getpixel( x, y ) && 1 == maskgetpixel( x, y ) )
	{
		masksetpixel( x, y, 0x00 );	// Punch hole in the mask.
		
		// Scan surrounding pixels:
		scanstartingatpixel( x -1, y );
		scanstartingatpixel( x +1, y );
		scanstartingatpixel( x, y -1 );
		scanstartingatpixel( x, y +1 );
	}
}


void	picture::debugprint()
{
	int	x = 0, y = 0;
	for( y = 0; y < width; y++ )
	{
		for( x = 0; x < width; x++ )
		{
			printf( "%c", getpixel( x, y ) ? 'X' : ' ' );
		}
		printf("\n");
	}
	
	for( y = 0; y < width; y++ )
	{
		for( x = 0; x < width; x++ )
		{
			printf( "%c", maskgetpixel( x, y ) ? 'X' : ' ' );
		}
		printf("\n");
	}
	
	printf("\n");
}


void picture::copyrow(int dest, int src)
{
	if( dest < 0 || src < 0 || dest >= height || src >= height || rowlength <= 0 )
		return;
	memcpy(bitmap + coordbyteoffset(0,dest),
	       bitmap + coordbyteoffset(0,src),
	       static_cast<size_t>(rowlength));
}

void picture::maskcopyrow(int dest, int src)
{
	if( dest < 0 || src < 0 || dest >= height || src >= height || maskrowlength <= 0 )
		return;
	memcpy(mask + maskcoordbyteoffset(0,dest),
	       mask + maskcoordbyteoffset(0,src),
	       static_cast<size_t>(maskrowlength));
}

unsigned int picture::fixcolor(unsigned int c)
{
	return (depth >= 32)?c:(c & __pow21(depth));
}

unsigned int picture::dupcolor(unsigned int c)
{
	int i;
	unsigned int f,p,d;
	
	if( depth <= 0 ) { return 0; }
	if (depth >= 32) { return c; }
	
	f = (c & __pow21(depth));
	p = __pow2(depth);
	d = 0;
	
	for (i=32/depth; i>=0; i--) {
		d *= p;
		d += f;
	}
	return d;
}

unsigned int picture::getpixel(int x, int y)
{
	if( !bitmap || x < 0 || y < 0 || x >= width || y >= height )
		return 0;
	int byteIndex = coordbyteoffset(x,y);
	int j;
	if (depth < 8)
	{
		if( static_cast<unsigned char>(bitmap[byteIndex]) & coordbitmask(x,y) )
			return 1;
		else
			return 0;
	}
	else if (depth == 8)
	{
		return static_cast<unsigned char>(bitmap[byteIndex]);
	}
	else {
		j = depth / 8;
		int	p1 = 0;
		while (j) {
			p1 = (p1 * 256) + static_cast<unsigned char>(bitmap[byteIndex]);
			byteIndex++;
			j--;
		}
		return static_cast<unsigned int>(p1);
	}
}

void picture::setpixel(int x, int y, int c)
{
	if( !bitmap || x < 0 || y < 0 || x >= width || y >= height )
		return;
	int i = coordbyteoffset(x,y);
	unsigned int d = dupcolor(static_cast<unsigned int>(c));
	unsigned int f = fixcolor(static_cast<unsigned int>(c));
	int j;
	if (depth < 8) {
		const unsigned int value = (static_cast<unsigned char>(bitmap[i]) & (~coordbitmask(x,y))) | (d & coordbitmask(x,y));
		bitmap[i] = char_from_byte_value(value);
	} else if (depth == 8) {
		bitmap[i] = char_from_byte_value(f);
	} else {
		j = depth / 8;
		while (j) {
			j--;
			bitmap[i+j] = char_from_byte_value(f & 0xFF);
			f /= 256;
		}
	}
}

unsigned int picture::maskgetpixel(int x, int y)
{
	if( !mask || x < 0 || y < 0 || x >= width || y >= height )
		return 0;
	int i = maskcoordbyteoffset(x,y);
	if (greyscalemask) {
		return static_cast<unsigned char>(mask[i]);
	} else {
		i = static_cast<int>(static_cast<unsigned char>(mask[i]) & maskcoordbitmask(x,y));
		return i ? 1 : 0;
	}
}

void picture::masksetpixel(int x, int y, int c)
{
	if( !mask || x < 0 || y < 0 || x >= width || y >= height )
		return;
	int i = maskcoordbyteoffset(x,y);
	unsigned int d = dupcolor(static_cast<unsigned int>(c));
	if (greyscalemask) {
		mask[i] = char_from_byte_value(d & 0xFF);
	} else {
		unsigned int bitpos = maskcoordbitmask(x,y);
		const unsigned int value = (static_cast<unsigned char>(mask[i]) & (~bitpos)) | (d & bitpos);
		mask[i] = char_from_byte_value(value);
	}
}

void picture::__directcopybmptomask(void)
{
	if( !mask || !bitmap )
		return;
	memcpy(mask, bitmap, static_cast<size_t>(std::min(masklength, bitmaplength)));
}

void picture::bwrite(fstream fp)
{
	int stuff[8];
	char * buf;
	stuff[0] = 0x12AAB175;
	stuff[1] = 0;
	stuff[2] = width;
	stuff[3] = height;
	stuff[4] = depth;
	stuff[5] = greyscalemask?8:1;
	stuff[6] = bitmaplength;
	stuff[7] = masklength;
	const int totalLength = 32 + bitmaplength + masklength;
	buf = new char[static_cast<size_t>(totalLength)];
	memcpy(buf, reinterpret_cast<const char *>(stuff), 32);
	memcpy(buf+32, bitmap, static_cast<size_t>(bitmaplength));
	memcpy(buf+32+bitmaplength, mask, static_cast<size_t>(masklength));
	fp.write(buf, stream_size_from_nonnegative_int(totalLength));
	delete [] buf;
	/* fp.write(reinterpret_cast<char *>(stuff), 32); */
	/* fp.write(bitmap, bitmaplength); */
	/* fp.write(mask, masklength); */
}

void picture::bread(fstream fp)
{
	int stuff[8];
	fp.read(reinterpret_cast<char *>(stuff), 32);
	if (stuff[0] == 0x12AAB175) {
		if (bitmaplength) { delete [] bitmap; }
		if (masklength) { delete [] mask; }
		width = stuff[2];
		height = stuff[3];
		depth = stuff[4];
		greyscalemask = (stuff[5] == 8);
		bitmaplength = stuff[6];
		masklength = stuff[7];
		bitmap = new char[static_cast<size_t>(bitmaplength)];
		mask = new char[static_cast<size_t>(masklength)];
		fp.read(bitmap, stream_size_from_nonnegative_int(bitmaplength));
		fp.read(mask, stream_size_from_nonnegative_int(masklength));
		
		rowlength = __bitmap_row_width(width,height,depth);
		maskrowlength = __bitmap_row_width(width,height,greyscalemask?8:1);
	}
}

void picture::writefile(char * fn)
{
	fstream fp(fn, ios::out|ios::binary|ios::app);
	int stuff[8];
	char * buf;
	stuff[0] = 0x12AAB175;
	stuff[1] = 0;
	stuff[2] = width;
	stuff[3] = height;
	stuff[4] = depth;
	stuff[5] = greyscalemask?8:1;
	stuff[6] = bitmaplength;
	stuff[7] = masklength;
	const int totalLength = 32 + bitmaplength + masklength;
	buf = new char[static_cast<size_t>(totalLength)];
	memcpy(buf, reinterpret_cast<const char *>(stuff), 32);
	memcpy(buf+32, bitmap, static_cast<size_t>(bitmaplength));
	memcpy(buf+32+bitmaplength, mask, static_cast<size_t>(masklength));
	fp.write(buf, stream_size_from_nonnegative_int(totalLength));
	delete [] buf;
	/* fp.write(reinterpret_cast<char *>(stuff), 32); */
	/* fp.write(bitmap, bitmaplength); */
	/* fp.write(mask, masklength); */
	fp.close();
}


void picture::writebitmapandmasktopbm(const char * fn)
{
	fstream fp(fn, ios::out|ios::binary|ios::trunc);
	char		str[256];
	
	// Write PBM Header:
	snprintf( str, sizeof(str), "P4\n%d %d\n", width, height );
	fp.write( str, static_cast<std::streamsize>(strlen(str)) );
	
	fp.write( bitmap, stream_size_from_nonnegative_int(bitmaplength) );
	
	// Write second PBM Header:
	snprintf( str, sizeof(str), "\nP4\n%d %d\n", width, height );
	fp.write( str, static_cast<std::streamsize>(strlen(str)) );
	
	fp.write( mask, stream_size_from_nonnegative_int(masklength) );
	fp.close();
}


void picture::writebitmaptopbm(const char * fn)
{
	fstream fp(fn, ios::out|ios::binary|ios::trunc);
	char		str[256];
	
	// Write PBM Header:
	snprintf( str, sizeof(str), "P4\n%d %d\n", width, height );
	fp.write( str, static_cast<std::streamsize>(strlen(str)) );
	
	fp.write( bitmap, stream_size_from_nonnegative_int(bitmaplength) );
	fp.close();
}


void picture::writemasktopbm(const char * fn)
{
	fstream fp(fn, ios::out|ios::binary|ios::trunc);
	char		str[256];
	
	// Write PBM Header:
	snprintf( str, sizeof(str), "P4\n%d %d\n", width, height );
	fp.write( str, static_cast<std::streamsize>(strlen(str)) );
	
	fp.write( mask, stream_size_from_nonnegative_int(masklength) );
	fp.close();
}


void picture::readfile(char * fn)
{
	fstream fp(fn, ios::binary|ios::in);
	int stuff[8];
	fp.read(reinterpret_cast<char *>(stuff), 32);
	if (stuff[0] == 0x12AAB175) {
		if (bitmaplength) { delete [] bitmap; }
		if (masklength) { delete [] mask; }
		width = stuff[2];
		height = stuff[3];
		depth = stuff[4];
		greyscalemask = (stuff[5] == 8);
		bitmaplength = stuff[6];
		masklength = stuff[7];
		bitmap = new char[static_cast<size_t>(bitmaplength)];
		mask = new char[static_cast<size_t>(masklength)];
		fp.read(bitmap, stream_size_from_nonnegative_int(bitmaplength));
		fp.read(mask, stream_size_from_nonnegative_int(masklength));
		
		rowlength = __bitmap_row_width(width,height,depth);
		maskrowlength = __bitmap_row_width(width,height,greyscalemask?8:1);
	}
	fp.close();
}
