/*
 * RscpProtocol.cpp
 *
 *  Created on: 19.05.2014
 *      Author: dikirile
 */

#include <stdlib.h>
#include <sys/time.h>
#ifdef WINNT
#include <windows.h>
#endif
#include "RscpProtocol.h"


RscpProtocol::RscpProtocol() {
}

RscpProtocol::~RscpProtocol() {
}

bool RscpProtocol::setHeaderTimestamp(SRscpFrame *frame) {
	// sanity check
	if(frame == NULL) {
		return false;
	}

	bool bTimeSet = false;
	// get linux timestamp
	struct timeval timeVal;
	gettimeofday(&timeVal, NULL);
	// set frame timestamp
	frame->header.timestamp.seconds = timeVal.tv_sec;
	frame->header.timestamp.nanoseconds = timeVal.tv_usec * 1000;

	return bTimeSet;
}

uint32_t RscpProtocol::calculateCRC32(const uint8_t *data, uint16_t length) {
    static const uint32_t crc_table[] = {
      0x4DBDF21C, 0x500AE278, 0x76D3D2D4, 0x6B64C2B0,
      0x3B61B38C, 0x26D6A3E8, 0x000F9344, 0x1DB88320,
      0xA005713C, 0xBDB26158, 0x9B6B51F4, 0x86DC4190,
      0xD6D930AC, 0xCB6E20C8, 0xEDB71064, 0xF0000000
    };
	uint32_t crc = 0;
    for(uint32_t n = 0; n < length; n++) {
        crc = (crc >> 4) ^ crc_table[(crc ^ (data[n] >> 0)) & 0x0F];  /* lower nibble */
        crc = (crc >> 4) ^ crc_table[(crc ^ (data[n] >> 4)) & 0x0F];  /* upper nibble */
	}
	return crc;
}

