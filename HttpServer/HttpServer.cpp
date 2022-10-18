#include "HttpServer.h"
#define MAX 1024

#ifdef _WIN32

static int __stream_socketpair(struct addrinfo* addr_info, SOCKET sock[2]) {
    SOCKET listener, client, server;
    int opt = 1;

    listener = server = client = INVALID_SOCKET;
    listener = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol); //创建服务器socket并进行绑定监听等
    if (INVALID_SOCKET == listener)
        goto fail;

    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (SOCKET_ERROR == bind(listener, addr_info->ai_addr, (int)addr_info->ai_addrlen))
        goto fail;

    if (SOCKET_ERROR == getsockname(listener, addr_info->ai_addr, (int*)&addr_info->ai_addrlen))
        goto fail;

    if (SOCKET_ERROR == listen(listener, 5))
        goto fail;

    client = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol); //创建客户端socket，并连接服务器

    if (INVALID_SOCKET == client)
        goto fail;

    if (SOCKET_ERROR == connect(client, addr_info->ai_addr, (int)addr_info->ai_addrlen))
        goto fail;

    server = accept(listener, 0, 0);

    if (INVALID_SOCKET == server)
        goto fail;

    closesocket(listener);

    sock[0] = client;
    sock[1] = server;

    return 0;
fail:
    if (INVALID_SOCKET != listener)
        closesocket(listener);
    if (INVALID_SOCKET != client)
        closesocket(client);
    return -1;
}

