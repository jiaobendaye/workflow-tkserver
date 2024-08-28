/*
  Copyright (c) 2019 Sogou, Inc.

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

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <utility>
#include <string>
#include <sstream>
#include "workflow/HttpMessage.h"
#include "workflow/HttpUtil.h"
#include "workflow/WFHttpServer.h"
#include "workflow/WFTaskFactory.h"
#include "workflow/Workflow.h"
#include "workflow/WFFacilities.h"
#include "StringUtil.h"

using namespace protocol;

static const size_t FILE_SIZE_BOOTOM = 10*1024*1024;

void pwitevio_callback(WFFileVIOTask *task)
{
	long ret = task->get_retval();
	HttpResponse *resp = (HttpResponse *)task->user_data;

	if (task->get_state() != WFT_STATE_SUCCESS || ret < 0)
	{
		resp->set_status_code("503");
		resp->append_output_body("503 Internal Server Error.");
	}
	else 
	{
		resp->set_status_code("200");
	}
}

void pwrite_callback(WFFileIOTask *task)
{
	FileIOArgs *args = task->get_args();
	long ret = task->get_retval();
	HttpResponse *resp = (HttpResponse *)task->user_data;

	close(args->fd);
	if (task->get_state() != WFT_STATE_SUCCESS || ret < 0)
	{
		resp->set_status_code("503");
		resp->append_output_body("503 Internal Server Error.");
	}
	else 
	{
		resp->set_status_code("200");
	}
}

void pread_callback(WFFileIOTask *task)
{
	FileIOArgs *args = task->get_args();
	long ret = task->get_retval();
	HttpResponse *resp = (HttpResponse *)task->user_data;

	if (task->get_state() != WFT_STATE_SUCCESS || ret < 0)
	{
		resp->set_status_code("503");
		resp->append_output_body("503 Internal Server Error.");
	}
	else /* Use '_nocopy' carefully. */
	{
		resp->append_output_body_nocopy(args->buf, ret);
	}
}

std::string get_etag(const std::string& path)
{
	return path;
}

inline bool is_digit(char c) 
{
	return '0' <= c && c <= '9';
}

void download_range(WFHttpTask *server_task,  int fd, const std::string& range)
{
	HttpResponse *resp = server_task->get_resp();
	//parser & check range
	// Range: bytes=89-999
	size_t start = 0, end = 0;
	do
	{
		if(!range.empty())
		{
			const char* c = range.c_str();
			if(strncmp(range.c_str(),"bytes=",6) != 0)
				break;
			c+=6;

			if(!is_digit(*c)) break;

			while(is_digit(*c))
			{
				start = start*10+(*c-'0');
				++c;
			}
			if(*c != '-' || !is_digit(*(++c))) break;

			while(is_digit(*c))
			{
				end = end*10+(*c-'0');
				++c;
			}
		}
	} while (0);
	size_t file_size = lseek(fd, 0, SEEK_END);
	if(end >= start && end <= file_size && end > 0)
	{
		if((end - start) > FILE_SIZE_BOOTOM)
		{
			end = start+FILE_SIZE_BOOTOM;
		}
		end = std::min(end, file_size);
		// resp : Content-Range: bytes 89-999/100000
		std::stringstream ss;
		ss << "bytes " << start << '-' << end << '/' << file_size;
		resp->set_header_pair("Content-Range", ss.str());
		size_t size = end - start;
		void *buf = malloc(size); /* As an example, assert(buf != NULL); */
		WFFileIOTask *pread_task;

		pread_task = WFTaskFactory::create_pread_task(fd, buf, size, start,
														pread_callback);
		/* To implement a more complicated server, please use series' context
			* instead of tasks' user_data to pass/store internal data. */
		pread_task->user_data = server_task->get_resp();	/* pass resp pointer to pread task. */
		server_task->user_data = buf;	/* to free() in callback() */
		server_task->set_callback([](WFHttpTask *t){ free(t->user_data); });
		series_of(server_task)->push_back(pread_task);
	}
	else
	{
		resp->set_status_code("400");
		resp->append_output_body("Range Error.");
	}
}

void download_large_file(WFHttpTask *server_task,int fd, const std::string& abs_path) 
{
	HttpRequest *req = server_task->get_req();
	HttpResponse *resp = server_task->get_resp();

	std::string name;
	std::string value;
	std::string range, old_etag;
	HttpHeaderCursor req_cursor(req);
	while (req_cursor.next(name, value))
	{
		if(name == "If-Range") 
		{
			old_etag = value;
		}
		if(name == "Range") 
		{
			range = value;
		}
		if(!old_etag.empty() && !range.empty())
			break;
	}
	std::string now_etag = get_etag(abs_path);
	resp->set_header_pair("ETag", now_etag);
	resp->set_header_pair("Accept-Ranges", "bytes");
	resp->set_header_pair("Content-Type", "application/octet-stream");

	if(!old_etag.empty() && old_etag == now_etag) {
		//按客户端的range续上
		resp->set_status_code("206");
		return download_range(server_task, fd, range);
	} else {
		//文件已经改动了,告诉客户端需要从头开始下传
		resp->set_status_code("200");
		resp->append_output_body("File is large, use ETag/Range.");
		return;
	}
}


