#include "TKHttpMsg.h"
#include <sstream>
#include <string>
#include <cstring>
namespace protocol {

std::string TKHttpMsg::get_body_string() const 
{
  if (body_received < TKHttpMsgHeaderSize )
  {
    return "bad tkhttpmsg";
  }
  std::stringstream ss;
  TKHttpMsgHead * p = (TKHttpMsgHead *)body;
  ss << "id: " << TKID_HTTP_MSG;
  ss << " | method: " << p->method;
  ss << " | version: " << p->version;

  return ss.str();
}

size_t  TKHttpMsg::buildTKHttpMsgBody(void **buf, const std::string& ret,
		const std::string& url, const std::string& header, const std::string& body)
{
	size_t payloadLen = ret.size() + url.size() + header.size() + body.size();
	size_t bufSize = protocol::TKHttpMsgHeaderSize + payloadLen;
	char * p = (char *)malloc(bufSize);
	if (p == nullptr)
	{
		return -1;
	}
	memset(p, 0, bufSize);
	*buf = p;
	protocol::TKHttpMsgHead * httpMsgHead = (protocol::TKHttpMsgHead *)p;
	httpMsgHead->method = TK_HTTP_METHOD_GET;
	httpMsgHead->ret_data.offsize = protocol::TKHttpMsgHeaderSize;
	httpMsgHead->ret_data.length = ret.size();
	httpMsgHead->url.offsize = httpMsgHead->ret_data.offsize = httpMsgHead->ret_data.length;
	httpMsgHead->url.length = url.size();
	httpMsgHead->header.offsize = httpMsgHead->url.offsize = httpMsgHead->url.length;
	httpMsgHead->header.length = header.size();
	httpMsgHead->body.offsize = httpMsgHead->header.offsize = httpMsgHead->header.length;
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