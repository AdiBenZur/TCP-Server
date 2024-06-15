#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
using namespace std;
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include <string>
#include <sstream>

struct SocketState
{
	SOCKET id;            // Socket handle
	int    recv;            // Receiving?
	int    send;            // Sending?
	int sendSubType;    // Sending sub-type
	char buffer[1024];
	int len;
	string queryParameter;
	time_t timeStamp;
	bool hasqueryParam = 0;
};

const int TIME_PORT = 8080;
const int MAX_SOCKETS = 60;
const int EMPTY = 0;
const int LISTEN = 1;
const int RECEIVE = 2;
const int IDLE = 3;
const int SEND = 4;

const int GET = 1;
const int HEAD = 2;
const int OPTIONS = 3;
const int PUT = 4;
const int POST = 5;
const int DELET_RESOURCE = 6;
const int TRACE = 7;
const int INVALID_REQUEST = 8;

const char* SUPPORTED_OPTIONS = "GET, HEAD, PUT, DELETE, OPTIONS, TRACE\n";


void initSocket(SOCKET& listenSocket);
bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
string extractQueryParams(const string& request);
string readFile(const string& filename);
string constructHTTPResponse(int statusCode, const string& contentType, const string& content);
void sendMessage(int index);

void optionsRequest(int index, int& statusCode, string& content,string& contentType);
void putRequest(int index, int& statusCode, string& content, string& contentType);
void getOrHeadRequest(int index, int& statusCode, string& content, string& contentType);
void traceRequest(int index, int& statusCode, string& content, string& contentType);
void deleteRequest(int index, int& statusCode, string& content, string& contentType);
void postRequest(int index, int& statusCode, string& content, string& contentType);
string getRequestBody(string request);


struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;

void main()
{
	SOCKET listenSocket;
	initSocket(listenSocket);
	addSocket(listenSocket, LISTEN);

	while (true)
	{
		fd_set waitRecv;
		FD_ZERO(&waitRecv);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
				FD_SET(sockets[i].id, &waitRecv);
		}

		fd_set waitSend;
		FD_ZERO(&waitSend);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].send == SEND)
				FD_SET(sockets[i].id, &waitSend);
		}

		int nfd;
		nfd = select(0, &waitRecv, &waitSend, NULL, NULL);
		if (nfd == SOCKET_ERROR)
		{
			cout << "Server: Error at select(): " << WSAGetLastError() << endl;
			WSACleanup();
			return;
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitRecv))
			{
				nfd--;
				switch (sockets[i].recv)
				{
				case LISTEN:
					acceptConnection(i);
					break;

				case RECEIVE:
					receiveMessage(i);
					break;
				}
			}
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitSend))
			{
				nfd--;
				switch (sockets[i].send)
				{
				case SEND:
					time_t now;
					time(&now);
					if (now - sockets[i].timeStamp <= 120)
					{
						sendMessage(i);
					}
					closesocket(sockets[i].id);
					removeSocket(i);
					break;
				}
			}
		}
	}

	cout << "Server: Closing Connection.\n";
	closesocket(listenSocket);
	WSACleanup();
}

void initSocket(SOCKET& listenSocket) 
{
	WSAData wsaData;

	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData)) 
	{
		cout << "Server: Error at WSAStartup()\n";
		return;
	}

	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (INVALID_SOCKET == listenSocket) 
	{
		cout << "Server: Error at socket(): " << WSAGetLastError() << endl;
		WSACleanup();
		return;
	}

	sockaddr_in serverService;
	serverService.sin_family = AF_INET;
	serverService.sin_addr.s_addr = INADDR_ANY;
	serverService.sin_port = htons(TIME_PORT);

	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService))) 
	{
		cout << "Server: Error at bind(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}

	if (SOCKET_ERROR == listen(listenSocket, 5)) 
	{
		cout << "Server: Error at listen(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}
}

bool addSocket(SOCKET id, int what)
{
	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		if (sockets[i].recv == EMPTY)
		{
			sockets[i].id = id;
			sockets[i].recv = what;
			sockets[i].send = IDLE;
			sockets[i].len = 0;
			socketsCount++;
			return (true);
		}
	}
	return (false);
}

