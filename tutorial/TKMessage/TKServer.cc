/*
  Copyright (c) 2020 Sogou, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

	  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Author: Xie Han (xiehan@sogou-inc.com;63350856@qq.com)
*/

#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <arpa/inet.h>
#include "workflow/WFTask.h"
#include "workflow/Workflow.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/WFServer.h"
#include "workflow/WFFacilities.h"
#include "workflow/HttpMessage.h"
#include "workflow/HttpUtil.h"
#include "TKMessage.h"
#include "TKHttpMsg.h"
#include <atomic>
#include <string>
#include <sys/socket.h>

using namespace protocol;

using WFTKTask = WFNetworkTask<TKRequest, TKResponse>;
using WFTKServer = WFServer<TKRequest, TKResponse>;

struct series_context
{
	std::string ret_data;
	std::string url;
	WFTKTask *proxy_task;
};

const uint32_t MAX_REQ = 1000;
#define REDIRECT_MAX    5
#define RETRY_MAX       2

std::atomic<uint32_t> reqing;

void reply_callback(WFTKTask* proxy_task)
{ 
	SeriesWork *series = series_of(proxy_task);
	series_context *context =
		(series_context *)series->get_context();
	auto *proxy_resp = proxy_task->get_resp();
	size_t size = proxy_resp->get_header_length();

	if (proxy_task->get_state() == WFT_STATE_SUCCESS)
		fprintf(stderr, "%s: Reply success. BodyLength: %zu\n", context->url.c_str(), size);
	else /* WFT_STATE_SYS_ERROR*/
		fprintf(stderr, "%s: Reply failed: %s, BodyLength: %zu\n",
				context->url.c_str(), strerror(proxy_task->get_error()), size);
}

void http_callback(WFHttpTask *task)
{
	int state = task->get_state();
	int error = task->get_error();
	auto *resp = task->get_resp();
	SeriesWork *series = series_of(task);

	series_context* context = (series_context*)series->get_context();
	context->proxy_task->set_callback(reply_callback);
	auto *proxy_resp = context->proxy_task->get_resp();

	std::string header;
	std::string str_body;
	if (state == WFT_STATE_SUCCESS)
	{
		const void *body;
		size_t len;

		std::stringstream ss;
		/* get response header. */
		protocol::HttpHeaderCursor resp_cursor(resp);
		std::string name;
		std::string value;
		while (resp_cursor.next(name, value))
			ss << name << ": " << value << "\r\n";
		header = ss.str();
		ss.str("");

		/* get response body. */
		resp->get_parsed_body(&body, &len);
		resp->append_output_body_nocopy(body, len);
		str_body.assign((char*)body, len);
	}
	else
	{
		const char *err_string;

		if (state == WFT_STATE_SYS_ERROR)
			err_string = strerror(error);
		else if (state == WFT_STATE_DNS_ERROR)
			err_string = gai_strerror(error);
		else if (state == WFT_STATE_SSL_ERROR)
			err_string = "SSL error";
		else /* if (state == WFT_STATE_TASK_ERROR) */
			err_string = "URL error (Cannot be a HTTPS proxy)";

		fprintf(stderr, "%s: Fetch failed. state = %d, error = %d: %s\n",
				context->url.c_str(), state, error, err_string);

		/* As a tutorial, make it simple. And ignore reply status. */
		str_body = err_string;
	}

	proxy_resp->set_header_type(context->proxy_task->get_req()->get_header_type());
	void * msg_ptr = nullptr;
	size_t msg_len = TKHttpMsg::build_tkhttpmsg_body(&msg_ptr, context->ret_data, context->url, header, str_body);
	if (msg_ptr != nullptr)
	{
		proxy_resp->set_message_body_nocopy(msg_ptr, msg_len);
	}
}

void process(WFTKTask *task)
{
	if(task->get_task_seq() == 0) 
	{
		WFConnection *conn = task->get_connection();
		conn->set_context(NULL, [](void *context){ printf("Connection closed\n"); });
		// struct sockaddr sock;
		// socklen_t len = sizeof(sockaddr);
		// int res = task->get_peer_addr(&sock, &len);
		// if (res != 0) {
		// 	fprintf(stderr, "parse addr failed errno: %d, %s,\n",errno, std::strerror(errno));
		// }
	}

	TKRequest *req = task->get_req();
	void *body;
	size_t size;

	req->get_message_body_nocopy(&body, &size);
	//需要根据TKID来转一下
	switch (req->get_header_type()) {
	case TKID_HTTP_MSG :
	{
		printf("Server recv req, httpMsg: %s\n", TKHttpMsg::to_str((TKHttpRequest*)req).c_str());
		break;
	}
	default:
		printf("Server recv req, tkMsg. header: %s, body:%.*s\n", TKMessage::Dump_header(*req).c_str(), (int)size, (char *)body);
		// task->noreply();
		return;
	}

	if (reqing <= MAX_REQ)
	{
		std::string ret_data;
		std::string url;
		std::string header;
		std::string body;
		TKHttpMsg::unpack((TKHttpRequest*)req, ret_data, url, header, body);
		if (strncasecmp(url.c_str(), "http://", 7) != 0 &&
			strncasecmp(url.c_str(), "https://", 8) != 0)
		{
			url = "http://" + url;
		}
		series_context* ctx = new series_context;
		auto* http_task = WFTaskFactory::create_http_task(url, REDIRECT_MAX, RETRY_MAX,
										   http_callback);
		protocol::HttpRequest *http_req = http_task->get_req();
		http_req->add_header_pair("Accept", "*/*");
		http_req->add_header_pair("User-Agent", "Wget/1.14 (linux-gnu)");
		http_req->add_header_pair("Connection", "close");
		//todo

		ctx->url = std::move(url);
		ctx->ret_data = std::move(ret_data);
		ctx->proxy_task = task;
		series_of(task)->set_context(ctx);
		series_of(task)->set_callback([](const SeriesWork *series) {
			delete (series_context *)series->get_context();
			reqing--;
		});
		reqing++;
		**task << http_task;
	} else {
		printf("reqing is %u, close the connection\n", reqing.load());
		task->noreply();
	}
}

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	wait_group.done();
}

int main(int argc, char *argv[])
{
	unsigned short port;

	if (argc != 2)
	{
		fprintf(stderr, "USAGE %s <port>\n", argv[0]);
		exit(1);
	}

	port = atoi(argv[1]);
	signal(SIGINT, sig_handler);

	struct WFServerParams params = SERVER_PARAMS_DEFAULT;
	params.request_size_limit = 4 * 1024;

	WFTKServer server(&params, process);
	if (server.start(AF_INET6, port) == 0 ||
		server.start(AF_INET, port) == 0)
	{
		wait_group.wait();
		server.stop();
	}
	else
	{
		perror("server.start");
		exit(1);
	}

	return 0;
}

