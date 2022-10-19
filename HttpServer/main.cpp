#include"HttpServer.h"
#include<iostream>
#include<filesystem>
#include<fstream>

class myHttpServer :public HttpServer
{
public:
	myHttpServer(int port,int thNum):HttpServer(port,thNum)
	{}
	//重写httpServer processHttpRequest 实现处理http请求 该函数会被多个线程调用 
	virtual bool processHttpRequest(HttpConnection* conn, std::string& reData)
	{
		
		std::string path = conn->Request()->path();
		if (path == "/")
		{
			path = "./";
		}
		else
		{
			path.erase(path.begin());
		}
		std::filesystem::path file(path);
		if (!std::filesystem::exists(file))
		{
			//没有这个路径
			std::cout << "文件或者目录不存在" << std::endl;
			conn->Response()->setState(404);
			conn->Response()->setVersion("HTTP/1.1");
			conn->Response()->addHeader(std::make_pair(std::string("Content-Type"), getFiletype(".html")));
			conn->Response()->addHeader(std::make_pair(std::string("Content-Length"), std::string("0")));
			return true;

		}
		//判断发送文件还目录
		if (std::filesystem::is_directory(file))
		{
			//是目录
			std::cout << "开始发送目录" << std::endl;


			//组织目录的数据
			std::string dirHtml = "<html><head><title>" + path + "</title></head><body><table>";
			auto begin = std::filesystem::directory_iterator(file); //获取文件系统迭代器
			auto end = std::filesystem::directory_iterator();    //end迭代器
			for (auto it = begin; it != end; it++) {
				auto& entry = *it;
				std::string fileName = entry.path().string();
				fileName.erase(0, path.size());
				size_t fileSize = entry.file_size();
				if (std::filesystem::is_regular_file(entry)) {
					//如果是文件
					dirHtml += "<tr><td><a href=\"" + fileName + "\">" + fileName + "</a></td><td>" + std::to_string(fileSize) + "</td><td>FILE</td></tr>";
				}
				else if (std::filesystem::is_directory(entry)) {
					dirHtml += "<tr><td><a href=\"" + fileName + "/\">" + fileName + "</a></td><td>" + std::to_string(fileSize) + "</td><td>DIR</td></tr>";
				}
			}
			dirHtml += "</table></body></html>";
			conn->Response()->setState(200);
			conn->Response()->setVersion("HTTP/1.1");
			conn->Response()->addHeader(std::make_pair(std::string("Content-Type"), getFiletype(".html")));
			conn->Response()->addHeader(std::make_pair(std::string("Content-Length"), std::to_string(dirHtml.size())));
			reData = dirHtml;
			std::cout << "目录大小:" << dirHtml.size() << std::endl;
			//返回true 告诉服务器我已经成功处理
			return true;
		}
		else
		{
			//是文件
			
#define SEND_MAX_FILE //发送大文件方式

#ifdef SEND_MAX_FILE
			//先打开下看能不能打开 不能打开直接返回502
			std::ifstream* fileHander = new std::ifstream(path, std::ios::binary | std::ios::ate);
			size_t fileSize = 0;
			int state = 200;
			if (fileHander->is_open())
			{
				fileSize = fileHander->tellg();
			}
			else
			{
				state = 502;
				fileSize = 0;
			}
			std::cout << "文件大小" << fileSize << std::endl;
			conn->Response()->setState(state);
			conn->Response()->setVersion("HTTP/1.1");
			conn->Response()->addHeader(std::make_pair(std::string("Content-Type"), getFiletype(state / 100 == 2 ? path.c_str() : ".html")));
			conn->Response()->addHeader(std::make_pair(std::string("Content-Length"), std::to_string(fileSize)));
			if (state == 502)
			{
				fileHander->close();
				delete fileHander;
				return true;
			}
			fileTask* task = new fileTask(fileHander, 0, fileSize);
			//把任务添加到 _fileTaskList
			_mtx.lock();
			_fileTaskList.insert(std::make_pair(conn, task));
			_mtx.unlock();
			//把http连接的发送数据回调指向 myHttpServer::sendFile 让他来发送数据
			conn->SetWriteCall(std::bind(&myHttpServer::sendFile, this, std::placeholders::_1, std::placeholders::_2));
			//返回true 告诉服务器我已经成功处理
			return true;
#else
			//这种是发送小文件方式
			std::ifstream fileHander(path, std::ios::binary | std::ios::ate);
			size_t fileSize = 0;
			int state = 200;
			std::string fileData = "";
			if (fileHander.is_open())
			{
				fileSize = fileHander.tellg();
				fileHander.seekg(std::ios::beg);
				fileData += std::string(fileSize, 0);
				fileHander.read(&fileData[0], fileSize);
			}
			else
			{
				state = 502;
				fileSize = 0;
			}

			conn->Response()->setState(state);
			conn->Response()->setVersion("HTTP/1.1");
			conn->Response()->addHeader(std::make_pair(std::string("Content-Type"), getFiletype(state / 100 == 2 ? path.c_str() : ".html")));
			conn->Response()->addHeader(std::make_pair(std::string("Content-Length"), std::to_string(fileSize)));
			reData = fileData;
			return true;
#endif // SEND_MAX_FILE

			
		}
	}
	//重写httpServer processHttpClose 实现处理http连接关闭处理 该函数会被多个线程调用
	virtual void processHttpClose(HttpConnection* conn) {
		//连接已经关闭 清除他的任务
		std::cout << "连接断开 开始清除任务" << std::endl;
		std::lock_guard<std::mutex> lock(_mtx); //上个锁
		auto it = _fileTaskList.find(conn);
		if (it != _fileTaskList.end())
		{
			delete it->second;
			_fileTaskList.erase(it);
		}
	}
	//返回值是是否已经发送完 true 是已经发送完 false是没有发完
	//该函数会被一直调用 直到数据被发完 或者客户端断开了流 或者断开了连接
	bool sendFile(HttpConnection* conn, std::string& reData)
	{
		std::lock_guard<std::mutex> lock(_mtx); //上个锁
		auto it = _fileTaskList.find(conn);
		if (it != _fileTaskList.end())
		{
			//处理
			std::string buff(10240,0);
			std::cout << "开始发送文件" << std::endl;
			fileTask* task = it->second;
			task->fileHandle->seekg(task->begin_pos);
			task->fileHandle->read(&buff[0], buff.size());
			size_t count = task->fileHandle->gcount();
			task->begin_pos += count;
			reData = std::string(buff.c_str(), count);
			if (task->begin_pos >= task->end_pos)
			{
				//删除这个任务
				delete task;
				_fileTaskList.erase(it);
				std::cout << "文件已经全部发送完成" << std::endl;
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			//没有找到那就已经发完了
			return true;
		}
	}
private:
	class fileTask
	{
	public:
		fileTask(std::ifstream* f, size_t b_pos, size_t n_pos) 
			:fileHandle(f), begin_pos(b_pos), end_pos(n_pos)
		{}
		~fileTask()
		{
			if (fileHandle != nullptr)
			{
				if (fileHandle->is_open())
				{
					fileHandle->close();
					delete fileHandle;
					fileHandle = nullptr;
				}
			}
		}
		std::ifstream* fileHandle;
		size_t begin_pos;
		size_t end_pos;
	};
	std::mutex _mtx;
	std::map<HttpConnection*, fileTask*> _fileTaskList;
};

int main()
{
	// 端口 反应堆工作反应堆个数
	myHttpServer http(9999, 6);
	if (http.getState() != HttpServer::s_error)
	{
		http.run();
	}
	return 0;
}