static int __dgram_socketpair(struct addrinfo* addr_info, SOCKET sock[2])
{
    SOCKET client, server;
    struct addrinfo addr, * result = NULL;
    const char* address;
    int opt = 1;

    server = client = INVALID_SOCKET;

    server = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
    if (INVALID_SOCKET == server)
        goto fail;

    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    if (SOCKET_ERROR == bind(server, addr_info->ai_addr, (int)addr_info->ai_addrlen))
        goto fail;

    if (SOCKET_ERROR == getsockname(server, addr_info->ai_addr, (int*)&addr_info->ai_addrlen))
        goto fail;

    client = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
    if (INVALID_SOCKET == client)
        goto fail;

    memset(&addr, 0, sizeof(addr));
    addr.ai_family = addr_info->ai_family;
    addr.ai_socktype = addr_info->ai_socktype;
    addr.ai_protocol = addr_info->ai_protocol;

    if (AF_INET6 == addr.ai_family)
        address = "0:0:0:0:0:0:0:1";
    else
        address = "127.0.0.1";

    if (getaddrinfo(address, "0", &addr, &result))
        goto fail;

    setsockopt(client, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    if (SOCKET_ERROR == bind(client, result->ai_addr, (int)result->ai_addrlen))
        goto fail;

    if (SOCKET_ERROR == getsockname(client, result->ai_addr, (int*)&result->ai_addrlen))
        goto fail;

    if (SOCKET_ERROR == connect(server, result->ai_addr, (int)result->ai_addrlen))
        goto fail;

    if (SOCKET_ERROR == connect(client, addr_info->ai_addr, (int)addr_info->ai_addrlen))
        goto fail;

    freeaddrinfo(result);
    sock[0] = client;
    sock[1] = server;
    return 0;

fail:
    if (INVALID_SOCKET != client)
        closesocket(client);
    if (INVALID_SOCKET != server)
        closesocket(server);
    if (result)
        freeaddrinfo(result);
    return -1;
}

int socketpair(int family, int type, int protocol, SOCKET recv[2]) {
    const char* address;
    struct addrinfo addr_info, * p_addrinfo;
    int result = -1;

    memset(&addr_info, 0, sizeof(addr_info));
    addr_info.ai_family = family;
    addr_info.ai_socktype = type;
    addr_info.ai_protocol = protocol;
    if (AF_INET6 == family)
        address = "0:0:0:0:0:0:0:1";
    else
        address = "127.0.0.1";

    if (0 == getaddrinfo(address, "0", &addr_info, &p_addrinfo)) {
        if (SOCK_STREAM == type)
            result = __stream_socketpair(p_addrinfo, (SOCKET*)recv);   //use for tcp
        else if (SOCK_DGRAM == type)
            result = __dgram_socketpair(p_addrinfo, (SOCKET*)recv);    //use for udp
        freeaddrinfo(p_addrinfo);
    }
    return result;
}

#endif // _WIN32


Channel::Channel(SockHandle fd, unsigned int events, std::function<void(void*)> readCall, std::function<void(void*)> writeCall, std::function<void(void*)> closeCall, void* arg)
	:_fd(fd), _event(events), _arg(arg), _readCall(readCall), _writeCall(writeCall), _closeCall(closeCall)
{}

SockHandle Channel::fd()
{
	return _fd;
}

unsigned int Channel::events()
{
	return _event;
}


bool Channel::isWriteEvent()
{
    return _event & write;
}

void Channel::writeEvent(bool flag)
{
    if (flag)
    {
        _event |= write;
    }
    else
    {
        _event &= ~write;
    }
}

void Channel::readCall()
{
    if (_readCall != nullptr)
    {
        _readCall(_arg);
    }
}

void Channel::writeCall()
{
    if (_writeCall != nullptr)
    {
        _writeCall(_arg);
    }
}

void Channel::closeCall()
{
    if (_closeCall != nullptr)
    {
        _closeCall(_arg);
    }
}


SelectDispatcher::SelectDispatcher()
{
	FD_ZERO(&_readSet);
	FD_ZERO(&_writeSet);
}

bool SelectDispatcher::add(Channel& channel)
{
	return setFdSet(channel);
}

bool SelectDispatcher::remove(Channel& channel)
{
	return unsetFdSet(channel);
}

bool SelectDispatcher::modify(Channel& channel)
{
	unsetFdSet(channel);
	setFdSet(channel);
	return true;
}

void SelectDispatcher::dispatch(EventLoop& evLoop, int timeOut)
{
	timeval tv;
	tv.tv_sec = timeOut;
	tv.tv_usec = 0;
	fd_set tRead = _readSet;
	fd_set tWrite = _writeSet;
	int count = select(MAX, &tRead, &tWrite, nullptr, &tv);
	for (int i = 0; i < MAX; i++)
	{
		if(FD_ISSET(i, &tRead))
		{
			evLoop.eventActivate(i, Channel::read);
		}
		if (FD_ISSET(i, &tWrite))
		{
			evLoop.eventActivate(i, Channel::write);
		}
	}
}

SelectDispatcher::~SelectDispatcher()
{
}

bool SelectDispatcher::setFdSet(Channel& channel)
{
	if (channel.events() & Channel::read)
	{
		FD_SET(channel.fd(), &_readSet);
	}
	if (channel.events() & Channel::write)
	{
		FD_SET(channel.fd(), &_writeSet);
	}
	return true;
}

bool SelectDispatcher::unsetFdSet(Channel& channel)
{
	FD_CLR(channel.fd(),&_readSet);
	FD_CLR(channel.fd(),&_writeSet);
	return true;
}

EventLoop::EventLoop():EventLoop("MainThread")
{

}

EventLoop::EventLoop(std::string thName)
{
	_thName = thName;
	_isQuit = false;
	_SelectDispatcher = new SelectDispatcher();
	_thId = std::this_thread::get_id();
    int ret = socketpair(AF_INET, SOCK_STREAM, 0, _sockPair);
    if (ret == -1)
    {
        //std::cout << "socketpair Error" << std::endl;
        std::exit(0);
    }
    Channel channel(_sockPair[1], Channel::read, std::bind(&EventLoop::readLocalMessage, this, std::placeholders::_1), nullptr, nullptr, this);
    addTask(channel, ChannelElement::ADD);
}

bool EventLoop::run()
{
    if (std::this_thread::get_id() != _thId)
    {
        return false;
    }
    while (!_isQuit)
    {
        _SelectDispatcher->dispatch(*this, 200);
        processTask();
    }
    return true;
}

bool EventLoop::close()
{
    return _isQuit = true;
}

bool EventLoop::addTask(Channel& channel, ChannelElement::TaskType type)
{
    _mtx.lock();
    ChannelElement node;
    node.channel = new Channel(channel);
    node.type = type;
    _que.push(node);
    _mtx.unlock();
    if (_thId == std::this_thread::get_id())
    {
        //当前子线程
        processTask();
    }
    else
    {
        //当前主线程
        taskWakeup();
    }
    return true;
}

void EventLoop::processTask()
{
    _mtx.lock();
    while (!_que.empty())
    {
        ChannelElement node = _que.front();
        _que.pop();
        if (node.type == ChannelElement::ADD)
        {
            _list.insert(std::make_pair(node.channel->fd(), node.channel));
            _SelectDispatcher->add(*node.channel);
        }
        else if (node.type == ChannelElement::DELETES)
        {
            auto c = _list.find(node.channel->fd());
            if (c != _list.end())
            {
                _SelectDispatcher->remove(*node.channel);
                delete c->second;
                _list.erase(c);
                delete node.channel;
            }
        }
        else if (node.type == ChannelElement::MODIFY)
        {
            auto c = _list.find(node.channel->fd());
            if (c != _list.end())
            {
                delete c->second;
                c->second = node.channel;
                _SelectDispatcher->modify(*node.channel);
            }
        }
    }
    _mtx.unlock();
}

void EventLoop::eventActivate(SockHandle fd, int events)
{
    auto c = _list.find(fd);
    if (c != _list.end())
    {
        Channel* channel = c->second;
        //读事件
        if (Channel::read & events)
        {
            channel->readCall();
        }
        //写事件
        else if (Channel::write & events)
        {
            channel->writeCall();
        }
        //断开事件
        else if (Channel::close & events)
        {
            channel->closeCall();
            addTask(*channel, ChannelElement::DELETES);
        }
    }
}

std::thread::id EventLoop::getThId()
{
	return _thId;
}

void EventLoop::setName(std::string& name)
{
    _thName = name;
}

EventLoop::~EventLoop()
{
    if (_SelectDispatcher != nullptr)
    {
        delete _SelectDispatcher;
        _SelectDispatcher = nullptr;
    }
}

void EventLoop::readLocalMessage(void* arg)
{
    EventLoop* evLoop = (EventLoop*)arg;
    char buf[256];
    recv(evLoop->_sockPair[1], buf, sizeof(buf), 0);
}

void EventLoop::taskWakeup()
{
    const std::string msg = "test";
    send(_sockPair[0], msg.c_str(), (int)msg.size(), 0);
}

ReactorPool::ReactorPool(EventLoop* mainLoop, int thNum)
{
    _mainLoop = mainLoop;
    _isStart = true;
    _thNum = thNum;
    _index = 0;
}

ReactorPool::~ReactorPool()
{
    close();
    _isStart = false;
}

void ReactorPool::run()
{
    for (int i = 0; i < _thNum; i++)
    {
        new std::thread(&ReactorPool::WorkIng, this,i);
    }
    _isStart = true;
}

void ReactorPool::close()
{
    for (auto it = _workEvLoopList.begin(); it != _workEvLoopList.end(); it++)
    {
        (*it)->close();
        delete (*it);
    }
    _workEvLoopList.clear();
}

EventLoop* ReactorPool::takeWorkerEventLoop()
{
    if (_mainLoop->getThId() != std::this_thread::get_id())
    {
        std::exit(0);
    }
    EventLoop* evLoop = _mainLoop;
    if (_thNum > 0)
    {
        evLoop = _workEvLoopList[_index];
        _index = ++_index % _thNum;
    }
    return evLoop;
}

void ReactorPool::WorkIng(int id)
{
    EventLoop* evloop = new EventLoop("Thread-" + std::to_string(id));
    _mtx.lock();
    _workEvLoopList.push_back(evloop);
    _mtx.unlock();
    evloop->run();
}

HttpServer::HttpServer(int port, std::function<bool(HttpRequest*, HttpResponse*, std::string&)> processCall, int thNum)
    :_thNum(thNum),_port(port),_processCall(processCall), _state(s_close), _mainLoop(nullptr), _reactorPool(nullptr)
{
    if (!listenInit(_port))
    {
        _state = s_error;
        return;
    }
    _mainLoop = new EventLoop();
    _reactorPool = new ReactorPool(_mainLoop, _thNum);
}

bool HttpServer::run()
{
    _reactorPool->run();
    Channel channel(_fd, Channel::read, std::bind(&HttpServer::acceptConnect, this, std::placeholders::_1), nullptr, nullptr, this);
    _mainLoop->addTask(channel, ChannelElement::ADD);
    _mainLoop->run();
    return true;
}

HttpServer::state HttpServer::getState()
{
    return _state;
}

void HttpServer::acceptConnect(void* arg)
{
    if (arg != this)
    {
        return;
    }
    sockaddr_in caddr;
#ifdef _WIN32
    int len = sizeof(caddr);
#endif // _WIN32
#ifdef linux
    unsigned int len = sizeof(caddr);
#endif // linux


    SockHandle cfd = accept(_fd, (sockaddr*)&caddr, &len);


    if (cfd != -1)
    {
        //std::cout << "收到一个连接" << std::endl;
        new HttpConnection(cfd, _reactorPool->takeWorkerEventLoop(), _processCall);
    }
}

HttpServer::~HttpServer()
{
    closefd();
    WinSockClose();
    if (_mainLoop != nullptr)
    {
        _mainLoop->close();
        delete _mainLoop;
        _mainLoop = nullptr;
    }
    if (_reactorPool != nullptr)
    {
        delete _reactorPool;
        _reactorPool = nullptr;
    }
}

void HttpServer::WinSockClose()
{
#ifdef _WIN32
    WSACleanup();
#endif // _WIN32
}

void HttpServer::closefd()
{
#ifdef _WIN32
    closesocket(_fd);
#endif // _WIN32
#ifdef _LINUX
    close(_fd);
#endif // _LINUX


}


bool HttpServer::listenInit(int port)
{
#ifdef _WIN32
    //初始化WSA  
    WORD sockVersion = MAKEWORD(2, 2);
    WSADATA wsaData;
    if (WSAStartup(sockVersion, &wsaData) != 0)
    {
        return false;
    }
#endif // _WIN32
    _fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_fd==-1)
    {
        WinSockClose();
        return false;
    }
    sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(_port);
#ifdef _WIN32
    saddr.sin_addr.S_un.S_addr = 0;
#endif // _WIN32
#ifdef linux
    saddr.sin_addr.s_addr = 0;
#endif // LINUX
    
    int rc = 0;
    rc = bind(_fd, (sockaddr*)&saddr, sizeof(saddr));
    if (rc == -1)
    {
        closefd();
        WinSockClose();
        return false;
    }
    rc = listen(_fd, 128);
    if (rc == -1)
    {
        closefd();
        WinSockClose();
        return false;
    }
    return true;

}

