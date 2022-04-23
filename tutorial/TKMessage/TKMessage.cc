#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <utility>
#include "TKMessage.h"

namespace protocol
{
int TKMessage::encode(struct iovec vectors[], int max/*max==8192*/)
{
	// uint32_t n = htonl(this->body_size);

	// memcpy(this->head, &n, 4);
    //TODO bigendian process
	vectors[0].iov_base = &this->header;
	vectors[0].iov_len = TKHEADERSIZE;
	vectors[1].iov_base = this->body;
	vectors[1].iov_len = this->header.length;
	printf("send tkmsg with length: %u \n", this->header.length);

	return 2;	/* return the number of vectors used, no more then max. */
}

int TKMessage::append(const void *buf, size_t size)
{
	if (this->head_received < TKHEADERSIZE)
	{
		size_t head_left;
		void *p;

		p = (char *)&this->header + head_received;
		head_left = TKHEADERSIZE - this->head_received;
		if (size < TKHEADERSIZE - this->head_received)
		{
			memcpy(p, buf, size);
			this->head_received += size;
			return 0;
		}

		memcpy(p, buf, head_left);
		size -= head_left;
		buf = (const char *)buf + head_left;

        //TODO bigendian process
		if (this->header.length > this->size_limit)
		{
			errno = EMSGSIZE;
			return -1;
		}

		this->body = (char *)malloc(this->header.length);
		if (!this->body)
			return -1;

		this->body_received = 0;
	}

	size_t body_left = this->header.length - this->body_received;

	if (size > body_left)
	{
		errno = EBADMSG;
		return -1;
	}

	memcpy(this->body, buf, size);
	if (size < body_left)
	{
		printf("tkmsg recving..., bodyleft: %lu", body_left);
		return 0;
	}

	this->body_received = this->header.length;
	printf("recv tkmsg with length: %u \n",this->header.length);
	return 1;
}

int TKMessage::set_message_body(const void *body, size_t size)
{
	void *p = malloc(size);

	if (!p)
		return -1;

	memcpy(p, body, size);
	free(this->body);
	this->body = (char *)p;
    this->header.length = size;

	this->head_received = TKHEADERSIZE;
	this->body_received = size;
	return 0;
}

TKMessage::TKMessage(TKMessage&& msg) :
	ProtocolMessage(std::move(msg))
{
    memcpy(&this->header, &msg.header, TKHEADERSIZE);
	this->head_received = msg.head_received;
	this->body = msg.body;
	this->body_received = msg.body_received;

	msg.head_received = 0;
	msg.body = NULL;
}

TKMessage& TKMessage::operator = (TKMessage&& msg)
{
	if (&msg != this)
	{
		*(ProtocolMessage *)this = std::move(msg);

		memcpy(&this->header, &msg.header, TKHEADERSIZE);
		this->head_received = msg.head_received;
		this->body = msg.body;
		this->body_received = msg.body_received;

		msg.head_received = 0;
		msg.body = NULL;
	}

	return *this;
}
}