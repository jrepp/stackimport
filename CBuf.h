/*
 *  CBuf.h
 *  stackimport
 *
 *  Created by Mr. Z. on 03/31/10.
 *  Copyright 2010 Mr Z. All rights reserved.
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "stackimport_c.h"
#include "stackimport_logging.h"


struct shared_buffer
{
	char*		mBuffer;
	size_t		mSize;
	int			mRefCount;
	stackimport_deallocate_fn mDeallocate;
	void*		mAllocatorUserData;
};


class CBuf
{
public:
	explicit CBuf( size_t inSize = 0 );
	CBuf( const CBuf& inTemplate, size_t startOffs = 0, size_t amount = SIZE_MAX ); 
	~CBuf();
	
	void			resize( size_t inSize );
	
	void			memcpy( size_t toOffs, const char* fromPtr, size_t fromOffs, size_t amount );
	void			memcpy( size_t toOffs, const CBuf& fromPtr, size_t fromOffs = 0, size_t amount = SIZE_MAX );
	
	char			operator [] ( int idx ) const;
	char&			operator [] ( int idx );
	char			operator [] ( size_t idx ) const;
	char&			operator [] ( size_t idx );
	
	char*			buf( size_t offs = 0, size_t amount = SIZE_MAX );
	const char*		buf( size_t offs = 0, size_t amount = SIZE_MAX ) const;
	char*			checked_buf( size_t offs = 0, size_t amount = SIZE_MAX );
	const char*		checked_buf( size_t offs = 0, size_t amount = SIZE_MAX ) const;
	
	void			xornstr( size_t dstOffs, const char * src, size_t srcOffs, size_t amount );
	void			xornstr( size_t dstOffs, const CBuf& src, size_t srcOffs, size_t amount );

	void			shiftnstr( size_t dstOffs, size_t amount, int shiftAmount );
	
	size_t			size() const					{ return mShared->mSize; }
	
	int16_t			int16at( size_t offs ) const	{ int16_t value = 0; ::memcpy( &value, buf(offs,sizeof(value)), sizeof(value) ); return value; }
	int32_t			int32at( size_t offs ) const	{ int32_t value = 0; ::memcpy( &value, buf(offs,sizeof(value)), sizeof(value) ); return value; }

	uint16_t		uint16at( size_t offs ) const	{ uint16_t value = 0; ::memcpy( &value, buf(offs,sizeof(value)), sizeof(value) ); return value; }
	uint32_t		uint32at( size_t offs ) const	{ uint32_t value = 0; ::memcpy( &value, buf(offs,sizeof(value)), sizeof(value) ); return value; }
	
	bool			hasdata( size_t offs, size_t amount ) const	{ return (mShared->mBuffer != nullptr) && offs <= mShared->mSize && amount <= (mShared->mSize -offs); }
	
	void			tofile( const std::string& fpath );
	
	void			debug_print()		{ if( !mShared ) stackimport_quill_log_message(STACKIMPORT_MESSAGE_INFO, "NULL"); else { stackimport_quill_logf(STACKIMPORT_MESSAGE_INFO, "CBuf %p { size = %zu, refCount = %d, \"%-*s\" }", static_cast<const void*>(this), mShared->mSize, mShared->mRefCount, static_cast<int>(mShared->mSize), mShared->mBuffer ); } }
	
	CBuf&			operator = ( const CBuf& inTemplate );

protected:
	void			alloc_buffer( size_t amount );
	void			release_buffer();
	bool			make_buffer_exclusive();

protected:
	shared_buffer*	mShared;
};