Buffer::Buffer(size_t size) :_readPos(0), _writePos(0)
{
    _data = std::string(size, 0);
}

size_t Buffer::size()
{
    return _data.size();
}

void Buffer::ExtendRoom(size_t size)
{
    if (readSize() >= size)
    {
        return;
    }
    else if (_readPos + writeSize() >= size)
    {
        memcpy(&_data[0], &_data[0] + _readPos, readSize());
        _writePos = readSize();
        _readPos = 0;
    }
    else
    {
        _data += std::string(_data.size() + size, 0);
    }

}

size_t Buffer::readSize()
{
    return _writePos - _readPos;
}

size_t Buffer::writeSize()
{
    return _data.size() - _writePos;
}
//写入数据
void Buffer::append(const std::string data)
{
    if (data.size() <= 0)
    {
        return;
    }
    ExtendRoom(data.size());
    memcpy(&_data[0] + _writePos, &data[0], data.size());
    _writePos += data.size();
}

//socket写入数据
size_t Buffer::scoketRecv(SockHandle fd)
{
    std::string buff(1024, 0);
    size_t count = 0;
    int len = 0;
    while ((len = recv(fd, &buff[0], 1024, 0)) != -1 && len != 0)
    {
        count += len;
        append(std::string(buff.c_str(), len));
    }
    return count;
}