int32_t RscpProtocol::getFrameLength(const uint8_t * data, const uint32_t & length) {
	// validate input
	if(data == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// first the length must at least be of the header size
	if(length < sizeof(SRscpFrameHeader)) {
		return RSCP::ERR_INVALID_FRAME_LENGTH;
	}
	// check the header information for length
	SRscpFrameHeader *header = reinterpret_cast<SRscpFrameHeader *> ((uint8_t*)data);

	if(header->magic != RSCP::MAGIC) {
		return RSCP::ERR_INVALID_MAGIC;
	}
	// check the frame version number
	if(header->ctrl.bits.version != RSCP::VERSION) {
		return RSCP::ERR_PROT_VERSION_MISMATCH;
	}
	// calculate the expected frame length
	int32_t frameLength = sizeof(SRscpFrameHeader) + header->dataLength + ((header->ctrl.bits.crc != 0) ? sizeof(uint32_t) : 0);
	return frameLength;
}

int32_t RscpProtocol::createFrameAsBuffer(SRscpFrameBuffer* frameBuffer, const uint8_t * data, uint16_t dataLength, bool calcCRC) {
	if(frameBuffer == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// calculate the required frame size
	size_t sFrameSize =  sizeof(SRscpFrameHeader);
	// allocate the required memory
	frameBuffer->data = (uint8_t *) malloc(sFrameSize + dataLength + (calcCRC ? 4 : 0));
	if(frameBuffer->data == NULL) {
		return RSCP::ERR_NO_MEMORY;
	}
	// set the memory size
	frameBuffer->dataLength = sFrameSize + dataLength + (calcCRC ? 4 : 0);
	// set initial header values
	memset(frameBuffer->data, 0, sizeof(SRscpFrameHeader));
	SRscpFrame* tmpFrame = reinterpret_cast<SRscpFrame*>(frameBuffer->data);
	tmpFrame->header.magic = RSCP::MAGIC;
	tmpFrame->header.ctrl.bits.crc = calcCRC;
	tmpFrame->header.ctrl.bits.version = RSCP::VERSION;
	tmpFrame->header.dataLength = dataLength;
	setHeaderTimestamp(tmpFrame);

	// insert data from the SRscpValues
	memcpy(&tmpFrame->header + 1, data, dataLength);

	// calculate CRC if necessary and add to the frame
	if(calcCRC) {
		uint32_t uCRC32 = calculateCRC32(frameBuffer->data, frameBuffer->dataLength - sizeof(uint32_t));
		memcpy(&frameBuffer->data[frameBuffer->dataLength - sizeof(uCRC32)], &uCRC32, sizeof(uCRC32));
	}

	return RSCP::OK;
}

int32_t RscpProtocol::createFrameAsBuffer(SRscpFrameBuffer* frame, const SRscpValue & data, bool calcCRC) {
	// just overload the vector function
	return createFrameAsBuffer(frame, std::vector<SRscpValue>(1, data), calcCRC);
}

int32_t RscpProtocol::createFrameAsBuffer(SRscpFrameBuffer *frameBuffer, const std::vector<SRscpValue> & data, bool calcCRC) {
	if(frameBuffer == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}

	// calculate the required frame size
	size_t sFrameSize =  sizeof(SRscpFrameHeader);

	// calculate the complete data size
	size_t sDataSize = 0;
	for(size_t i = 0; i < data.size(); ++i) {
		sDataSize += sizeof(SRscpValue) - sizeof(data[i].data) + data[i].length;
	}

	// allocate the required memory
	frameBuffer->data = (uint8_t *) malloc(sFrameSize + sDataSize + (calcCRC ? 4 : 0));
	if(frameBuffer->data == NULL) {
		return RSCP::ERR_NO_MEMORY;
	}
	// set the memory size
	frameBuffer->dataLength = sFrameSize + sDataSize + (calcCRC ? 4 : 0);

	// set initial header values
	memset(frameBuffer->data, 0, sizeof(SRscpFrameHeader));
	SRscpFrame* tmpFrame = reinterpret_cast<SRscpFrame*> (frameBuffer->data);
	tmpFrame->header.magic = RSCP::MAGIC;
	tmpFrame->header.ctrl.bits.crc = calcCRC;
	tmpFrame->header.ctrl.bits.version = RSCP::VERSION;
	tmpFrame->header.dataLength = sDataSize;
	setHeaderTimestamp(tmpFrame);

	// insert data from the SRscpValues
	uint8_t *dataPtr = frameBuffer->data + sFrameSize;
	for(size_t i = 0; i < data.size(); ++i) {
		SRscpValue *value = reinterpret_cast<SRscpValue *>(dataPtr);
		value->tag = data[i].tag;
		value->dataType = data[i].dataType;
		value->length = data[i].length;
		if(value->length > 0) {
			// copy into the position of the data pointer and not into the data pointer itself as the data is appended
			memcpy(&value->data, data[i].data, data[i].length);
		}
		dataPtr += sizeof(SRscpValue) - sizeof(data[i].data) + data[i].length;
	}

	// calculate CRC if necessary and add to the frame
	if(calcCRC) {
		uint32_t uCRC32 = calculateCRC32(frameBuffer->data, frameBuffer->dataLength - sizeof(uint32_t));
		memcpy(frameBuffer->data + frameBuffer->dataLength - sizeof(uCRC32), &uCRC32, sizeof(uCRC32));
	}

	return RSCP::OK;
}

int32_t RscpProtocol::createFrameAsBuffer(SRscpFrameBuffer *frameBuffer, const SRscpFrame & frame, bool calcCRC) {

	if(frameBuffer == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}

	// calculate the required frame size
	size_t sFrameSize =  sizeof(SRscpFrameHeader);

	// calculate the complete data size
	size_t sDataSize = 0;
	for(size_t i = 0; i < frame.data.size(); ++i) {
		sDataSize += sizeof(SRscpValue) - sizeof(frame.data[i].data) + frame.data[i].length;
	}

	// allocate the required memory
	frameBuffer->data = (uint8_t *) malloc(sFrameSize + sDataSize + (calcCRC ? 4 : 0));
	if(frameBuffer->data == NULL) {
		return RSCP::ERR_NO_MEMORY;
	}
	// set the memory size
	frameBuffer->dataLength = sFrameSize + sDataSize + (calcCRC ? 4 : 0);

	// copy header information
	memcpy(frameBuffer->data, &frame.header, sFrameSize);

	// insert data from the SRscpValues
	uint8_t *dataPtr = frameBuffer->data + sFrameSize;
	for(size_t i = 0; i < frame.data.size(); ++i) {
		SRscpValue *value = reinterpret_cast<SRscpValue *>(dataPtr);
		value->tag = frame.data[i].tag;
		value->dataType = frame.data[i].dataType;
		value->length = frame.data[i].length;
		if(value->length > 0) {
			// copy into the position of the data pointer and not into the data pointer itself as the data is appended
			memcpy(&value->data, frame.data[i].data, frame.data[i].length);
		}
		dataPtr += sizeof(SRscpValue) - sizeof(frame.data[i].data) + frame.data[i].length;
	}

	// calculate CRC if necessary and add to the frame
	if(calcCRC) {
		uint32_t uCRC32 = calculateCRC32(frameBuffer->data, frameBuffer->dataLength - sizeof(uint32_t));
		memcpy(frameBuffer->data + frameBuffer->dataLength - sizeof(uCRC32), &uCRC32, sizeof(uCRC32));
	}

	return RSCP::OK;
}

int32_t RscpProtocol::createFrame(SRscpFrame* frame, const SRscpValue & data, bool calcCRC) {
	// just overload the vector function
	return createFrame(frame, std::vector<SRscpValue>(1, data), calcCRC);
}

int32_t RscpProtocol::createFrame(SRscpFrame* frame, const std::vector<SRscpValue> & data, bool calcCRC) {
	if(frame == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}

	// set frame edge values
	memset(&frame->header, 0, sizeof(frame->header));
	frame->header.magic = RSCP::MAGIC;
	frame->header.ctrl.bits.crc = calcCRC;
	frame->header.ctrl.bits.version = RSCP::VERSION;
	setHeaderTimestamp(frame);
	frame->data = data;

	// calculate the data length
	for(size_t i = 0; i < data.size(); ++i) {
		frame->header.dataLength += sizeof(SRscpValue) - sizeof(data[i].data) + data[i].length;
	}

	// in case the CRC has to be calculated we need to generate a new frame buffer which then calculates the CRC
	if(calcCRC) {
		SRscpFrameBuffer frameBuffer;
		int32_t iResult = createFrameAsBuffer(&frameBuffer, *frame, calcCRC);
		if(iResult != RSCP::OK) {
			return iResult;
		}
		// get the CRC from the frame buffer
		memcpy(&frame->CRC, &frameBuffer.data + frameBuffer.dataLength - sizeof(frame->CRC), sizeof(frame->CRC));
		// free the buffer data again
		destroyFrameData(frameBuffer);
	}
	return RSCP::OK;
}

bool RscpProtocol::allocateMemory(SRscpValue* value, size_t size)  {
	// sanity check
	if(value == NULL) {
		return false;
	}
	// if no data is allocated yet -> allocate full size
	if(value->data == NULL) {
		if(size == 0) {
			return true;
		}
		value->data = (uint8_t *) malloc(size);
		return (value->data != NULL);
	}
	else {
		// if data is already allocated -> reallocate to the correct size
		uint8_t *ucTmp = (uint8_t *) realloc(value->data, size);
		if(ucTmp != NULL) {
			value->data = ucTmp;
			return true;
		}
	}
	return false;
}

int32_t RscpProtocol::parseFrame(const uint8_t* data, const uint32_t & length, SRscpFrame* frame) {
	// sanity check
	if((data == NULL) || (frame == NULL)) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// check first that at least the header size is in the frame
	if(sizeof(SRscpFrameHeader) > length) {
		return RSCP::ERR_INVALID_FRAME_LENGTH;
	}
	// assign pointer to the data struct
	SRscpFrame* inFrame = reinterpret_cast<SRscpFrame*>((uint8_t*)data);
	// check if the magic matches
	if(inFrame->header.magic != RSCP::MAGIC) {
		return RSCP::ERR_INVALID_MAGIC;
	}
	// check the frame version number
	if(inFrame->header.ctrl.bits.version != RSCP::VERSION) {
		return RSCP::ERR_PROT_VERSION_MISMATCH;
	}
	// check the frame length
	uint32_t frameLength = sizeof(SRscpFrameHeader) + inFrame->header.dataLength + ((inFrame->header.ctrl.bits.crc != 0) ? sizeof(uint32_t) : 0);
	if(frameLength > length) {
		return RSCP::ERR_INVALID_FRAME_LENGTH;
	}
	// check that CRC matches before starting to parse
	if(inFrame->header.ctrl.bits.crc != 0) {
		uint32_t calcCRC32 = calculateCRC32(data, frameLength - sizeof(uint32_t));
		uint32_t frameCRC32;
		memcpy(&frameCRC32, data + frameLength - sizeof(uint32_t), sizeof(uint32_t));
		// compare CRC
		if(frameCRC32 != calcCRC32) {
			return RSCP::ERR_INVALID_CRC;
		}
		// CRC matches set the CRC inside the output frame
		frame->CRC = frameCRC32;
	}
	else {
		// no CRC inside the frame -> set 0 for output frame
		frame->CRC = 0;
	}
	// copy header information
	memcpy(&frame->header, &inFrame->header, sizeof(SRscpFrameHeader));
	// parse the SRscpValues
	int32_t iResult = parseData((uint8_t*)(&inFrame->header + 1), inFrame->header.dataLength, frame->data);
	if(iResult < 0) {
		return iResult;
	}
	// parsing done return OK
	frameLength = (sizeof(SRscpFrameHeader) + iResult + ((inFrame->header.ctrl.bits.crc != 0) ? sizeof(uint32_t) : 0));
	return frameLength;
}

int32_t RscpProtocol::parseData(const uint8_t* data, const uint32_t & length, std::vector<SRscpValue> & vecValues) {
	// sanity check
	if(data == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// start parsing
	uint32_t uiPos = 0;
	SRscpValue * value = reinterpret_cast<SRscpValue *>(((uint8_t*)data) + uiPos);

	// check the boundaries of the buffer
	while((uiPos < length) && ((uiPos + value->length) <= length)) {
		// parse the data
		SRscpValue newVal;
		newVal.tag = value->tag;
		newVal.dataType = value->dataType;
		newVal.length = value->length;
		if(value->length > 0) {
			// allocate data memory for each value separately
			newVal.data = (uint8_t *) malloc(value->length);
			if(newVal.data == NULL) {
				// not enough memory, return only what parsed until now
				destroyValueData(vecValues);
				return RSCP::ERR_NO_MEMORY;
			}
			memcpy(newVal.data, &value->data, value->length);
		}
		else {
			// set NULL pointer as no data is in this tag
			newVal.data = NULL;
		}
		//push new value to the return vector
		vecValues.push_back(newVal);
		// increment value pointer
		uiPos += sizeof(SRscpValue) - sizeof(newVal.data) + value->length;
		value =  reinterpret_cast<SRscpValue *>(((uint8_t*)data) + uiPos);
	}

	// return all collected values
	return uiPos;
}

std::string RscpProtocol::getValueAsString(const SRscpValue* value) {
	// sanity check
	std::string strValue;
	if((value == NULL) || (value->data == NULL)) {
		return strValue;
	}
	// extract only the bytes that are defined by the length
	strValue.assign((char*)value->data, value->length);
	return strValue;
}

int32_t RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t * data, const uint16_t & dataLength, const uint8_t &dataType) {
	// sanity check
	if(response == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// reset the memory of the response value to zero
	memset(response, 0, sizeof(SRscpValue));
	// calculate the new tag size
	uint16_t newTagLength = dataLength;
	// check boundaries
	if(newTagLength > 0xFFF8) {
		return RSCP::ERR_DATA_LIMIT_EXCEEDED;
	}
	// re-allocate the structure memory
	if(allocateMemory(response, newTagLength) == false) {
		return RSCP::ERR_NO_MEMORY;
	}
	// copy data
	response->tag = tag;
	response->dataType = dataType;
	response->length = dataLength;
	// copy into the position of the data pointer
	if(response->length > 0) {
		memcpy(response->data, data, dataLength);
	}
	return RSCP::OK;
}

int32_t RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const std::vector<SRscpValue> & value) {
	// sanity check
	if(response == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// reset the memory of the response value to zero
	memset(response, 0, sizeof(SRscpValue));
	// calculate the new tag size
	uint32_t newTagLength = 0;
	for(size_t i = 0; i < value.size(); ++i) {
		newTagLength += sizeof(SRscpValue) - sizeof(value[i].data) + value[i].length;
	}
	// check boundaries
	if(newTagLength > 0xFFF8) {
		return RSCP::ERR_DATA_LIMIT_EXCEEDED;
	}
	// re-allocate the structure memory
	if(allocateMemory(response, newTagLength) == false) {
		return RSCP::ERR_NO_MEMORY;
	}
	// first add the TAG for a container
	response->tag = tag;
	response->dataType = RSCP::eTypeContainer;
	response->length = newTagLength;

	// copy data
	size_t sPos = 0;
	for(size_t i = 0; i < value.size(); ++i) {
		SRscpValue *newData = reinterpret_cast<SRscpValue *> (((uint8_t*)response->data) + sPos);
		newData->tag = value[i].tag;
		newData->dataType = value[i].dataType;
		newData->length = value[i].length;
		if(newData->length > 0) {
			// copy into the position of the data pointer and not into the data pointer itself as the data is appended
			memcpy(&newData->data, value[i].data, newData->length);
		}
		sPos += sizeof(SRscpValue) - sizeof(value[i].data) + value[i].length;
	}
	// all added successfully
	return RSCP::OK;
}

int32_t RscpProtocol::appendValue(SRscpValue* response, const SRscpTag & tag, const uint8_t * data, const uint16_t & dataLength, const uint8_t &dataType) {
	// sanity check
	if(response == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// calculate the new tag size
	uint16_t newTagLength = sizeof(SRscpValue) - sizeof(response->data) + dataLength;
	// check boundaries
	if(response->length + newTagLength > 0xFFF8) {
		return RSCP::ERR_DATA_LIMIT_EXCEEDED;
	}
	// re-allocate the structure memory
	if(allocateMemory(response, response->length + newTagLength) == false) {
		return RSCP::ERR_NO_MEMORY;
	}
	// copy data
	SRscpValue *newData = reinterpret_cast<SRscpValue *>(response->data + response->length);
	newData->tag = tag;
	newData->dataType = dataType;
	newData->length = dataLength;
	// copy into the position of the data pointer and not into the data pointer itself as the data is appended
	if(newData->length > 0) {
		memcpy(&newData->data, data, dataLength);
	}
	// increment the total length by the new tag
	response->length += newTagLength;
	return RSCP::OK;
}

int32_t RscpProtocol::appendValue(SRscpValue* response, const SRscpTag & tag, const std::vector<SRscpValue> & value) {
	// sanity check
	if(response == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// calculate the new tag size
	uint32_t newTagLength = sizeof(SRscpValue) - sizeof(response->data);
	for(size_t i = 0; i < value.size(); ++i) {
		newTagLength += sizeof(SRscpValue) - sizeof(value[i].data) + value[i].length;
	}
	// check boundaries
	if(response->length + newTagLength > 0xFFF8) {
		return RSCP::ERR_DATA_LIMIT_EXCEEDED;
	}
	// re-allocate the structure memory
	if(allocateMemory(response, response->length + newTagLength) == false) {
		return RSCP::ERR_NO_MEMORY;
	}
	// first add the TAG for a container
	SRscpValue *mainTag = reinterpret_cast<SRscpValue *> (response->data + response->length);
	mainTag->tag = tag;
	mainTag->dataType = RSCP::eTypeContainer;
	mainTag->length = newTagLength - (sizeof(SRscpValue) - sizeof(mainTag->data));

	// copy data
	size_t sPos = mainTag->length;
	for(size_t i = 0; i < value.size(); ++i) {
		SRscpValue *newData = reinterpret_cast<SRscpValue *> (response->data + response->length + sPos);
		newData->tag = value[i].tag;
		newData->dataType = value[i].dataType;
		newData->length = value[i].length;
		if(newData->length > 0) {
			// copy into the position of the data pointer and not into the data pointer itself as the data is appended
			memcpy(&newData->data, value[i].data, newData->length);
		}
		sPos += sizeof(SRscpValue) - sizeof(value[i].data) + value[i].length;
	}
	// increment the total length by the new tag
	response->length += newTagLength;
	// all added successfully
	return RSCP::OK;
}

int32_t RscpProtocol::appendValue(SRscpValue* response, const std::vector<SRscpValue> & value) {
	// sanity check
	if(response == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	// calculate the new tag size
	uint32_t newTagLength = 0;
	for(size_t i = 0; i < value.size(); ++i) {
		newTagLength += sizeof(SRscpValue) - sizeof(value[i].data) + value[i].length;
	}
	// check boundaries
	if(response->length + newTagLength > 0xFFF8) {
		return RSCP::ERR_DATA_LIMIT_EXCEEDED;
	}
	// re-allocate the structure memory
	if(allocateMemory(response, response->length + newTagLength) == false) {
		return RSCP::ERR_NO_MEMORY;
	}
	// copy data
	size_t sPos = 0;
	for(size_t i = 0; i < value.size(); ++i) {
		SRscpValue *newData = reinterpret_cast<SRscpValue *> (response->data + response->length + sPos);
		newData->tag = value[i].tag;
		newData->dataType = value[i].dataType;
		newData->length = value[i].length;
		if(newData->length > 0) {
			// copy into the position of the data pointer and not into the data pointer itself as the data is appended
			memcpy(&newData->data, value[i].data, newData->length);
		}
		sPos += sizeof(SRscpValue) - sizeof(value[i].data) + value[i].length;
	}
	// increment the total length by the new tag
	response->length += newTagLength;
	// all added successfully
	return RSCP::OK;
}

int32_t RscpProtocol::destroyValueData(SRscpValue * value) {
	if(value == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	if(value->data != NULL) {
		free(value->data);
		value->data = NULL;
	}
	return RSCP::OK;
}

int32_t RscpProtocol::destroyValueData(std::vector<SRscpValue> & value) {
	for(size_t i = 0; i < value.size(); ++i) {
		destroyValueData(value[i]);
	}
	value.clear();
	return RSCP::OK;
}

int32_t RscpProtocol::destroyFrameData(SRscpFrame * frame) {
	if(frame == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	return destroyValueData(frame->data);
}

int32_t RscpProtocol::destroyFrameData(SRscpFrameBuffer * frameBuffer) {
	if(frameBuffer == NULL) {
		return RSCP::ERR_INVALID_INPUT;
	}
	if(frameBuffer->data != NULL) {
		free(frameBuffer->data);
		frameBuffer->data = NULL;
	}
	return RSCP::OK;
}
