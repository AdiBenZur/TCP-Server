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


void InitializeSocket(SOCKET& listenSocket);
bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
string extractQueryParams(const string& request);
string readFile(const string& filename);
string constructHTTPResponse(int statusCode, const string& contentType, const string& content);
void sendMessage(int index);

void handleOPTIONSRequest(int index, int& statusCode, string& content,string& contentType);
void handlePUTRequest(int index, int& statusCode, string& content, string& contentType);
void handleGETOrHEADRequest(int index, int& statusCode, string& content, string& contentType);
void handleTRACERequest(int index, int& statusCode, string& content, string& contentType);
void handleDELETERequest(int index, int& statusCode, string& content, string& contentType);
void handlePOSTequest(int index, int& statusCode, string& content, string& contentType);


string getRequestBody(string request);


struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;

void main()
{
	SOCKET listenSocket;
	InitializeSocket(listenSocket);
	addSocket(listenSocket, LISTEN);

	// Accept connections and handles them one by one.
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

		//
		// Wait for interesting event.
		// Note: First argument is ignored. The fourth is for exceptions.
		// And as written above the last is a timeout, hence we are blocked if nothing happens.
		//
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

	// Closing connections and Winsock.
	cout << "Server: Closing Connection.\n";
	closesocket(listenSocket);
	WSACleanup();
}

