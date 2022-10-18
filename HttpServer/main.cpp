#include"HttpServer.h"
#include<iostream>

bool httpCallBack(HttpRequest* Request, HttpResponse* Response, std::string& reData)
{
	if (Request->method() != "POST")
	{
		reData = u8"请用POST请求";
		Response->setState(400);
		Response->setVersion("HTTP/1.1");
		Response->addHeader(std::make_pair(std::string("Content-Length"), std::to_string(reData.size())));
		return true;
	}
	reData = u8"请求成功";

	std::cout << Request->postData() << std::endl;

	Response->setState(200);
	Response->setVersion("HTTP/1.1");
	Response->addHeader(std::make_pair(std::string("Content-Length"), std::to_string(reData.size())));
	return true;
}

int main()
{
	HttpServer http(9999, httpCallBack, 6);
	if (http.getState() != HttpServer::s_error)
	{
		http.run();
	}
	
	getchar();
	return 0;
}