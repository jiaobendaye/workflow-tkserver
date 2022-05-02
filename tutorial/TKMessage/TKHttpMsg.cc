#include "TKHttpMsg.h"
#include <sstream>
#include <string>
#include <cstring>
namespace protocol {

std::string TKHttpMsg::to_str(const TKHttpMsg* pMsg) 
{
  if (pMsg->body_received < TKHttpMsgHeaderSize )
  {
    return "bad tkhttpmsg";
  }
  TKHttpMsgHead * p = (TKHttpMsgHead *)pMsg->body;
	if ((p->ret_data.offsize + p->ret_data.length) > pMsg->get_header_length()
		||(p->url.offsize + p->url.length) > pMsg->get_header_length()
		||(p->header.offsize + p->header.length) > pMsg->get_header_length()
		||(p->body.offsize + p->body.length) > pMsg->get_header_length())
	{
    return "bad tkhttpmsg";
	}

  std::stringstream ss;
  ss << "id: " << pMsg->get_header_type();
  ss << " | method: " << p->method;
  ss << " | version: " << p->version;
	std::string ret(((char*)p)+p->ret_data.offsize, p->ret_data.length);
	ss << " | ret_data: " << ret;
	std::string url(((char*)p)+p->url.offsize, p->url.length);
	ss << " | url: " << url;
	std::string header(((char*)p)+p->header.offsize, p->header.length);
	ss << " | header: " << header;
	std::string body(((char*)p)+p->body.offsize, p->body.length);
	ss << " | body: " << body;

  return ss.str();
}

bool TKHttpMsg::unpack(const TKHttpMsg* pMsg, std::string& ret, std::string& url, std::string& header, std::string& body)
{
  if (pMsg->body_received < TKHttpMsgHeaderSize )
  {
    return false;
  }

  TKHttpMsgHead * p = (TKHttpMsgHead *)pMsg->body;
	if ((p->ret_data.offsize + p->ret_data.length) > pMsg->get_header_length()
		||(p->url.offsize + p->url.length) > pMsg->get_header_length()
		||(p->header.offsize + p->header.length) > pMsg->get_header_length()
		||(p->body.offsize + p->body.length) > pMsg->get_header_length())
	{
    return false;
	}
	ret.assign(((char*)p)+p->ret_data.offsize, p->ret_data.length);
	url.assign(((char*)p)+p->url.offsize, p->url.length);
	header.assign(((char*)p)+p->header.offsize, p->header.length);
	body.assign(((char*)p)+p->body.offsize, p->body.length);
	return true;
}

size_t  TKHttpMsg::build_tkhttpmsg_body(void **buf, const std::string& ret,
		const std::string& url, const std::string& header, const std::string& body)
{
	size_t payloadLen = ret.size() + url.size() + header.size() + body.size();
	size_t bufSize = TKHttpMsgHeaderSize + payloadLen;
	char * p = (char *)malloc(bufSize);
	if (p == nullptr)
	{
		return -1;
	}
	memset(p, 0, bufSize);
	*buf = p;
	protocol::TKHttpMsgHead * httpMsgHead = (protocol::TKHttpMsgHead *)p;
	httpMsgHead->method = TK_HTTP_METHOD_GET;
	httpMsgHead->ret_data.offsize = TKHttpMsgHeaderSize;
	httpMsgHead->ret_data.length = ret.size();

	httpMsgHead->url.offsize = httpMsgHead->ret_data.offsize + httpMsgHead->ret_data.length;
	httpMsgHead->url.length = url.size();

	httpMsgHead->header.offsize = httpMsgHead->url.offsize + httpMsgHead->url.length;
	httpMsgHead->header.length = header.size();

	httpMsgHead->body.offsize = httpMsgHead->header.offsize + httpMsgHead->header.length;
	httpMsgHead->body.length = body.size();
	
	p = (char *)(httpMsgHead+1);
	memcpy(p, ret.c_str(), ret.size());
	p += ret.size();
	memcpy(p, url.c_str(), url.size());
	p += url.size();
	memcpy(p, header.c_str(), header.size());
	p += header.size();
	memcpy(p, body.c_str(), body.size());
	p += body.size();

	return bufSize;
}
}