void InitializeSocket(SOCKET& listenSocket) {
	WSAData wsaData;

	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData)) {
		std::cout << "Server: Error at WSAStartup()\n";
		return;
	}

	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (INVALID_SOCKET == listenSocket) {
		std::cout << "Server: Error at socket(): " << WSAGetLastError() << std::endl;
		WSACleanup();
		return;
	}

	sockaddr_in serverService;
	serverService.sin_family = AF_INET;
	serverService.sin_addr.s_addr = INADDR_ANY;
	serverService.sin_port = htons(TIME_PORT);

	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService))) {
		std::cout << "Server: Error at bind(): " << WSAGetLastError() << std::endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}

	if (SOCKET_ERROR == listen(listenSocket, 5)) {
		std::cout << "Server: Error at listen(): " << WSAGetLastError() << std::endl;
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

	//
	// Set the socket to be in non-blocking mode.
	//
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

void receiveMessage(int index) {
	SOCKET msgSocket = sockets[index].id;

	int len = sockets[index].len;
	int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

	if (SOCKET_ERROR == bytesRecv) {
		std::cout << "Server: Error at recv(): " << WSAGetLastError() << std::endl;
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}

	if (bytesRecv == 0) {
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	else {
		sockets[index].buffer[len + bytesRecv] = '\0'; //add the null-terminating to make it a string
		std::cout << "Server: Received: " << bytesRecv << " bytes of \"" << &sockets[index].buffer[len] << "\" message.\n";
		sockets[index].send = SEND;
		sockets[index].recv = EMPTY;
		time(&sockets[index].timeStamp);
		// Determine the HTTP method
		std::string request = sockets[index].buffer;
		std::string method = request.substr(0, request.find(' '));

		// Handle the HTTP method
		if (method == "OPTIONS") {
			sockets[index].sendSubType = OPTIONS;
		}
		else if (method == "GET" || method == "HEAD") {
			sockets[index].sendSubType = (method == "GET") ? GET : HEAD;
			sockets[index].queryParameter = extractQueryParams(request);
		}
		else if (method == "POST") {
			sockets[index].sendSubType = POST;
		}
		else if (method == "PUT") {
			sockets[index].sendSubType = PUT;
		}
		else if (method == "DELETE") {
			sockets[index].sendSubType = DELET_RESOURCE;
		}
		else if (method == "TRACE") {
			sockets[index].sendSubType = TRACE;
		}
		else
		{
			sockets[index].sendSubType = INVALID_REQUEST;
		}

		sockets[index].len += bytesRecv - (method.length() + 1);
		memmove(sockets[index].buffer, sockets[index].buffer + (method.length() + 1), sockets[index].len);
		sockets[index].buffer[sockets[index].len] = '\0';
	}
}

std::string extractQueryParams(const std::string& request) {
	// Find the position of the first '?' character
	std::string queryParams = "";


	size_t pos = request.find('?');
	if (pos != std::string::npos && pos < 20) {
		// Extract query parameters if they exist
		pos = request.find('=');
		queryParams = request.substr(pos + 1, 2);
	}

	// If no query parameters found, return default value 'en'
	return queryParams;
}

std::string readFile(const std::string& filename) {
	// Open the file using fopen
	FILE* fp = fopen(filename.c_str(), "r");
	if (fp == nullptr) {
		std::cerr << "Error: Unable to open file '" << filename << "'" << std::endl;
		return ""; // Return empty string if file cannot be opened
	}

	// Read the content of the file
	std::stringstream buffer;
	char tempBuffer[1024]; // Temporary buffer for reading
	size_t bytesRead;
	while ((bytesRead = fread(tempBuffer, 1, sizeof(tempBuffer), fp)) > 0) {
		buffer.write(tempBuffer, bytesRead);
	}

	// Close the file
	fclose(fp);

	return buffer.str();
}

std::string constructHTTPResponse(int statusCode, const std::string& contentType, const std::string& content) {
	std::ostringstream response;

	// Status line
	response << "HTTP/1.1 " << statusCode << " ";
	switch (statusCode) {
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

	// Content-Type header
	response << "Content-Type: " << contentType << "\r\n";

	// Content-Length header
	response << "Content-Length: " << content.size() << "\r\n";

	// Empty line indicating end of headers
	response << "\r\n";

	// Content
	response << content;

	return response.str();
}

void handlePUTRequest(int index, int& statusCode, std::string& content, std::string& contentType) {
	//std::string requestBody = sockets[index].buffer;
	std::string message = sockets[index].buffer;
	std::string requestBody = getRequestBody(message);
	std::string fileName = sockets[index].buffer;
	fileName = fileName.substr(1, fileName.find(' '));

	// Open or create the file using FILE*
	FILE* fp = fopen(fileName.c_str(), "w");
	if (fp != nullptr) {
		// Write request body to the file
		fwrite(requestBody.c_str(), sizeof(char), requestBody.size(), fp);

		// Close the file
		fclose(fp);

		// Set response status code and content
		statusCode = 201;
		content = "File " + fileName + " successfully updated or created.\n";
		contentType = "text/plain";
	}
	else {
		// Error opening or creating the file
		statusCode = 500; // Internal Server Error
		content = "Error: Unable to open or create file.\n";
		contentType = "text/plain";
	}
}

void handleDELETERequest(int index, int& statusCode, std::string& content, std::string& contentType) {
	std::string message = sockets[index].buffer;
	std::string fileName = message.substr(1, message.find(' '));

	// Check if the file exists
	if (std::ifstream(fileName)) {
		// Attempt to delete the file
		if (std::remove(fileName.c_str()) == 0) {
			// File deletion successful
			statusCode = 200; // OK
			content = "File " + fileName + " successfully deleted.";
			contentType = "text/plain";
		}
		else {
			// Error deleting the file
			statusCode = 500; // Internal Server Error
			content = "Error: Unable to delete file " + fileName;
			contentType = "text/plain";
		}
	}
	else {
		// File not found, return 404 status code
		statusCode = 404; // Not Found
		content = "Error: File " + fileName + " not found.";
		contentType = "text/plain";
	}
}

void handleGETOrHEADRequest(int index, int& statusCode, std::string& content, std::string& contentType) {
	std::string fileName = sockets[index].buffer;
	if (sockets[index].queryParameter.empty()) {
		fileName = fileName.substr(0, fileName.find(' '));
		sockets[index].queryParameter = "en";
	}
	else {
		fileName = fileName.substr(0, fileName.find('?'));
	}
	fileName = "files/" + sockets[index].queryParameter + fileName;
	content = readFile(fileName);
	if (!content.empty()) {
		statusCode = 200;
	}
	else {
		statusCode = 404;
		// File not found or could not be opened, handle error
	}
	contentType = "text/html";
	content = (sockets[index].sendSubType == GET) ? content : "";
}

void handleOPTIONSRequest(int index, int& statusCode, std::string& content, std::string& contentType) {
	content = SUPPORTED_OPTIONS;
	statusCode = 200;
	contentType = "text/plain";
}

void handleTRACERequest(int index, int& statusCode, std::string& content, std::string& contentType) {
	content = sockets[index].buffer;
	content = "TRACE " + content;
	statusCode = 200;
	contentType = "message/http";
}

void handlePOSTequest(int index, int& statusCode, std::string& content, std::string& contentType) {
	cout << getRequestBody(sockets[index].buffer) << endl;
	statusCode = 200;
	contentType = "text/plain";
	content = "Your message was successfully recived\n";
}

std::string getRequestBody(std::string request) {
	// Find the position of the double line break that separates headers from the body
	std::size_t pos = request.find("\r\n\r\n");

	// If found, return the substring after the double line break (excluding it)
	if (pos != std::string::npos) {
		std::string result = request.substr(pos + 4);
		return result;
	}

	// If not found, return an empty string or handle error as needed
	return "";
}

void sendMessage(int index)
{
	int bytesSent = 0;
	char sendBuff[1024];
	int statusCode = 0;
	string contantType;
	std::string content = "";

	SOCKET msgSocket = sockets[index].id;
	if (sockets[index].sendSubType == GET || sockets[index].sendSubType == HEAD)
	{
		handleGETOrHEADRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == OPTIONS)
	{
		handleOPTIONSRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == PUT)
	{
		handlePUTRequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == DELET_RESOURCE)
	{
		handleDELETERequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == TRACE)
	{
		handleTRACERequest(index, statusCode, content, contantType);
	}
	else if (sockets[index].sendSubType == POST)
	{
		handlePOSTequest(index, statusCode, content, contantType);
	}

	std::strcpy(sendBuff, constructHTTPResponse(statusCode, contantType, content).data());

	bytesSent = send(msgSocket, sendBuff, (int)strlen(sendBuff), 0);
	if (SOCKET_ERROR == bytesSent)
	{
		cout << "Server: Error at send(): " << WSAGetLastError() << endl;
		return;
	}

	cout << "Server: Sent: " << bytesSent << "\\" << strlen(sendBuff) << " bytes of \"" << sendBuff << "\"\n";

	sockets[index].send = IDLE;


}
