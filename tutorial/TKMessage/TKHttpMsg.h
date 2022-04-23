#pragma once
#include "TKMessage.h"

namespace protocol {

const size_t TKHttpMsgHeaderSize = 20; 
#define TKID_HTTP_MSG (0x00001)

#define TK_HTTP_METHOD_POST (1);
#define TK_HTTP_METHOD_GET (2);

struct TKHttpMsgHead
{
	uint16_t method;
	uint16_t version;
	TKElem ret_data;
	TKElem url;
	TKElem header;
	TKElem body;
};

class TKHttpMsg :public TKMessage 
{
	public:
	static std::string to_str(TKHttpMsg* pMsg);

  public:
  static size_t build_tkhttpmsg_body(void **buf, const std::string& ret,
		const std::string& url, const std::string& header, const std::string& body);
};

using TKHttpRequest = TKHttpMsg;
using TKHttpResponse = TKHttpMsg;
}