void removeSocket(int index)
{
	sockets[index].recv = EMPTY;
	sockets[index].send = EMPTY;
	socketsCount--;
}

void acceptConnection(int index)
{
	SOCKET id = sockets[index].id;
	struct sockaddr_in from;		// Address of sending partner
	int fromLen = sizeof(from);

	SOCKET msgSocket = accept(id, (struct sockaddr*)&from, &fromLen);
	if (INVALID_SOCKET == msgSocket)
	{
		cout << "Server: Error at accept(): " << WSAGetLastError() << endl;
		return;
	}
	cout << "Server: Client " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << " is connected." << endl;

	unsigned long flag = 1;
	if (ioctlsocket(msgSocket, FIONBIO, &flag) != 0)
	{
		cout << "Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
	}

	if (addSocket(msgSocket, RECEIVE) == false)
	{
		cout << "\t\tToo many connections, dropped!\n";
		closesocket(id);
	}
	return;
}

void receiveMessage(int index) 
{
	SOCKET msgSocket = sockets[index].id;

	int len = sockets[index].len;
	int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

	if (SOCKET_ERROR == bytesRecv) 
	{
		cout << "Server: Error at recv(): " << WSAGetLastError() << endl;
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}

	if (bytesRecv == 0) 
	{
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	else 
	{
		sockets[index].buffer[len + bytesRecv] = '\0'; 
		cout << "Server: Received: " << bytesRecv << " bytes of \"" << &sockets[index].buffer[len] << "\" message.\n";
		sockets[index].send = SEND;
		sockets[index].recv = EMPTY;
		time(&sockets[index].timeStamp);
		
		string request = sockets[index].buffer;
		string methodToActivate = request.substr(0, request.find(' '));

		if (methodToActivate == "OPTIONS") 
		{
			sockets[index].sendSubType = OPTIONS;
		}
		else if (methodToActivate == "GET" || methodToActivate == "HEAD") 
		{
			sockets[index].sendSubType = (methodToActivate == "GET") ? GET : HEAD;
			sockets[index].queryParameter = extractQueryParams(request);
		}
		else if (methodToActivate == "POST") 
		{
			sockets[index].sendSubType = POST;
		}
		else if (methodToActivate == "PUT") 
		{
			sockets[index].sendSubType = PUT;
		}
		else if (methodToActivate == "DELETE") 
		{
			sockets[index].sendSubType = DELET_RESOURCE;
		}
		else if (methodToActivate == "TRACE") 
		{
			sockets[index].sendSubType = TRACE;
		}
		else
		{
			sockets[index].sendSubType = INVALID_REQUEST;
		}

		sockets[index].len += bytesRecv - (methodToActivate.length() + 1);
		memmove(sockets[index].buffer, sockets[index].buffer + (methodToActivate.length() + 1), sockets[index].len);
		sockets[index].buffer[sockets[index].len] = '\0';
	}
}

string extractQueryParams(const string& request) 
{
	string queryParams = "";
	size_t pos = request.find('?');
	if (pos != std::string::npos && pos < 20) 
	{
		pos = request.find('=');
		queryParams = request.substr(pos + 1, 2);
	}

	return queryParams;
}

string readFile(const string& filename) 
{
	FILE* filePtr = fopen(filename.c_str(), "r");
	if (filePtr == nullptr) 
	{
		cerr << "Error: Unable to open file '" << filename << "'" << endl;
		return ""; 
	}

	stringstream buffer;
	char tempBuffer[1024];
	size_t bytesRead;
	while ((bytesRead = fread(tempBuffer, 1, sizeof(tempBuffer), filePtr)) > 0) 
	{
		buffer.write(tempBuffer, bytesRead);
	}

	fclose(filePtr);

	return buffer.str();
}

string constructHTTPResponse(int statusCode, const string& contentType, const string& content) 
{
	ostringstream response;

	response << "HTTP/1.1 " << statusCode << " ";
	switch (statusCode) 
	{
	case 200:
		response << "OK";
		break;
	case 201:
		response << "Created";
		break;
	case 400:
		response << "Bad Request";
		break;
	case 404:
		response << "Not Found";
		break;
	case 500:
		response << "Internal Server Error";
		break;
	default:
		response << "Unknown Status";
		break;
	}
	response << "\r\n";
	response << "Content-Type: " << contentType << "\r\n";
	response << "Content-Length: " << content.size() << "\r\n";
	response << "\r\n";
	response << content;

	return response.str();
}

void putRequest(int index, int& statusCode, string& content, string& contentType) 
{
	string message = sockets[index].buffer;
	string requestBody = getRequestBody(message);
	string fileName = sockets[index].buffer;
	fileName = fileName.substr(1, fileName.find(' '));

	FILE* filePtr = fopen(fileName.c_str(), "w");
	if (filePtr != nullptr) 
	{
		fwrite(requestBody.c_str(), sizeof(char), requestBody.size(), filePtr);
		fclose(filePtr);

		statusCode = 201;
		content = "File " + fileName + " successfully updated or created.\n";
		contentType = "text/plain";
	}
	else 
	{
		statusCode = 500;
		content = "Error: Unable to open or create file.\n";
		contentType = "text/plain";
	}
}

void deleteRequest(int index, int& statusCode, string& content, string& contentType) {
	string message = sockets[index].buffer;
	string fileName = message.substr(1, message.find(' '));

	if (ifstream(fileName)) 
	{
		if (std::remove(fileName.c_str()) == 0) 
		{
			statusCode = 200; // OK
			content = "File " + fileName + " successfully deleted.";
			contentType = "text/plain";
		}
		else 
		{
			statusCode = 500; // Internal Server Error
			content = "Error: Unable to delete file " + fileName;
			contentType = "text/plain";
		}
	}
	else 
	{
		statusCode = 404; // Not Found
		content = "Error: File " + fileName + " not found.";
		contentType = "text/plain";
	}
}

void getOrHeadRequest(int index, int& statusCode, string& content, string& contentType) 
{
	string fileName = sockets[index].buffer;
	if (sockets[index].queryParameter.empty()) 
	{
		fileName = fileName.substr(0, fileName.find(' '));
		sockets[index].queryParameter = "en";
	}
	else 
	{
		fileName = fileName.substr(0, fileName.find('?'));
	}
	fileName = "files/" + sockets[index].queryParameter + fileName;
	content = readFile(fileName);
	if (!content.empty()) 
	{
		statusCode = 200;
	}
	else
	{
		statusCode = 404;
	}
	contentType = "text/html";
	content = (sockets[index].sendSubType == GET) ? content : "";
}

void optionsRequest(int index, int& statusCode, string& content, string& contentType) 
{
	content = SUPPORTED_OPTIONS;
	statusCode = 200;
	contentType = "text/plain";
}

void traceRequest(int index, int& statusCode, string& content, string& contentType)
{
	content = sockets[index].buffer;
	content = "TRACE " + content;
	statusCode = 200;
	contentType = "message/http";
}

void postRequest(int index, int& statusCode, string& content, string& contentType) 
{
	cout << getRequestBody(sockets[index].buffer) << endl;
	statusCode = 200;
	contentType = "text/plain";
	content = "Your message was successfully recived\n";
}

string getRequestBody(string request) 
{
	size_t pos = request.find("\r\n\r\n");

	if (pos != string::npos) {
		string result = request.substr(pos + 4);
		return result;
	}
	return "";
}

void sendMessage(int index)
{
	int bytesSent = 0;
	char sendBuff[1024];
	int statusCode = 0;
	string contantType;
	string content = "";

	SOCKET msgSocket = sockets[index].id;
	if (sockets[index].sendSubType == GET || sockets[index].sendSubType == HEAD)
	{
		getOrHeadRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == OPTIONS)
	{
		optionsRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == PUT)
	{
		putRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == DELET_RESOURCE)
	{
		deleteRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == TRACE)
	{
		traceRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == POST)
	{
		postRequest(index, statusCode, content, contantType);
	}

	strcpy(sendBuff, constructHTTPResponse(statusCode, contantType, content).data());

	bytesSent = send(msgSocket, sendBuff, (int)strlen(sendBuff), 0);
	if (SOCKET_ERROR == bytesSent)
	{
		cout << "Server: Error at send(): " << WSAGetLastError() << endl;
		return;
	}

	cout << "Server: Sent: " << bytesSent << "\\" << strlen(sendBuff) << " bytes of \"" << sendBuff << "\"\n";

	sockets[index].send = IDLE;
}
