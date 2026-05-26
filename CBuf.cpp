/*
 *  CBuf.cpp
 *  stackimport
 *
 *  Created by Mr. Z. on 03/31/10.
 *  Copyright 2010 Mr Z. All rights reserved.
 *
 */

#include <cstring>
#include <cstdlib>
#include <new>
#include <climits>
#include "CBuf.h"
#include "byteutils.h"
#include "stackimport_platform_internal.h"
#include "assert.h"

namespace {

shared_buffer* failed_buffer()
{
	static shared_buffer buffer = { nullptr, 0, INT_MAX, nullptr, nullptr };
	return &buffer;
}

}


CBuf::CBuf( size_t inSize )
	: mShared(nullptr)
{
	alloc_buffer( inSize );
}


CBuf::CBuf( const CBuf& inTemplate, size_t startOffs, size_t amount )
	: mShared(nullptr)
{
	if( amount == SIZE_MAX )
		amount = startOffs <= inTemplate.size() ? inTemplate.size() -startOffs : 0;
	if( !inTemplate.hasdata( startOffs, amount ) )
		amount = 0;
	
	if( startOffs == 0 && amount == inTemplate.size() )
	{
		mShared = inTemplate.mShared;
		mShared->mRefCount++;
	}
	else
	{
		alloc_buffer( amount );
		if( amount > 0 )
			::memcpy( mShared->mBuffer, inTemplate.buf(startOffs, amount), amount );
	}
}


CBuf::~CBuf()
{
	release_buffer();
}


void	CBuf::alloc_buffer( size_t amount )
{
	mShared = static_cast<shared_buffer*>( stackimport_internal_allocate( sizeof(shared_buffer), alignof(shared_buffer) ) );
	if( !mShared )
	{
		stackimport_internal_note_allocation_failure();
		mShared = failed_buffer();
		return;
	}
	*mShared = {};
	const auto& platform = stackimport_current_internal_platform();
	mShared->mDeallocate = platform.deallocate;
	mShared->mAllocatorUserData = platform.user_data;
	if( amount > 0 )
	{
		mShared->mBuffer = static_cast<char*>( stackimport_internal_allocate( amount, alignof(char) ) );
		if( !mShared->mBuffer )
		{
			stackimport_internal_note_allocation_failure();
			stackimport_internal_deallocate( mShared, mShared->mDeallocate, mShared->mAllocatorUserData );
			mShared = failed_buffer();
			return;
		}
	}
	mShared->mSize = amount;
	mShared->mRefCount = 1;
}


void	CBuf::release_buffer()
{
	if( !mShared )
		return;
	if( mShared->mDeallocate == nullptr )
	{
		mShared = nullptr;
		return;
	}
	if( mShared->mRefCount > 1 )
		mShared->mRefCount--;
	else
	{
		if( mShared->mBuffer )
			stackimport_internal_deallocate( mShared->mBuffer, mShared->mDeallocate, mShared->mAllocatorUserData );
		mShared->mBuffer = nullptr;
		mShared->mSize = 0;
		stackimport_internal_deallocate( mShared, mShared->mDeallocate, mShared->mAllocatorUserData );
	}
	mShared = nullptr;
}


bool	CBuf::make_buffer_exclusive()
{
	if( mShared->mRefCount == 1 )
		return true;	// Already are exclusive owner.
	
	shared_buffer*	oldBuffer = mShared;
	alloc_buffer( oldBuffer->mSize );
	if( oldBuffer->mSize > 0 && !mShared->mBuffer )
	{
		mShared = oldBuffer;
		return false;
	}
	if( oldBuffer->mSize > 0 )
		::memmove( mShared->mBuffer, oldBuffer->mBuffer, oldBuffer->mSize );
	oldBuffer->mRefCount --;
	return true;
}


void	CBuf::resize( size_t inSize )
{
	if( mShared->mRefCount == 1 )
	{
		char* newBuffer = nullptr;
		if( inSize > 0 )
		{
			newBuffer = static_cast<char*>( stackimport_internal_allocate( inSize, alignof(char) ) );
			if( !newBuffer )
			{
				stackimport_internal_note_allocation_failure();
				return;
			}
		}
		if( mShared->mBuffer )
			stackimport_internal_deallocate( mShared->mBuffer, mShared->mDeallocate, mShared->mAllocatorUserData );
		mShared->mBuffer = newBuffer;
		mShared->mSize = inSize;
	}
	else
	{
		shared_buffer*	oldBuffer = mShared;
		alloc_buffer( inSize );
		if( mShared->mDeallocate == nullptr || (inSize > 0 && !mShared->mBuffer) )
		{
			mShared = oldBuffer;
			return;
		}
		oldBuffer->mRefCount--;
	}
}

