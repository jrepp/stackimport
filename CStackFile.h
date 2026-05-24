/*
 *  CStackFile.h
 *  stackimport
 *
 *  Created by Mr. Z. on 10/06/06.
 *  Copyright 2006 Mr Z. All rights reserved.
 *
 */

// If you're not compiling for a platform where the Mac resource manager is
//	available, set the following to 0 to remove that code from compilation:
#ifndef MAC_CODE
#define MAC_CODE		1
#endif

// If you're compiling for 64 bit, you don't have access to QuickTime, which
//	we use to create AIFF files from 'snd ' resources. So turn this off.
#define USE_QUICKTIME	0


#include <map>
#include <vector>
#include <cstdint>
#include "CBuf.h"

#if MAC_CODE
#include <Carbon/Carbon.h>
#if USE_QUICKTIME
#include <QuickTime/QuickTime.h>
#endif
#endif
#include <cstring>

#include "include/stackimport_sax.hpp"


class CStackBlockIdentifier
{
public:
	char		mType[5];
	int32_t		mID;
	bool		mIsWildcard;

	CStackBlockIdentifier( const char* inType, int32_t inID ) : mID(inID), mIsWildcard(false) 	{ size_t len = strlen(inType); if(len > 4) len = 4; memmove( mType, inType, len ); mType[len] = 0; }
	CStackBlockIdentifier( const char* inType ) : mID(0), mIsWildcard(true) 					{ size_t len = strlen(inType); if(len > 4) len = 4; memmove( mType, inType, len ); mType[len] = 0; }
	virtual ~CStackBlockIdentifier()		= default;
	
	virtual bool operator == ( const CStackBlockIdentifier& inOther ) const
	{
		if( strcmp( mType, inOther.mType ) != 0 )
			return false;
		if( mID != inOther.mID && !mIsWildcard && !inOther.mIsWildcard )
			return false;
		
		return true;
	}
	
	virtual bool operator != ( const CStackBlockIdentifier& inOther ) const
	{
		if( strcmp( mType, inOther.mType ) == 0 )
		{
			if( mID == inOther.mID || mIsWildcard || inOther.mIsWildcard )
				return false;
		}
		
		return true;
	}
	
	virtual bool operator > ( const CStackBlockIdentifier& inOther ) const
	{
		int		cmpResult = strcmp( mType, inOther.mType );
		if( cmpResult < 0 )
			return false;
		if( cmpResult > 0 )
			return true;
		
		if( mID <= inOther.mID || mIsWildcard || inOther.mIsWildcard )
			return false;
		
		return true;
	}

	virtual bool operator < ( const CStackBlockIdentifier& inOther ) const
	{
		int		cmpResult = strcmp( mType, inOther.mType );
		if( cmpResult > 0 )
			return false;
		if( cmpResult < 0 )
			return true;
		if( mID >= inOther.mID || mIsWildcard || inOther.mIsWildcard )
			return false;
		
		return true;
	}
};


typedef std::map<CStackBlockIdentifier,CBuf> 	CBlockMap;
typedef std::map<int32_t,std::vector<int32_t> >	CBtnIDsPerBgMap;
typedef std::map<int16_t,std::string>			CFontTable;

struct CLayerSummary
{
	bool		isCard;
	int32_t		id;
	int32_t		owner;
	uint8_t		flags;
	std::string	file;
	std::string	name;
	int16_t		partCount;
	int16_t		contentCount;
	size_t		scriptBytes;
	size_t		textBytes;
};

struct CPageSummary
{
	int32_t					id;
	int16_t					entryCount;
	std::vector<int32_t>	cardIDs;
};

struct CSourceBlockSummary
{
	std::string	type;
	int32_t		id;
	uint64_t	offset;
	uint32_t	size;
	uint64_t	payloadOffset;
	uint32_t	payloadBytes;
	std::string	status;
};

