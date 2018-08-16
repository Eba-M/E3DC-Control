/*
 * RscpProtocol.h
 *
 *  Created on: 19.05.2014
 *      Author: dikirile
 */

#ifndef RSCPPROTOCOL_H_
#define RSCPPROTOCOL_H_

#include <vector>
#include <string>
#include <string.h>
#include "RscpTypes.h"

class RscpProtocol {
public:
    /*
     * Constructor
     */
	RscpProtocol();
    /*
     * Destructor
     */
	virtual ~RscpProtocol();
    /*
     * \brief Function to get the total expected length that the frame buffer inside \var data should have (not has).
     *        This function also validates the MAGIC and VERSION of the frame. The frame inside the \var data buffer
     *        does not have to be complete but should at least have the size of an SRscpFrameHeader in bytes.
     * @param data		- Pointer to the raw data frame buffer
     * @param length	- Length of data buffer in bytes. Must be at least sizeof(SRscpFrameHeader) bytes.
     * @return			- RSCP error code if the function fails or the amount of bytes the frame should have to be full.
     */
	int32_t getFrameLength(const uint8_t * data, const uint32_t & length);
    /*
     * \brief Create a RSCP frame from one single RscpValue struct into the pre-allocated \var frameBuffer.
     *        The data is aligned in line inside the frameBuffer structure to allow direct send of the complete frame.
     * 		  The user is responsible to free the memory of \var frameBuffer with RscpProtocol::destroyFrameBuffer().
     * @param frame       - Pointer to an rscp frame struct (should be != NULL)
     * @param data        - Pointer to the first RSCP value struct in line.
     * @param dataLength  - Data length of the data buffer in bytes.
     * @param calcCRC     - If set TRUE the CRC for the frame is calculated and appended to the frame.
     * @return	          - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createFrameAsBuffer(SRscpFrameBuffer* frameBuffer, const uint8_t * data, uint16_t dataLength, bool calcCRC);
    /*
     * \brief Create a RSCP frame from one single RscpValue struct into the pre-allocated \var frameBuffer.
     *        The data is aligned in line inside the frameBuffer structure to allow direct send of the complete frame.
     * 		  The user is responsible to free the memory of \var frameBuffer with RscpProtocol::destroyFrameBuffer().
     * @param frame   - Pointer to an rscp frame struct (should be != NULL)
     * @param data    - RSCP value struct
     * @param calcCRC - If set TRUE the CRC for the frame is calculated and appended to the frame.
     * @return	      - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createFrameAsBuffer(SRscpFrameBuffer* frameBuffer, const SRscpValue & data, bool calcCRC);
    /*
     * \brief Create a RSCP frame from more than one RscpValue struct into the pre-allocated \var frameBuffer.
     *        The data is aligned in line inside the frameBuffer structure to allow direct send of the complete frame.
     * 		  The user is responsible to free the memory of \var frameBuffer with RscpProtocol::destroyFrameBuffer().
     * @param frame   - Pointer to an rscp frame struct (should be != NULL)
     * @param data    - Reference RSCP value struct
     * @param calcCRC - If set TRUE the CRC for the frame is calculated and appended to the frame.
     * @return	      - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createFrameAsBuffer(SRscpFrameBuffer *frameBuffer, const std::vector<SRscpValue> & data, bool calcCRC);
    /*
     * \brief Create a RSCP frame from SRscpFrame struct into the pre-allocated \var frameBuffer.
     *        The data is aligned in line inside the frameBuffer structure to allow direct send of the complete frame.
     * 		  The user is responsible to free the memory of \var frameBuffer with RscpProtocol::destroyFrameBuffer().
     * @param frame   - Pointer to an rscp frame struct (should be != NULL)
     * @param data    - vector with RSCP value structs
     * @param calcCRC - If set TRUE the CRC for the frame is calculated and appended to the frame.
     * @return	      - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createFrameAsBuffer(SRscpFrameBuffer *frameBuffer, const SRscpFrame & frame, bool calcCRC);
    /*
     * \brief Create a RSCP frame from one single RscpValue struct into the pre-allocated \var frame.
     * 		  The user is responsible to free the memory of \var frame with RscpProtocol::destroyFrameData().
     * @param frame   - Pointer to an rscp frame struct (should be != NULL)
     * @param data    - RSCP value struct
     * @param calcCRC - If set TRUE the CRC for the frame is calculated and appended to the frame.
     * @return	      - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createFrame(SRscpFrame* frame, const SRscpValue & data, bool calcCRC);
    /*
     * \brief Create a RSCP frame from more than one RscpValue struct into the pre-allocated \var frame.
     * 		  The user is responsible to free the memory of \var frame with RscpProtocol::destroyFrameData().
     * @param frame   - Pointer to an rscp frame struct (should be != NULL)
     * @param data    - vector with RSCP value structs
     * @param calcCRC - If set TRUE the CRC for the frame is calculated and appended to the frame.
     * @return	      - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createFrame(SRscpFrame* frame, const std::vector<SRscpValue> & data, bool calcCRC);
    /*
     * \brief Function to parse raw frame data from \var data of length \var length
     * 		  into the preallocated struct \var frame.
     * 		  The user is responsible to free the memory of \var frame with RscpProtocol::destroyFrameData().
     * @param data		- Pointer to the raw data frame buffer
     * @param length	- Length of data in bytes
     * @param frame		- Frame buffer into which the data is parsed (should be != NULL)
     * @return			- RSCP error code if the function fails or processed amount of bytes on success
     */
	int32_t parseFrame(const uint8_t* data, const uint32_t & length, SRscpFrame* frame);
    /*
     * \brief Function to parse raw tag data from \var data of length \var length
     * 		  and return a vector of tags.
     * 		  The user is responsible to free the memory of \var frameData with RscpProtocol::destroyValueData().
     * @param data		- Pointer to the raw data frame buffer
     * @param length	- Length of data in bytes
     * @param frameData - Reference to a data vector of SRscpValues
     * @return			- RSCP error code if the function fails or processed amount of bytes on success
     */
    int32_t parseData(const uint8_t* data, const uint32_t & length, std::vector<SRscpValue> & frameData);
	/*
	 * \biref This function allocates memory of size \var size. If data is already allocated it will reallocate the requested size.
	 * @param value  - Pointer to the RSCP value struct.
	 * @param size   - The buffer size that is required
	 * @return TRUE if data was allocated otherwise false.
	 */
	bool allocateMemory(SRscpValue* value, size_t size);
    /*
     * \brief Create value as a new RSCP tag inside an RSCP value struct \var response.
     * 		  The struct length is set 0 and the data type to RSCP::eTypeNone. The data pointer
     * 		  is set to NULL pointer.
     * @param response - RSCP value struct into which the value should be added as a new RSCP tag.
     * @param tag      - TAG number that should be used for this tag.
     * @return         - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag) {
    	return createValue(response, tag, NULL, 0, RSCP::eTypeNone);
    }
    /*
     * \brief Create value as a new RSCP tag inside an RSCP value struct \var response.
     * 		  The struct length is incremented further and the necessary data is allocated.
     * 		  The allocated data in the \var response.data has to be freed by the user when
     * 		  unused anymore.
     * 		  The user is responsible to free the memory of \var response with RscpProtocol::destroyValueData().
     * @param response - RSCP value struct into which the value should be added as a new RSCP tag.
     * @param tag      - TAG number that should be used for this tag.
     * @param value    - The value with the datatype that should be added to the tag.
     * @return         - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const bool & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeBool);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const char & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const char & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeChar8);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const int8_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeChar8);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUChar8);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const int16_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const int16_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeInt16);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint16_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const uint16_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUInt16);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const int32_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const int32_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeInt32);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint32_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const uint32_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUInt32);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const int64_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const int64_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeInt64);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint64_t & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const uint64_t & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUInt64);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const float & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const float & value) {
    	return createValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeFloat32);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const double & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const double & value) {
    	return createValue(response, tag, (uint8_t *)&value, sizeof(value), RSCP::eTypeDouble64);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const SRscpTimestamp & timestamp)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const SRscpTimestamp & timestamp) {
    	return createValue(response, tag, (uint8_t *)&timestamp, sizeof(timestamp), RSCP::eTypeTimestamp);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const char * value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const char * value) {
    	return createValue(response, tag, (uint8_t*)value, strlen(value), RSCP::eTypeString);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const std::string & value)
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const std::string & value) {
    	return createValue(response, tag, (uint8_t*)value.c_str(), value.size(), RSCP::eTypeString);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const bool & value);
     *
     * This function copies the \var value.data and adds it to the \var response.data buffer. The \var value.data buffer
     * is not freed and has to be freed by the user.  The datatype is therefore set as RSCP::eTypeContainer.
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const SRscpValue & value) {
    	return createValue(response, tag, std::vector<SRscpValue>(1, value));
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const SRscpValue & value)
     *
     * Copy a vector of SRscpValue into a new container with the tag \var tag.
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const std::vector<SRscpValue> & value);
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const bool & value);
     *
     * This function copies the \var value buffer and adds it to the \var response.data buffer. The \var value.data buffer
     * is not freed and has to be freed by the user. The datatype is therefore set as RSCP::eTypeByteArray.
     * @param value      - Pointer to the value buffer
     * @param dataLength - Length of the value buffer
     * @return           - RSCP error code if the function fails else RSCP::OK
     */
    int32_t createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t* value, const uint16_t & dataLength) {
    	return createValue(response, tag, value, dataLength, RSCP::eTypeByteArray);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t* value, const uint16_t & dataLength);
     *
     * Same as RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t* value, const uint16_t & dataLength)
     * but the data type can be user defined.
     */
	int32_t createValue(SRscpValue* response, const SRscpTag & tag, const uint8_t * data, const uint16_t & dataLength, const uint8_t &dataType);
    /*
     * @copydoc RscpProtocol::createErrorValue(SRscpValue* response, const SRscpTag & tag, const uint32_t & error)
     */
    int32_t createErrorValue(SRscpValue* response, const SRscpTag & tag, const uint32_t & error) {
    	return createValue(response, tag, (uint8_t *)&error, sizeof(error), RSCP::eTypeError);
    }
    /*
     * @copydoc RscpProtocol::createValue(SRscpValue* response, const SRscpTag & tag, const bool & value);
     *
     * This function just sets the tag and the datatype inside the response variable.
     * It does not allocate any memory or modify the length of the response.
     */
    int32_t createValueType(SRscpValue* response, const SRscpTag & tag, const uint8_t & dataType) {
    	return createValue(response, tag, NULL, 0, dataType);
    }
    /*
     * @copydoc RscpProtocol::createContainerValue(SRscpValue* response, const SRscpTag & tag)
     */
    int32_t createContainerValue(SRscpValue* response, const SRscpTag & tag) {
    	return createValueType(response, tag, RSCP::eTypeContainer);
    }
    /*
     * \brief The appendValue functions are equivalent to the createValue function but they create a new SRscpValue inside an existing SRscpValue
     * 		  The user is responsible to free the memory of \var response with RscpProtocol::destroyValueData().
     */
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag) {
    	return appendValue(response, tag, NULL, 0, RSCP::eTypeNone);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const bool & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeBool);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const char & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeChar8);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const int8_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeChar8);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const uint8_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUChar8);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const int16_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeInt16);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const uint16_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUInt16);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const int32_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeInt32);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const uint32_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUInt32);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const int64_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeInt64);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const uint64_t & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeUInt64);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const float & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeFloat32);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const double & value) {
    	return appendValue(response, tag, (uint8_t *) &value, sizeof(value), RSCP::eTypeDouble64);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const char * value) {
    	return appendValue(response, tag, (uint8_t*)value, strlen(value), RSCP::eTypeString);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const std::string & value) {
    	return appendValue(response, tag, (uint8_t *) value.c_str(), value.size(), RSCP::eTypeString);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const SRscpTimestamp & timestamp) {
    	return appendValue(response, tag, (uint8_t *) &timestamp, sizeof(timestamp), RSCP::eTypeTimestamp);
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const SRscpValue & value) {
    	return appendValue(response, tag, std::vector<SRscpValue>(1, value));
    }
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const std::vector<SRscpValue> & value);
    /*
     * These functions append the value struct(s) into the data field of response without changing the tag variable of the response.
     * The length variable of the response and the data variable are increased.
     */
    int32_t appendValue(SRscpValue* response, const SRscpValue & value) {
    	return appendValue(response, std::vector<SRscpValue>(1, value));
    }
    /*
     * @copydoc RscpProtocol::appendValue(SRscpValue* response, const SRscpValue & value);
     *
     * These functions append the value struct(s) into the data field of response without changing the tag variable of the response.
     * The length variable of the response and the data variable are increased.
     */
    int32_t appendValue(SRscpValue* response, const std::vector<SRscpValue> & value);
    /*
     * @copydoc RscpProtocol::appendValue(SRscpValue* response, const SRscpTag & tag, const uint8_t* value, const uint16_t & dataLength);
     *
     * This function adds a new TAG value with \var value as the data of \var dataLength bytes.
     */
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const uint8_t* value, const uint16_t & dataLength) {
    	return appendValue(response, tag, value, dataLength, RSCP::eTypeByteArray);
    }
    /*
     * @copydoc RscpProtocol::appendValue(SRscpValue* response, const SRscpTag & tag, const uint8_t* value, const uint16_t & dataLength, const uint8_t &dataType);
     *
     * This function adds a new TAG value with \var value as the data of \var dataLength bytes and \var dataType as type.
     */
    int32_t appendValue(SRscpValue* response, const SRscpTag & tag, const uint8_t* data, const uint16_t & dataLength, const uint8_t &dataType);
    /*
     * @copydoc RscpProtocol::appendValueType(SRscpValue* response, const SRscpTag & tag, const bool & value);
     *
     * This function just adds a tag and sets the datatype inside the response variable. The length is set 0.
     */
    int32_t appendValueType(SRscpValue* response, const SRscpTag & tag, const uint8_t & dataType) {
    	return appendValue(response, tag, NULL, 0, dataType);
    }
    /*
     * @copydoc RscpProtocol::appendErrorValue(SRscpValue* response, const SRscpTag & tag, const uint32_t & error)
     */
    int32_t appendErrorValue(SRscpValue* response, const SRscpTag & tag, const uint32_t & error) {
    	return appendValue(response, tag, (uint8_t *)&error, sizeof(error), RSCP::eTypeError);
    }
	/*!
	 * \brief This templates is called by the get functions to unify the getting process of all datatypes.
	 */
    template <class cType>
    cType getValue(const SRscpValue* value) {
    	if((value == NULL) || (value->data == NULL)) {
    		return cType();
    	}
    	// if the size is smaller than needed copy only the smaller part
    	if(sizeof(cType) <= value->length) {
    		cType tTmp;
    		memcpy(&tTmp, value->data, sizeof(cType));
    		return tTmp;
    	}
    	// if the size needed is bigger then zero out the rest
    	else {
    		cType tTmp;
    		memset(&tTmp, 0, sizeof(cType));
    		memcpy(&tTmp, value->data, value->length);
    		return tTmp;
    	}
    }
    /*
     * \brief Get a value from the RSCP struct \var value as a define return data type.
     * @param value - The pointer to a struct RSCP.
     * @return		- The value in the requested data type.
     */
    bool getValueAsBool(const SRscpValue* value) {
    	return getValue<bool>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    int8_t getValueAsChar8(const SRscpValue* value) {
    	return getValue<int8_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    uint8_t getValueAsUChar8(const SRscpValue* value) {
    	return getValue<uint8_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    int16_t getValueAsInt16(const SRscpValue* value) {
    	return getValue<int16_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    uint16_t getValueAsUInt16(const SRscpValue* value) {
    	return getValue<uint16_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    int32_t getValueAsInt32(const SRscpValue* value) {
    	return getValue<int32_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    uint32_t getValueAsUInt32(const SRscpValue* value) {
    	return getValue<uint32_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    int64_t getValueAsInt64(const SRscpValue* value) {
    	return getValue<int64_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    uint64_t getValueAsUInt64(const SRscpValue* value) {
    	return getValue<uint64_t>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    float getValueAsFloat32(const SRscpValue* value) {
    	return getValue<float>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsDouble64(const SRscpValue* value)
	 */
    double getValueAsDouble64(const SRscpValue* value) {
    	return getValue<double>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    SRscpTimestamp getValueAsTimestamp(const SRscpValue* value) {
    	return getValue<SRscpTimestamp>(value);
    }
	/*!
	 * \copydoc RscpProtocol::getValueAsBool(const SRscpValue* value)
	 */
    std::string getValueAsString(const SRscpValue* value);
    /*
     * \brief Function get \var value as a container type. It returns all the SRscpValue structs from the container.
     * 		  The user is responsible to free the memory of the vector<SRscpValue> with RscpProtocol::destroyValueData().
     * @param data - RSCP value struct
     * @return     - Vector with all rscp value structs found in the container. Empty on failure and no or invalid data.
     */
    std::vector<SRscpValue> getValueAsContainer(const SRscpValue* value) {
    	std::vector<SRscpValue> dataValues;
    	parseData(value->data, value->length, dataValues);
    	return dataValues;
    }
    /*
     * \brief This function destroys all allocated data inside a RSCP value.
     * @param  - Pointer to the RSCP value.
     * @return - RSCP error code if the function fails else RSCP::OK
     */
    int32_t destroyValueData(SRscpValue * value);
    /*
	 * \copydoc RscpProtocol::destroyValueData(SRscpValue & value)
	 * Overload with reference to SRscpValue.
     */
    int32_t destroyValueData(SRscpValue & value) {
    	return destroyValueData(&value);
    }
    /*
     * \brief This function destroys all allocated data inside a vector of RSCP values.
     * @param  - Reference to the vector of the RSCP values.
     * @return - RSCP error code if the function fails else RSCP::OK
     */
    int32_t destroyValueData(std::vector<SRscpValue> & value);
    /*
     * \brief This function destroys all allocated data inside a RSCP frame.
     * @param  - Pointer to a RSCP frame
     * @return - RSCP error code if the function fails else RSCP::OK
     */
    int32_t destroyFrameData(SRscpFrame * frame);
    /*
	 * \copydoc RscpProtocol::destroyFrameData(SRscpFrame * frame)
	 * Overload with reference to SRscpFrame.
     */
    int32_t destroyFrameData(SRscpFrame & frame) {
    	return destroyValueData(frame.data);
    }
    /*
     * \brief This function destroys all allocated data inside a RSCP frame buffer.
     * @param  - Pointer to a RSCP frame buffer struct
     * @return - RSCP error code if the function fails else RSCP::OK
     */
    int32_t destroyFrameData(SRscpFrameBuffer * frameBuffer);
	/*!
	 * \copydoc RscpProtocol::destroyFrameBuffer(SRscpFrameBuffer & frameBuffer)
	 * Overload with reference to SRscpFrameBuffer.
	 */
    int32_t destroyFrameData(SRscpFrameBuffer & frameBuffer) {
    	return destroyFrameData(&frameBuffer);
    }
private:
    /*
     * \brief This function calculates the ethernet protocol CRC32 hash from \var data over \var length bytes.
     * @param - Pointer to a data buffer
     * @param - Length of the buffer data
     * @return The calculated CRC32 value is returned.
     */
    uint32_t calculateCRC32(const uint8_t *data, uint16_t length);
    /*
     * \brief This function sets the current time in seconds and nanoseconds to the frame.
     * @param - Pointer to an rscp frame object.
     * @return True on success else false.
     */
    bool setHeaderTimestamp(SRscpFrame *frame);
};

#endif /* RSCPPROTOCOL_H_ */
