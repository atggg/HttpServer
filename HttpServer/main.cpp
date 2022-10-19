#include"HttpServer.h"
#include<iostream>
#include<filesystem>
#include<fstream>

class myHttpServer :public HttpServer
{
public:
	myHttpServer(int port,int thNum):HttpServer(port,thNum)
	{}
	//��дhttpServer processHttpRequest ʵ�ִ���http���� �ú����ᱻ����̵߳��� 
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
			//û�����·��
			std::cout << "�ļ�����Ŀ¼������" << std::endl;
			conn->Response()->setState(404);
			conn->Response()->setVersion("HTTP/1.1");
			conn->Response()->addHeader(std::make_pair(std::string("Content-Type"), getFiletype(".html")));
			conn->Response()->addHeader(std::make_pair(std::string("Content-Length"), std::string("0")));
			return true;

		}
		//�жϷ����ļ���Ŀ¼
		if (std::filesystem::is_directory(file))
		{
			//��Ŀ¼
			std::cout << "��ʼ����Ŀ¼" << std::endl;


			//��֯Ŀ¼������
			std::string dirHtml = "<html><head><title>" + path + "</title></head><body><table>";
			auto begin = std::filesystem::directory_iterator(file); //��ȡ�ļ�ϵͳ������
			auto end = std::filesystem::directory_iterator();    //end������
			for (auto it = begin; it != end; it++) {
				auto& entry = *it;
				std::string fileName = entry.path().string();
				fileName.erase(0, path.size());
				size_t fileSize = entry.file_size();
				if (std::filesystem::is_regular_file(entry)) {
					//������ļ�
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
			std::cout << "Ŀ¼��С:" << dirHtml.size() << std::endl;
			//����true ���߷��������Ѿ��ɹ�����
			return true;
		}
		else
		{
			//���ļ�
			
#define SEND_MAX_FILE //���ʹ��ļ���ʽ

#ifdef SEND_MAX_FILE
			//�ȴ��¿��ܲ��ܴ� ���ܴ�ֱ�ӷ���502
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
			std::cout << "�ļ���С" << fileSize << std::endl;
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
			//��������ӵ� _fileTaskList
			_mtx.lock();
			_fileTaskList.insert(std::make_pair(conn, task));
			_mtx.unlock();
			//��http���ӵķ������ݻص�ָ�� myHttpServer::sendFile ��������������
			conn->SetWriteCall(std::bind(&myHttpServer::sendFile, this, std::placeholders::_1, std::placeholders::_2));
			//����true ���߷��������Ѿ��ɹ�����
			return true;
#else
			//�����Ƿ���С�ļ���ʽ
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
	//��дhttpServer processHttpClose ʵ�ִ���http���ӹرմ��� �ú����ᱻ����̵߳���
	virtual void processHttpClose(HttpConnection* conn) {
		//�����Ѿ��ر� �����������
		std::cout << "���ӶϿ� ��ʼ�������" << std::endl;
		std::lock_guard<std::mutex> lock(_mtx); //�ϸ���
		auto it = _fileTaskList.find(conn);
		if (it != _fileTaskList.end())
		{
			delete it->second;
			_fileTaskList.erase(it);
		}
	}
	//����ֵ���Ƿ��Ѿ������� true ���Ѿ������� false��û�з���
	//�ú����ᱻһֱ���� ֱ�����ݱ����� ���߿ͻ��˶Ͽ����� ���߶Ͽ�������
	bool sendFile(HttpConnection* conn, std::string& reData)
	{
		std::lock_guard<std::mutex> lock(_mtx); //�ϸ���
		auto it = _fileTaskList.find(conn);
		if (it != _fileTaskList.end())
		{
			//����
			std::string buff(10240,0);
			std::cout << "��ʼ�����ļ�" << std::endl;
			fileTask* task = it->second;
			task->fileHandle->seekg(task->begin_pos);
			task->fileHandle->read(&buff[0], buff.size());
			size_t count = task->fileHandle->gcount();
			task->begin_pos += count;
			reData = std::string(buff.c_str(), count);
			if (task->begin_pos >= task->end_pos)
			{
				//ɾ���������
				delete task;
				_fileTaskList.erase(it);
				std::cout << "�ļ��Ѿ�ȫ���������" << std::endl;
				return true;
			}
			else
			{
				return false;
			}
		}
		else
		{
			//û���ҵ��Ǿ��Ѿ�������
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
	// �˿� ��Ӧ�ѹ�����Ӧ�Ѹ���
	myHttpServer http(9999, 6);
	if (http.getState() != HttpServer::s_error)
	{
		http.run();
	}
	return 0;
}