struct CResourceSummary
{
	std::string	type;
	int32_t		id;
	uint32_t	flags;
	std::string	name;
	size_t		bytes;
	std::string	status;
	std::string	disassemblyFile;
	std::string	architecture;
	std::string	outputFile;
};

class CStyleEntry
{
public:
	int16_t		mStyleID;
	int16_t		mFontID;
	std::string	mFontName;
	bool		mGroup;
	bool		mExtend;
	bool		mCondense;
	bool		mShadow;
	bool		mOutline;
	bool		mUnderline;
	bool		mItalic;
	bool		mBold;
	int16_t		mFontSize;
	
	CStyleEntry() : mStyleID(0), mFontID(0), mGroup(false), mExtend(false), 
		mCondense(false), mShadow(false), mOutline(false), mUnderline(false),
		mItalic(false), mBold(false), mFontSize(12)	{}
};


class CStackFile
{
public:
	CStackFile();
	
	bool	LoadFile( const std::string& fpath, const std::string& outputPackagePath );
	
	void	SetDumpRawBlockData( bool inDumpToFiles ) 			{ mDumpRawBlockData = inDumpToFiles; }
	void	SetStatusMessages( bool inPrintStatusMessages ) 	{ mStatusMessages = inPrintStatusMessages; }
	void	SetProgressMessages( bool inPrintProgressMessages ) { mProgressMessages = inPrintProgressMessages; }
	void	SetDecodeGraphics( bool inDecodeGraphics ) 			{ mDecodeGraphics = inDecodeGraphics; }
	void	SetResourceOutput( stackimport::IResourceOutput* output ) { mResourceOutput = output; }

protected:
	bool	LoadStackBlock( int32_t stackID, CBuf& blockData );
	bool	LoadListBlock( CBuf& blockData );
	bool	LoadPageTable( int32_t blockID, CBuf& blockData, int16_t pageEntryCount );
	bool	LoadFontTable( int32_t blockID, CBuf& blockData );
	bool	LoadStyleTable( int32_t blockID, CBuf& blockData );
	bool	LoadMasterBlock( int32_t blockID, CBuf& blockData );
	bool	LoadPrintBlock( int32_t blockID, CBuf& blockData );
	bool	LoadPageSetupBlock( int32_t blockID, CBuf& blockData );
	bool	LoadReportTemplateBlock( int32_t blockID, CBuf& blockData );
	bool	LoadLayerBlock( const char* vBlockType, int32_t blockID, CBuf& blockData, uint8_t inFlags );	// Card or Bkgd.
	std::string	OutputPath( const char* fileName ) const;
	bool	WriteSourceManifest( uint64_t dataForkBytes, const char* streamStatus ) const;
	bool	LoadResourceFork( const std::string& fpath );
	bool	WriteJsonIndexes() const;
	
#if MAC_CODE
	bool	LoadBWIcons();
	bool	LoadPictures();
	bool	LoadCursors();
	bool	LoadSounds();
	bool	Load68000Resources();
	bool	LoadPowerPCResources();
#endif //MAC_CODE

protected:
	bool			mDumpRawBlockData;	// Create .data files with the contents of each block.
	bool			mStatusMessages;	// Output "Status: blah" messages to stdout.
	bool			mProgressMessages;	// Output "Progress: 1 of N" messages to stdout.
	bool			mDecodeGraphics;	// WOBA-decode the graphics into readable PBM, don't just dump out the raw compressed data.
	stackimport::IResourceOutput*	mResourceOutput;	// Optional streaming sink for native and converted resource payloads.
	int32_t			mListBlockID;		// ID of the LIST block, read from STAK block.
	int32_t			mFontTableBlockID;	// ID of the FTBL block, read from STAK block.
	int32_t			mStyleTableBlockID;	// ID of the STBL block, read from STAK block.
	int32_t			mStackID;
	int32_t			mStackCardCount;
	int32_t			mFirstCardID;
	int16_t			mUserLevel;
	int16_t			mCardWidth;
	int16_t			mCardHeight;
	bool			mStackCantModify;
	bool			mStackCantDelete;
	bool			mStackPrivateAccess;
	bool			mStackCantAbort;
	bool			mStackCantPeek;
	std::string		mCreatedByVersion;
	std::string		mLastCompactedVersion;
	std::string		mLastEditedVersion;
	std::string		mFirstEditedVersion;
	std::string		mStackScript;
	int32_t			mCardBlockSize;		// Size of the card entries in PAGE blocks, read from LIST block.
	CBlockMap		mBlockMap;			// Associative map of type/id -> block data mappings for random access to blocks when actually parsing their contents.
	std::vector<CSourceBlockSummary>	mSourceBlocks;
	std::vector<CResourceSummary>	mResourceSummaries;
	std::string		mResourceForkStatus;
	uint64_t		mResourceForkBytes;
	int				mCurrentProgress;	// Current value for progress output.
	int				mMaxProgress;		// Maximum value for progress output.
	CBtnIDsPerBgMap	mButtonIDsPerBg;	// Table that holds the IDs of all BG buttons on each background. Used to detect what card-level button contents entries are actually sharedHighlight entries for a bg button.
	std::vector<CLayerSummary>	mLayerSummaries;
	std::vector<CPageSummary>	mPageSummaries;
	std::string		mBasePath;			// Path to package folder, in which we'll put JSON files and graphics'n stuff.
	std::string		mFileName;			// Name of the original stack file, w/o the path.
	std::string		mStyleSheetName;	// Name of CSS file containing our styles table.
	CFontTable		mFontTable;			// Actual, parsed font ID -> name mappings from FTBL block.
#if MAC_CODE
	ResFileRefNum	mResRefNum;
#endif
	std::map<int16_t,CStyleEntry>	mStyles;

public:
	class CStackBlockOutput : public stackimport::IBlockOutput {
	public:
		CStackBlockOutput(CBlockMap& block_map,
		                 std::vector<CSourceBlockSummary>& source_blocks,
		                 std::string& source_stream_status)
			: block_map_(block_map)
			, source_blocks_(source_blocks)
			, source_stream_status_(source_stream_status)
			, position_(0)
			, num_blocks_(0)
			, stopped_(false)
		{}

