#pragma once

#ifdef _WIN32
#include<WinSock2.h>
#include <WS2tcpip.h>
typedef SOCKET SockHandle;
#pragma comment(lib,"ws2_32.lib")

static int __stream_socketpair(struct addrinfo* addr_info, SOCKET sock[2]);
static int __dgram_socketpair(struct addrinfo* addr_info, SOCKET sock[2]);
int socketpair(int family, int type, int protocol, SOCKET recv[2]);
#endif // _WIN32
#ifdef linux
#include<sys/select.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<memory.h>
#include<fcntl.h>
typedef int SockHandle;
#endif // _LINUX

#include <stdlib.h>
#include <stdio.h>

#include<functional>
#include<mutex>
#include<thread>
#include<queue>
#include<map>
#include<string>
#include<iostream>

//log
#define debug

#ifdef _WIN32
#ifdef debug

#define LOG(arg) \
OutputDebugStringA(arg)
#endif // debug
#endif // _WIN32

#ifdef linux
#ifdef debug

#define LOG(arg) \
printf(arg)
#endif // debug
#endif // linux





class Channel
{
public:
	enum EventType
	{
		read = 0x2,
		write = 0x4,
		close = 0x8,
	};
	Channel(
		SockHandle fd,
		unsigned int events,
		std::function<void(void*)> readCall,
		std::function<void(void*)> writeCall,
		std::function<void(void*)> closeCall,
		void* arg
	);
	SockHandle fd();
	bool isWriteEvent();
	unsigned int events();
	void writeEvent(bool flag);
	void readCall();
	void writeCall();
	void closeCall();
private:
	std::function<void(void*)> _readCall;
	std::function<void(void*)> _writeCall;
	std::function<void(void*)> _closeCall;
	SockHandle _fd;
	unsigned int _event;
	void* _arg;
	
};

class ChannelElement
{
public:
	enum TaskType
	{
		ADD, DELETES, MODIFY
	};
	TaskType type;   // 如何处理该节点中的channel
	Channel* channel;
};

class EventLoop;
class SelectDispatcher 
{
public:
	SelectDispatcher();
	virtual bool add(Channel& channel);
	virtual bool remove(Channel& channel);
	virtual bool modify(Channel& channel);
	virtual void dispatch(EventLoop& evLoop, int timeOut);
	virtual ~SelectDispatcher();
private:
	bool setFdSet(Channel& channel);
	bool unsetFdSet(Channel& channel);
	fd_set _readSet;
	fd_set _writeSet;
};

class EventLoop
{
public:
	EventLoop();
	EventLoop(std::string thName);
	bool run();
	bool close();
	bool addTask(Channel& channel, ChannelElement::TaskType type);
	void processTask();
	void eventActivate(SockHandle fd, int events);
	std::thread::id getThId();
	void setName(std::string &name);
	~EventLoop();
	void readLocalMessage(void* arg);
	void taskWakeup();
private:
	//判断是否退出
	bool _isQuit;
	SelectDispatcher* _SelectDispatcher;
	//线程id
	std::thread::id _thId;
	//线程名字
	std::string _thName;
	//锁
	std::mutex _mtx;
	//socket对
	SockHandle _sockPair[2];
	//任务队列
	std::queue<ChannelElement> _que;
	//
	std::map<SockHandle, Channel*> _list;
};

class ReactorPool
{
public:
	ReactorPool(EventLoop* mainLoop, int thNum);
	~ReactorPool();
	void run();
	void close();
	EventLoop* takeWorkerEventLoop();
	void WorkIng(int id);
private:
	EventLoop* _mainLoop;
	bool _isStart;
	int _thNum;
	std::vector<EventLoop*> _workEvLoopList;
	int _index;
	std::vector<std::thread*> _tharr;
	std::mutex _mtx;
};
class Buffer
{
public:
	Buffer(size_t size);
	size_t size();
	void ExtendRoom(size_t size);
	size_t readSize();
	size_t writeSize();
	void append(const std::string data);
	size_t scoketRecv(SockHandle fd);
	size_t socketSend(SockHandle fd);
	std::string getAEction(std::string endStr);
	std::string getAllData();
private:
	std::string _data;
	size_t _readPos;
	size_t _writePos;
};

class HttpRequest
{
public:
	HttpRequest();
	void clear();
	bool parse(Buffer& buff);
	std::string& method();
	std::string& url();
	std::string& path();
	std::map<std::string, std::string>& GET();
	std::string& version();
	std::map<std::string, std::string>& header();
	std::string& postData();
	std::map<std::string, std::string>& POST();
private:
	std::string _method;
	std::string _url;
	std::string _path;
	std::map<std::string, std::string> _GET;
	std::string _version;
	std::map<std::string, std::string> _header;
	std::string _postData;
	std::map<std::string, std::string> _POST;
};

class HttpResponse
{
public:
	HttpResponse();
	void setVersion(std::string v);
	void setState(int state);
	void addHeader(std::pair<std::string, std::string> key_val);
	void clear();
	std::string makeHeadString();
private:

	std::string _version;
	unsigned int _state;
	std::string _stateString;
	std::map<std::string, std::string> _header;
};


class HttpConnection
{
public:
	HttpConnection(SockHandle fd, EventLoop* evLoop, std::function<bool(HttpConnection*, std::string&)> requestCall, std::function<void(HttpConnection*)> closeCall);
	~HttpConnection();
	HttpRequest* Request();
	HttpResponse* Response();
	void SetWriteCall(std::function<bool(HttpConnection*, std::string&)> writeCall);
private:
	void writeProcess(void* arg); //TCP写事件
	void readProcess(void* arg); //TCP读事件
	void closeProcess(void* arg); //TCP关闭事件
	EventLoop* _evLoop;
	Buffer* _rbuff;
	Buffer* _wbuff;
	Channel* _channel;
	HttpRequest* _HttpRequest;
	HttpResponse* _HttpResponse;
	std::function<bool(HttpConnection*, std::string&)> _requestCall; //请求回调
	std::function<bool(HttpConnection*, std::string&)> _writeCall;//写回调
	std::function<void(HttpConnection*)> _closeCall; //关闭回调

};

class HttpServer
{
public:
	enum state
	{
		s_run,
		s_error,
		s_close
	};
	HttpServer(int port, int thNum);
	virtual ~HttpServer();
	bool run();
	state getState();
	//继承重写他
	virtual bool processHttpRequest(HttpConnection* conn, std::string& reData);
	virtual void processHttpClose(HttpConnection* conn);
	static const char* getFiletype(const char* fileName);
private:
	void acceptConnect(void* arg);
	void WinSockClose();
	void closefd();
	bool listenInit(int port);
	SockHandle _fd;
	int _port;
	int _thNum;
	state _state;
	EventLoop* _mainLoop;
	ReactorPool* _reactorPool;
};