size_t Buffer::socketSend(SockHandle fd)
{
    size_t len = send(fd, _data.c_str() + _readPos, (int)readSize(), 0);
    _readPos += len;
    return len;
}

//获取一段数据
std::string Buffer::getAEction(std::string endStr)
{
    size_t pos = 0;
    if (endStr.empty())
    {
        return std::string();
    }
    if ((pos = _data.find(endStr, _readPos)) == std::string::npos)
    {
        return std::string();
    }
    pos += endStr.size();
    std::string rtStr(_data.c_str() + _readPos, pos - _readPos);
    _readPos = pos;
    return rtStr;
}
//获取没有读取完的全部数据
std::string Buffer::getAllData()
{
    std::string rtStr(_data.c_str() + _readPos, _writePos - _readPos);
    _readPos = _writePos;
    return rtStr;
}

HttpRequest::HttpRequest() :_method(""), _url(""), _version(""), _postData("")
{
}

void HttpRequest::clear()
{
    _method.clear();
    _url.clear();
    _path.clear();
    _GET.clear();
    _version.clear();
    _header.clear();
    _postData.clear();
    _POST.clear();

}

bool HttpRequest::parse(Buffer& buff)
{
    //解析请求行的信息
    std::string line = buff.getAEction("\r\n");
    if (line != "")
    {
        size_t pos = line.find(" ", 0);
        size_t pos2 = 0;
        if (pos != std::string::npos)
        {
            _method = std::string(line.c_str(), pos);
        }
        else
        {
            return false;
        }
        pos += 1;
        pos2 = pos;
        pos = line.find(" ", pos);
        if (pos != std::string::npos)
        {
            _url = std::string(line.c_str() + pos2, pos - pos2);
        }
        else
        {
            return false;
        }
        pos += 1;
        pos2 = pos;
        pos = line.find("\r\n", pos);
        if (pos != std::string::npos)
        {
            _version = std::string(line.c_str() + pos2, pos - pos2);
        }
        else
        {
            return false;
        }
        pos = _url.find("?");
        std::string getData;
        if (pos != std::string::npos)
        {
            _path = std::string(_url.c_str(), pos);
            getData = std::string(_url.c_str() + pos + 1);
        }
        else
        {
            _path = _url;
        }
        pos = getData.find("&");
        while (pos != std::string::npos || getData != "")
        {
            bool flag = false;
            std::string getDataline;
            if (pos != std::string::npos)
            {
                getDataline = std::string(getData.c_str(), pos);
                getData = std::string(getData.c_str() + pos + 1);
            }
            else
            {
                getDataline = getData;
                flag = true;
            }
            pos = getDataline.find("=");
            if (pos != std::string::npos)
            {
                std::string key(getDataline.c_str(), pos);
                std::string val(getDataline.c_str() + pos + 1);
                _GET.insert(std::make_pair(key, val));
            }
            else
            {
                return false;
            }
            if (flag)
            {
                break;
            }
            pos = getData.find("&");
        }
    }
    else
    {
        return false;
    }
    //开始解析请求头信息
    std::string headline = buff.getAEction("\r\n");
    while (headline != "" && headline != "\r\n")
    {
        size_t pos = 0;
        size_t pos2 = 0;
        pos = headline.find(":");
        if (pos == std::string::npos)
        {
            return false;
        }
        std::string key(headline.c_str(), pos);
        pos += 1;
        pos2 = pos;
        pos = headline.find("\r\n");
        std::string val(headline.c_str() + pos2, pos - pos2);
        //去除首位空格
        auto fun = [](std::string& str) {
            size_t s = str.find_first_not_of(" ");
            size_t e = str.find_last_not_of(" ");
            str = str.substr(s, e - s + 1);
            return;
        };
        fun(val);
        fun(key);
        _header.insert(std::make_pair(key, val));

        headline = buff.getAEction("\r\n");
    }
    //解析post数据
    if (_method == "post" || _method == "POST")
    {
        _postData = buff.getAllData();
    }
    auto Content_Type = _header.find("Content-Type");
    if (_postData != "" && Content_Type != _header.end() && Content_Type->second == "application/x-www-form-urlencoded")
    {
        std::string PostData = _postData;
        size_t pos = PostData.find("&");
        while (pos != std::string::npos || PostData != "")
        {
            bool flag = false;
            std::string PostDataline;
            if (pos != std::string::npos)
            {
                PostDataline = std::string(PostData.c_str(), pos);
                PostData = std::string(PostData.c_str() + pos + 1);
            }
            else
            {
                PostDataline = PostData;
                flag = true;
            }
            pos = PostDataline.find("=");
            if (pos != std::string::npos)
            {
                std::string key(PostDataline.c_str(), pos);
                std::string val(PostDataline.c_str() + pos + 1);
                _POST.insert(std::make_pair(key, val));
            }
            if (flag)
            {
                break;
            }
            pos = PostData.find("&");
        }
    }
    return true;
}