		auto on_block(const stackimport::BlockRef& block,
		              stackimport::IStackReader& reader) -> bool override {
			if (stopped_) return false;

			if (block.payload_bytes > 0) {
				CBuf value(block.payload_bytes);
				size_t r = reader.read(reinterpret_cast<uint8_t*>(value.buf()),
				                      block.payload_bytes);
				if (r != block.payload_bytes) {
					source_stream_status_ = "truncated_payload";
					stopped_ = true;
					return false;
				}

				CStackBlockIdentifier key(reinterpret_cast<const char*>(block.type.v),
				                         block.id.get());
				block_map_[key] = value;
			}

			CSourceBlockSummary summary;
			summary.type.assign(4, '\0');
			std::memcpy(summary.type.data(), block.type.v, 4);
			summary.id = block.id.get();
			summary.offset = position_;
			summary.size = block.size();
			summary.payloadOffset = position_ + 12;
			summary.payloadBytes = block.payload_bytes;
			summary.status = "ok";
			source_blocks_.push_back(summary);

			position_ += 12 + block.payload_bytes;
			num_blocks_++;

			return true;
		}

		auto on_error(const char* msg) -> bool override {
			source_stream_status_ = msg;
			stopped_ = true;
			return false;
		}

		auto num_blocks() const -> int { return num_blocks_; }
		auto stopped() const -> bool { return stopped_; }

	private:
		CBlockMap& block_map_;
		std::vector<CSourceBlockSummary>& source_blocks_;
		std::string& source_stream_status_;
		uint64_t position_;
		int num_blocks_;
		bool stopped_;
	};
};
