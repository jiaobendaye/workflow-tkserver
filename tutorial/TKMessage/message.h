#pragma once
#include <_types/_uint16_t.h>
#include <_types/_uint32_t.h>
#include <stdlib.h>
#include "workflow/ProtocolMessage.h"

namespace protocol
{


struct TKHeader {
	uint32_t magic;
	uint32_t serial;
	uint16_t origin;
	uint16_t reserve;
	uint32_t type;
	uint32_t param;
	uint32_t length;
};


const size_t TKHEADERSIZE = 24; 

class TKMessage : public ProtocolMessage
{
private:
	virtual int encode(struct iovec vectors[], int max);
	virtual int append(const void *buf, size_t size);

public:
	void set_header_magic(uint32_t magic) {this->header.magic = magic;}
	void set_header_serial(uint32_t serial) {this->header.serial = serial;}
	void set_header_type(uint32_t type) {this->header.type = type;}
	void set_header_param(uint32_t param) {this->header.param = param;}
	void set_header_origin(uint16_t origin) {this->header.origin = origin;}
	void set_header_reserve(uint16_t reserve) {this->header.reserve = reserve;}

	uint32_t get_header_magic() const {return this->header.magic;}
	uint32_t get_header_serial() const {return this->header.serial;}
	uint32_t get_header_type() const {return this->header.type;}
	uint32_t get_header_param() const {return this->header.param;}
	uint16_t get_header_origin() const {return this->header.origin;}
	uint16_t get_header_reserve() const {return this->header.reserve;}

	int set_message_body(const void *body, size_t size);

	void get_message_body_nocopy(void **body, size_t *size)
	{
		*body = this->body;
		*size = this->header.length;
	}

protected:
    TKHeader header;
	size_t head_received;
	char *body;
	size_t body_received;

public:
	TKMessage()
	{
		this->head_received = 0;
		this->body = NULL;
	}

	TKMessage(TKMessage&& msg);
	TKMessage& operator = (TKMessage&& msg);

	virtual ~TKMessage()
	{
		free(this->body);
	}
};

using TKRequest = TKMessage;
using TKResponse = TKMessage;

struct TKElem
{
	uint16_t offsize;
	uint16_t length;
};

#define TKIDHttpMsg 1
struct TKHttpMsgHeader
{
	uint16_t method;
	uint16_t version;
	TKElem ret_data;
	TKElem url;
	TKElem headers;
	TKElem body;
};

}