std::string& HttpRequest::method()
{
    return _method;
}

std::string& HttpRequest::url()
{
    return _url;
}

std::string& HttpRequest::path()
{
    return _path;
}

std::map<std::string, std::string>& HttpRequest::GET()
{
    return _GET;
}

std::string& HttpRequest::version()
{
    return _version;
}

std::map<std::string, std::string>& HttpRequest::header()
{
    return _header;
}

std::string& HttpRequest::postData()
{
    return _postData;
}

std::map<std::string, std::string>& HttpRequest::POST()
{
    return _POST;
}

HttpResponse::HttpResponse() :_version(""), _state(0)
{
}

void HttpResponse::setVersion(std::string v)
{
    _version = v;
}

void HttpResponse::setState(int state)
{
    _state = state;
}

void HttpResponse::addHeader(std::pair<std::string, std::string> key_val)
{
    _header.insert(key_val);
}

void HttpResponse::clear()
{
    _version.clear();
    _state = 0;
    _stateString.clear();
    _header.clear();
}

std::string HttpResponse::makeHeadString()
{
    std::string headerString = "";
    headerString += _version + " " + std::to_string(_state) + " " + _stateString + "\r\n";
    for (auto m = _header.begin(); m != _header.end(); m++)
    {
        headerString += m->first + ": " + m->second + "\r\n";
    }
    headerString += "\r\n";
    return headerString;
}

