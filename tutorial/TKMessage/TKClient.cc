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

#include <cstddef>
#include <cstdlib>
#include <string.h>
#include <stdio.h>
#include "workflow/Workflow.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/WFFacilities.h"
#include "TKMessage.h"
#include "TKHttpMsg.h"

using WFTKTask = WFNetworkTask<protocol::TKRequest,
									 protocol::TKResponse>;
using tutorial_callback_t = std::function<void (WFTKTask *)>;

using namespace protocol;

class MyFactory : public WFTaskFactory
{
public:
	static WFTKTask *create_tk_task(const std::string& host,
												unsigned short port,
												int retry_max,
												tutorial_callback_t callback)
	{
		using NTF = WFNetworkTaskFactory<TKRequest, TKResponse>;
		WFTKTask *task = NTF::create_client_task(TT_TCP, host, port,
													   retry_max,
													   std::move(callback));
		task->set_keep_alive(30 * 1000);
		return task;
	}
};

unsigned short port;
std::string host;



void callback(WFTKTask* task)
{		
	int state = task->get_state();
	int error = task->get_error();
	TKResponse *resp = task->get_resp();
	char buf[1024];
	memset(buf, 0, sizeof(buf));
	void *body;
	size_t body_size;

	if (state != WFT_STATE_SUCCESS)
	{
		if (state == WFT_STATE_SYS_ERROR)
			fprintf(stderr, "SYS error: %s\n", strerror(error));
		else if (state == WFT_STATE_DNS_ERROR)
			fprintf(stderr, "DNS error: %s\n", gai_strerror(error));
		else
			fprintf(stderr, "other error.\n");
		return;
	}

	resp->get_message_body_nocopy(&body, &body_size);
	//需要根据TKID来转一下
	switch (resp->get_header_type()) {
	case TKID_HTTP_MSG :
	{
			printf("Server Response httpMsg: %s\n", TKHttpMsg::to_str((TKHttpResponse*)resp).c_str());
		break;
	}
	default:
			printf("Server Response tkMsg: %.*s\n", (int)body_size, (char *)body);
		break;
	}

	printf("Input next request string (Ctrl-D to exit): ");
	*buf = '\0';
	scanf("%1023s", buf);
	body_size = strlen(buf);
	//
	std::string url = buf;
	void * msg_ptr = nullptr;
	size_t msg_len = TKHttpMsg::build_tkhttpmsg_body(&msg_ptr, "", url, "", "");

	if (body_size > 0 && (msg_ptr != nullptr))
	{
		WFTKTask *next;
		next = MyFactory::create_tk_task(host, port, 0, callback);
		// next->get_req()->set_message_body(buf, body_size);
		next->get_req()->set_header_type(TKID_HTTP_MSG);
		next->get_req()->set_message_body(msg_ptr, msg_len);
		next->get_resp()->set_size_limit(4 * 1024);
		**task << next; /* equal to: series_of(task)->push_back(next) */
	}
	else
		printf("\n");
}

int main(int argc, char *argv[])
{
	if (argc != 3)
	{
		fprintf(stderr, "USAGE: %s <host> <port>\n", argv[0]);
		exit(1);
	}

	host = argv[1];
	port = atoi(argv[2]);

	/* First request is emtpy. We will ignore the server response. */
	WFFacilities::WaitGroup wait_group(1);
	WFTKTask *task = MyFactory::create_tk_task(host, port, 0, callback);
	task->get_resp()->set_size_limit(4 * 1024);
	Workflow::start_series_work(task, [&wait_group](const SeriesWork *) {
		wait_group.done();
	});

	wait_group.wait();
	return 0;
}

