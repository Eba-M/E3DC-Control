#ifndef __RSCP_TYPES_H_
#define __RSCP_TYPES_H_

#include <vector>
#include <stdint.h>

#define RSCP_MAX_FRAME_LENGTH       (sizeof(SRscpFrameHeader) + 0xFFFF + sizeof(SRscpFrame::CRC))

namespace RSCP {
const uint16_t	MAGIC	= 0xDCE3;
const uint8_t	VERSION	= 0x01;

enum eRscpTagTypes {
	eRequest        	= 0,
	eResponse       	= 1
};

enum eRscpDataType {
	eTypeNone			= 0,
	eTypeBool			= 1,
	eTypeChar8			= 2,
	eTypeUChar8			= 3,
	eTypeInt16			= 4,
	eTypeUInt16			= 5,
	eTypeInt32			= 6,
	eTypeUInt32			= 7,
	eTypeInt64			= 8,
	eTypeUInt64			= 9,
	eTypeFloat32		= 10,
	eTypeDouble64		= 11,
	eTypeBitfield		= 12,
	eTypeString			= 13,
	eTypeContainer		= 14,
	eTypeTimestamp		= 15,
	eTypeByteArray		= 16,
	eTypeError			= 255
};

enum eRscpReturnCodes {
	OK							= 0,
	ERR_INVALID_INPUT			= -1,
	ERR_NO_MEMORY				= -2,
	ERR_INVALID_MAGIC			= -3,
	ERR_PROT_VERSION_MISMATCH	= -4,
	ERR_INVALID_FRAME_LENGTH	= -5,
	ERR_INVALID_CRC				= -6,
	ERR_DATA_LIMIT_EXCEEDED		= -7
};
}

enum eRscpErrorCodes {
	RSCP_ERR_NOT_HANDLED    = 0x01,
	RSCP_ERR_ACCESS_DENIED  = 0x02,
	RSCP_ERR_FORMAT         = 0x03,
	RSCP_ERR_AGAIN          = 0x04,
	RSCP_ERR_OUT_OF_BOUNDS  = 0x05,
	RSCP_ERR_NOT_AVAILABLE  = 0x06,
	RSCP_ERR_UNKNOWN_TAG    = 0x07,
	RSCP_ERR_ALREADY_IN_USE = 0x08
};

union SRscpControl {
	struct {
		uint8_t reserved_2 : 8;
		uint8_t version : 4;
		uint8_t crc : 1;
		uint8_t reserved_1 : 3;
	} bits;
	uint16_t value;
} __attribute__((packed));

struct SRscpTimestamp {
	uint64_t seconds;
	uint32_t nanoseconds;
} __attribute__((packed));

typedef uint32_t SRscpTag;

struct SRscpValue {
	union  {
		struct {
#ifdef __BIG_ENDIAN__
			uint8_t nameSpace : 8;
			uint8_t tagType : 1;
			uint32_t tagSpace : 23;
#else
			uint32_t tagSpace : 23;
			uint8_t tagType : 1;
			uint8_t nameSpace : 8;
#endif
		} tagbits;
		SRscpTag tag;
	};
	uint8_t dataType;
	uint16_t length;
	uint8_t* data;
}  __attribute__((packed));

struct SRscpFrameHeader {
	uint16_t magic;
	SRscpControl ctrl;
	SRscpTimestamp timestamp;
	uint16_t dataLength;
} __attribute__((packed));

struct SRscpFrame {
	SRscpFrameHeader header;
	std::vector<SRscpValue> data;
	uint32_t CRC;
};

struct SRscpFrameBuffer {
	uint8_t *data;
	uint32_t dataLength;
};


#endif