HttpConnection::HttpConnection(SockHandle fd, EventLoop* evLoop, std::function<bool(HttpRequest*, HttpResponse*, std::string&)> processCall)
{
    _evLoop = evLoop;
    _rbuff = new Buffer(10240);
    _wbuff = new Buffer(10240);
    _HttpRequest = new HttpRequest;
    _HttpResponse = new HttpResponse;
    _processCall = processCall;
#ifdef _WIN32
    unsigned long ul = 1;
    ioctlsocket(fd, FIONBIO, &ul);
#endif // _WIN32
#ifdef linux
    int flag = 0;
    flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
#endif // linux

    _channel = new Channel(fd, Channel::read,
        std::bind(&HttpConnection::readProcess, this, std::placeholders::_1),
        std::bind(&HttpConnection::writeProcess, this, std::placeholders::_1),
        std::bind(&HttpConnection::closeProcess, this, std::placeholders::_1)
        , this);
    _evLoop->addTask(*_channel, ChannelElement::ADD);
}

HttpConnection::~HttpConnection()
{
    if (_rbuff != nullptr)
    {
        delete _rbuff;
        _rbuff = nullptr;
    }
    if (_wbuff != nullptr)
    {
        delete _wbuff;
        _wbuff = nullptr;
    }
    if (_channel != nullptr)
    {
        delete _channel;
        _channel = nullptr;
    }
    if (_HttpRequest != nullptr)
    {
        delete _HttpRequest;
        _HttpRequest = nullptr;
    }
    if (_HttpResponse != nullptr)
    {
        delete _HttpResponse;
        _HttpResponse = nullptr;
    }
}

void HttpConnection::writeProcess(void* arg)
{
    if (arg != this)
    {
        //std::cout << "writeProcess error" << std::endl;
        std::exit(0);
    }
    //std::cout << "写事件触发" << std::endl;
    _wbuff->socketSend(_channel->fd());
    if (_wbuff->readSize() == 0)
    {
        _channel->writeEvent(false);
        _evLoop->addTask(*_channel, ChannelElement::MODIFY);
    }
}

void HttpConnection::readProcess(void* arg)
{
    if (arg != this)
    {
        //std::cout << "writeProcess error" << std::endl;
        std::exit(0);
    }
    //std::cout << "读事件触发" << std::endl;
    size_t len = _rbuff->scoketRecv(_channel->fd());
    if (len == 0)
    {
        //调用关闭回调
        _evLoop->eventActivate(_channel->fd(), Channel::close);
        return;
    }
    if (_HttpRequest->parse(*_rbuff))
    {
        //解析请求头成功
        //处理http请求
        std::string data;
        bool rc = false;
        if (_processCall != nullptr)
        {
            rc = _processCall(_HttpRequest, _HttpResponse, data);
        }
        if (!rc)
        {
            _HttpResponse->setState(400);
            _HttpResponse->setVersion("HTTP/1.1");
            _HttpResponse->addHeader(std::make_pair(std::string("Content-Length"), std::string("0")));
            _wbuff->append(_HttpResponse->makeHeadString());
        }
        else
        {
            _wbuff->append(_HttpResponse->makeHeadString());
            _wbuff->append(data);
        }
        
    }
    else
    {
        //解析失败
        _HttpResponse->setState(400);
        _HttpResponse->setVersion("HTTP/1.1");
        _HttpResponse->addHeader(std::make_pair(std::string("Content-Length"), std::string("0")));
        _wbuff->append(_HttpResponse->makeHeadString());
    }
    //开启写事件
    _channel->writeEvent(true);
    _evLoop->addTask(*_channel, ChannelElement::MODIFY);
}

void HttpConnection::closeProcess(void* arg)
{
    if (arg != this)
    {
        //std::cout << "writeProcess error" << std::endl;
        std::exit(0);
    }
    //std::cout << "关闭事件触发" << std::endl;
    delete this;
}