void download_hole_file(WFHttpTask *server_task,int fd) 
{
	size_t size = lseek(fd, 0, SEEK_END);
	void *buf = malloc(size); /* As an example, assert(buf != NULL); */
	WFFileIOTask *pread_task;

	pread_task = WFTaskFactory::create_pread_task(fd, buf, size, 0,
													pread_callback);
	/* To implement a more complicated server, please use series' context
		* instead of tasks' user_data to pass/store internal data. */
	pread_task->user_data = server_task->get_resp();	/* pass resp pointer to pread task. */
	server_task->user_data = buf;	/* to free() in callback() */
	server_task->set_callback([](WFHttpTask *t){ free(t->user_data); });
	series_of(server_task)->push_back(pread_task);
}

void process_download(WFHttpTask *server_task,  const std::string& abs_path)
{
	HttpResponse *resp = server_task->get_resp();

	int fd = open(abs_path.c_str(), O_RDONLY);
	if (fd >= 0)
	{
		series_of(server_task)->set_callback([fd](const SeriesWork *){
			close(fd);
		});
		//如果是小文件,直接下传
		size_t size = lseek(fd, 0, SEEK_END);
		if(size <= FILE_SIZE_BOOTOM)
		{
			return download_hole_file(server_task, fd);
		}
		else 
		{
			return download_large_file(server_task, fd, abs_path);
		}
	}
	else
	{
		resp->set_status_code("404");
		resp->append_output_body("404 Not Found.");
	}
}

void process_upload(WFHttpTask *server_task,  const std::string& abs_path)
{
	HttpRequest *req = server_task->get_req();
	HttpResponse *resp = server_task->get_resp();

	int cnt = 1024;
	struct iovec *iovec = (struct iovec *)malloc(sizeof(struct iovec)*cnt);
	memset(iovec, 0, sizeof(struct iovec)*cnt);
	HttpUtil::decode_chunked_body_to_iovec(req,iovec,cnt);
	printf("process_upload iovec: %p cnt:%d\n", iovec, cnt);
	auto pwrite_task = WFTaskFactory::create_pwritev_task(abs_path, iovec,cnt,0,pwitevio_callback);

	pwrite_task->user_data = resp;
	server_task->user_data = iovec;
	server_task->set_callback([](WFHttpTask * t){free(t->user_data);});
	series_of(server_task)->push_back(pwrite_task);
}

void process(WFHttpTask *server_task, const char *root)
{
	HttpRequest *req = server_task->get_req();
	HttpResponse *resp = server_task->get_resp();

	resp->add_header_pair("Server", "Sogou C++ Workflow Server");
	const char *uri = req->get_request_uri();
	const char *p = uri;

	printf("Request %s  URI: %s\n",req->get_method(), uri);
	while (*p && *p != '?')
		p++;
	std::string abs_path(uri, p - uri);
	resp->add_header_pair("File", abs_path);
	StringUtil::url_decode(abs_path);
	abs_path = root + abs_path;
	
	if(strcmp(req->get_method(), HttpMethodGet) == 0) //download
	{ 
		return process_download(server_task, abs_path);
	} 
	else if(strcmp(req->get_method(), HttpMethodPost) == 0) //upload
	{ 
		return process_upload(server_task, abs_path);
	} 
	else 
	{
		HttpResponse *resp = server_task->get_resp();
		resp->set_status_code("405");
		resp->append_output_body("405 Method Not Allowed.");
	}
}

static WFFacilities::WaitGroup wait_group(1);

void sig_handler(int signo)
{
	printf("recv signo:%d\n", signo);
	wait_group.done();
}

int main(int argc, char *argv[])
{
	if (argc != 2 && argc != 3 && argc != 5)
	{
		fprintf(stderr, "%s <port> [root path] [cert file] [key file]\n",
				argv[0]);
		exit(1);
	}


	unsigned short port = atoi(argv[1]);
	const char *root = (argc >= 3 ? argv[2] : ".");
	auto&& proc = std::bind(process, std::placeholders::_1, root);
	WFHttpServer server(proc);
	std::string scheme;
	int ret;

	if (argc == 5)
	{
		ret = server.start(port, argv[3], argv[4]);	/* https server */
		scheme = "https://";
	}
	else
	{
		ret = server.start(port);
		scheme = "http://";
	}

	if (ret < 0)
	{
		perror("start server");
		exit(1);
	}

	printf("file server start.[%d] root:%s\n", port, root);

	wait_group.wait();

	server.stop();
	return 0;
}