void	CBuf::memcpy( size_t toOffs, const char* fromPtr, size_t fromOffs, size_t amount )
{
	if( !make_buffer_exclusive() )
		return;
	
	char*		thePtr = mShared->mBuffer;
	if( !fromPtr || toOffs > mShared->mSize || amount > (mShared->mSize -toOffs) )
		return;
	
	::memcpy( thePtr + toOffs, fromPtr +fromOffs, amount );
}


void	CBuf::memcpy( size_t toOffs, const CBuf& fromPtr, size_t fromOffs, size_t amount )
{
	if( amount == SIZE_MAX )
		amount = fromOffs <= fromPtr.size() ? fromPtr.size() -fromOffs : 0;
	if( !fromPtr.hasdata( fromOffs, amount ) )
		return;
	memcpy( toOffs, fromPtr.buf(fromOffs,amount), 0, amount );
}


char CBuf::operator [] ( int idx ) const
{
	if( idx < 0 || static_cast<size_t>(idx) >= mShared->mSize )
		return 0;
	return mShared->mBuffer[static_cast<size_t>(idx)];
}


char& CBuf::operator [] ( int idx )
{
	static char		dummy[2048] = {0};
	if( idx < 0 || static_cast<size_t>(idx) >= mShared->mSize )
		return dummy[0];
	
	if( !make_buffer_exclusive() )
		return dummy[0];
	return mShared->mBuffer[static_cast<size_t>(idx)];
}


char CBuf::operator [] ( size_t idx ) const
{
	if( idx >= mShared->mSize )
		return 0;
	return mShared->mBuffer[idx];
}


char& CBuf::operator [] ( size_t idx )
{
	static char		dummy[2048] = {0};
	if( idx >= mShared->mSize )
		return dummy[0];
	
	if( !make_buffer_exclusive() )
		return dummy[0];
	return mShared->mBuffer[idx];
}


char*	CBuf::buf( size_t offs, size_t amount )
{
	if( amount == SIZE_MAX )
		amount = offs <= mShared->mSize ? mShared->mSize -offs : 0;
	static char		dummy[2048] = {0};
	if( amount == 0 )
		return dummy;
	if( !hasdata( offs, amount ) )
		return dummy;
	assert( mShared->mBuffer != nullptr );
	
	if( !make_buffer_exclusive() )
		return dummy;
	return mShared->mBuffer + offs;
}


const char*	CBuf::buf( size_t offs, size_t amount ) const
{
	static char		dummy[2048] = {0};
	if( amount == SIZE_MAX )
		amount = offs <= mShared->mSize ? mShared->mSize -offs : 0;
	if( amount == 0 )
		return dummy;
	if( !hasdata( offs, amount ) )
		return dummy;
	assert( mShared->mBuffer != nullptr );
	
	return mShared->mBuffer + offs;
}


void	CBuf::xornstr( size_t dstOffs, const char * src, size_t srcOffs, size_t amount )
{
	if( amount == 0 )
		return;
	if( !src || !hasdata( dstOffs, amount ) )
		return;
	assert( mShared->mBuffer != nullptr);
	assert( (amount +dstOffs) <= mShared->mSize );
	if( !make_buffer_exclusive() )
		return;
	::xornstr( mShared->mBuffer +dstOffs, src +srcOffs, amount );
}


void	CBuf::xornstr( size_t dstOffs, const CBuf& src, size_t srcOffs, size_t amount )
{
	if( amount == 0 )
		return;
	if( !hasdata( dstOffs, amount ) || !src.hasdata( srcOffs, amount ) )
		return;
	assert( mShared->mBuffer != nullptr );
	assert( (amount +dstOffs) <= mShared->mSize );
	if( !make_buffer_exclusive() )
		return;
	::xornstr( mShared->mBuffer +dstOffs, src.buf(srcOffs,amount), amount );
}


void	CBuf::shiftnstr( size_t dstOffs, size_t amount, int shiftAmount )
{
	if( amount == 0 )
		return;
	if( dstOffs > mShared->mSize || amount > (mShared->mSize -dstOffs) )
		return;
	assert( mShared->mBuffer != nullptr );
	assert( (dstOffs +amount) <= mShared->mSize );
	if( !make_buffer_exclusive() )
		return;
	::shiftnstr( mShared->mBuffer +dstOffs, amount, shiftAmount );
}


void	CBuf::tofile( const std::string& fpath )
{
	stackimport_file_handle theFile = stackimport_internal_open_file( fpath.c_str(), "wb" );
	if( !theFile )
		return;
	if( mShared->mSize > 0 && stackimport_internal_write_file( theFile, mShared->mBuffer, mShared->mSize ) != mShared->mSize )
		stackimport_internal_message(STACKIMPORT_MESSAGE_WARNING, "Warning: Short write while writing buffer.");
	stackimport_internal_close_file( theFile );
}


CBuf&	CBuf::operator = ( const CBuf& inTemplate )
{
	if( mShared != inTemplate.mShared )
	{
		release_buffer();
		mShared = inTemplate.mShared;
		mShared->mRefCount++;
	}
	
	return *this;
}
