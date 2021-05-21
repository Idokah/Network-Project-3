#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
using namespace std;
#pragma comment(lib, "Ws2_32.lib")
#include <winsock2.h>
#include <time.h>
#include <string.h>
#include <sstream>
#include <chrono>
#include <fstream>
#include <filesystem>

#define CODE_200 "HTTP/1.1 200 OK\n\r"
#define CODE_201 "HTTP/1.1 201 Created\n\r"
#define CODE_404 "HTTP/1.1 404 Not Found\n\r"
#define CODE_204 "HTTP/1.1 204 No Content\n\r"
#define CODE_500 "HTTP/1.1 500 Internal Server Error\n\r"
#define EOF "\n\n\r\n\r\n"
#define TIMEOUT 120

enum ClientRequest
{
	GET,
	HEAD,
	TRACE,
	OPTIONS,
	_DELETE,
	PUT,
	POST
};

struct SocketState
{
	SOCKET id;
	int	recv;
	int	send;
	ClientRequest sendSubType;
	char buffer[4096];
	int len;
	time_t time;
};

const int TIME_PORT = 8080;
const int MAX_SOCKETS = 60;
const int EMPTY = 0;
const int LISTEN = 1;
const int RECEIVE = 2;
const int IDLE = 3;
const int SEND = 4;
const int SEND_TIME = 1;
const int SEND_SECONDS = 2;

bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);
void addHeader(string& sendBuff, int length = 0, bool trace = false,bool options=false,string path="");
int getContentFromFile(ifstream& file, string& sendBuff);
void returnFileNameAfterQuery(string& name);
void findStartOfBody(char* buff, int& startBody);
int findEndMessage(char* buff);
int extendEOF(char* buffer, int len, int bytesRecv);

struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;


void main()
{
	WSAData wsaData;
	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "Time Server: Error at WSAStartup()\n";
		return;
	}
	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (INVALID_SOCKET == listenSocket)
	{
		cout << "Time Server: Error at socket(): " << WSAGetLastError() << endl;
		WSACleanup();
		return;
	}
	sockaddr_in serverService;
	serverService.sin_family = AF_INET;
	serverService.sin_addr.s_addr = INADDR_ANY;
	serverService.sin_port = htons(TIME_PORT);
	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)&serverService, sizeof(serverService)))
	{
		cout << "Time Server: Error at bind(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}
	if (SOCKET_ERROR == listen(listenSocket, 5))
	{
		cout << "Time Server: Error at listen(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}
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
			cout << "Time Server: Error at select(): " << WSAGetLastError() << endl;
			WSACleanup();
			return;
		}

		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			time_t currentTime;
			if (sockets[i].send == IDLE && sockets[i].recv != LISTEN)
			{
				time(&currentTime);
				int fromLast = currentTime - sockets[i].time;
				if (fromLast > TIMEOUT)
				{
					closesocket(sockets[i].id);
					removeSocket(i);
					cout << "socket " << sockets[i].id << "disconnected - timeout" << endl << endl ;
				}
			}
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
					sendMessage(i);
					break;
				}
			}
		}
	}

	// Closing connections and Winsock.
	cout << "Time Server: Closing Connection.\n";
	closesocket(listenSocket);
	WSACleanup();
}

bool addSocket(SOCKET id, int what)
{
	unsigned long flag = 1;
	if (ioctlsocket(id, FIONBIO, &flag) != 0)
	{
		cout << "Time Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
	}

	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		if (sockets[i].recv == EMPTY)
		{
			sockets[i].id = id;
			sockets[i].recv = what;
			sockets[i].send = IDLE;
			time(&sockets[i].time);
			sockets[i].len = 0;
			socketsCount++;
			cout << "socket created : " << id << endl << endl << endl;
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
		cout << "Time Server: Error at accept(): " << WSAGetLastError() << endl;
		return;
	}
	cout << "Time Server: Client " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << " is connected." << endl;

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
		cout << "Time Server: Error at recv(): " << WSAGetLastError() << endl;
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
		sockets[index].buffer[len + bytesRecv] = '\0'; //add the null-terminating to make it a string
		cout << "Time Server: Recieved: " << bytesRecv << " bytes of \"" << &sockets[index].buffer[len] << "\" message.\n";
		int requestLen = 0;
		sockets[index].len += bytesRecv;

		if (sockets[index].len > 0)
		{
			if (strncmp(sockets[index].buffer, "GET", 3) == 0)
			{
				requestLen = strlen("GET /");
				sockets[index].sendSubType = GET;
			}
			else if (strncmp(sockets[index].buffer, "HEAD", 4) == 0)
			{
				requestLen = strlen("HEAD /");
				sockets[index].sendSubType = HEAD;
			}
			else if (strncmp(sockets[index].buffer, "TRACE", 5) == 0)
			{
				requestLen = strlen("TRACE /");
				sockets[index].sendSubType = TRACE;
			}
			else if (strncmp(sockets[index].buffer, "OPTIONS", 7) == 0)
			{
				requestLen = strlen("OPTIONS /");
				sockets[index].sendSubType = OPTIONS;
			}
			else if (strncmp(sockets[index].buffer, "DELETE", 6) == 0)
			{
				requestLen = strlen("DELETE /");
				sockets[index].sendSubType = _DELETE;
			}
			else if (strncmp(sockets[index].buffer, "PUT", 3) == 0)
			{
				requestLen = strlen("PUT /");
				sockets[index].sendSubType = PUT;
			}
			else if (strncmp(sockets[index].buffer, "POST", 4) == 0)
			{
				requestLen = strlen("POST /");
				sockets[index].sendSubType = POST;
			}
			sockets[index].len += extendEOF(sockets[index].buffer, len, bytesRecv);
			time(&sockets[index].time);
			sockets[index].send = SEND;
			memcpy(sockets[index].buffer, &sockets[index].buffer[requestLen], sockets[index].len);
			sockets[index].len -= requestLen;
			return;
		}
	}
}
int extendEOF(char* buffer, int len, int bytesRecv)
{
	buffer[len + bytesRecv] = '\n';
	buffer[len + bytesRecv + 1] = '\n';
	buffer[len + bytesRecv + 2] = '\r';
	buffer[len + bytesRecv + 3] = '\n';
	buffer[len + bytesRecv + 4] = '\r';
	buffer[len + bytesRecv + 5] = '\n';
	buffer[len + bytesRecv + 6] = '\0';
	return 6;
}

void sendMessage(int index)
{
	int bytesSent = 0;
	string sendBuff = "";
	SOCKET msgSocket = sockets[index].id;
	string requestContentFile = sockets[index].buffer;
	requestContentFile = requestContentFile.substr(0, requestContentFile.find(" "));
	returnFileNameAfterQuery(requestContentFile);
	requestContentFile.insert(0, "C:\\temp\\");
	int length;
	int endMessage = 0;
	endMessage = findEndMessage(sockets[index].buffer);
	if (sockets[index].sendSubType == GET)
	{
		ifstream infile(requestContentFile);
		sendBuff = "";
		if (!infile) //file doesn't exists - 404
		{
			addHeader(sendBuff);
			sendBuff.insert(0, CODE_404);
		}
		else
		{
			length = getContentFromFile(infile, sendBuff);
			addHeader(sendBuff, length);
			sendBuff.insert(0, CODE_200);
		}
	}
	if (sockets[index].sendSubType == _DELETE)
	{
		sendBuff = "";
		ifstream infile(requestContentFile);
		if (!infile) //file doesn't exists - 404
		{
			addHeader(sendBuff);
			sendBuff.insert(0, CODE_404);
		}
		else
		{
			infile.close();
			string statusCode = "";
			if (remove(requestContentFile.c_str()) == 0) {
				sendBuff += "File deleted";
				statusCode = CODE_200;
			}
			else {
				sendBuff += "Delete failed";
				statusCode = CODE_500;
			}
			addHeader(sendBuff, sendBuff.length());
			sendBuff.insert(0, statusCode);
		}
	}
	else if (sockets[index].sendSubType == HEAD)
	{
		ifstream infile(requestContentFile);
		sendBuff = "";
		int length;
		if (!infile) //file doesn't exists - 404
		{
			addHeader(sendBuff);
			sendBuff.insert(0, CODE_404);
		}
		else  //if file exists 
		{
			length = getContentFromFile(infile, sendBuff);
			sendBuff = ""; //getContentFromFile loads the infile into sendBuff, with HEAD request we don't want to transfer this data.
			addHeader(sendBuff, length);
			sendBuff.insert(0, CODE_200);
		}
	}
	else if (sockets[index].sendSubType == TRACE)
	{
		sendBuff = sockets[index].buffer;
		sendBuff.insert(0, "TRACE /");
		addHeader(sendBuff, sendBuff.length(),true);
		sendBuff.insert(0, CODE_200);
	}
	else if (sockets[index].sendSubType == OPTIONS)
	{
		sendBuff = "";
		addHeader(sendBuff,0,false,true);
		sendBuff.insert(0, CODE_204);
	}
	else if (sockets[index].sendSubType == PUT)
	{
		string content = sockets[index].buffer;
		int startBody = 0;
		findStartOfBody(sockets[index].buffer, startBody);
		content = content.substr(startBody, endMessage - startBody - strlen(EOF));
		addHeader(sendBuff, 0,false,false, requestContentFile);
		ifstream infile(requestContentFile);
		string statusCode = "";
		if (!infile)
			statusCode = CODE_201;
		else
		{
			statusCode = CODE_200;
			infile.close();
		}
		ofstream myfile(requestContentFile, ofstream::out);
		myfile << content;
		myfile.close();
		sendBuff.insert(0, statusCode);
	}
	else if (sockets[index].sendSubType == POST)
	{
		string content = sockets[index].buffer;
		int startBody = 0;
		findStartOfBody(sockets[index].buffer,startBody);
		content = content.substr(startBody,endMessage-startBody-strlen(EOF));
		string adderPath = "/" + requestContentFile;
		addHeader(sendBuff, 0,false,false,adderPath);
		ifstream infile(requestContentFile);
		string statusCode = "";
		if (!infile)
			statusCode = CODE_201;
		else
		{
			statusCode = CODE_200;
			infile.close();
		}
		cout <<endl<<"~~~~~POST CONTENT: "<< content <<"   ~~~~~"<< endl;
		ofstream myfile(requestContentFile, ofstream::out | ofstream::app);
		myfile << content;
		myfile.close();
		sendBuff.insert(0, statusCode);
	}

	bytesSent = send(msgSocket, sendBuff.c_str(), sendBuff.length(), 0);
	if (SOCKET_ERROR == bytesSent)
	{
		cout << "Time Server: Error at send(): " << WSAGetLastError() << endl;
		return;
	}

	cout << "Time Server: Sent: " << bytesSent << "\\" << sendBuff.length() << " bytes of \"" << sendBuff << "\" message.\n";
	


	sockets[index].len -= endMessage;
	if (sockets[index].len == 0) {
		sockets[index].buffer[0] = '\0';
		sockets[index].send = IDLE;
	}
	else {
		memcpy(sockets[index].buffer, &sockets[index].buffer[endMessage], sockets[index].len);
	}
}
void findStartOfBody(char* buff,int& startBody)
{
	string buffer = buff;
	startBody = buffer.find("\r\n\r\n") + strlen("\r\n\r\n");
}
int findEndMessage(char* buff)
{
	string buffer = buff;
	return (buffer.find(EOF) + strlen(EOF));
}
void addHeader(string& sendBuff, int length, bool trace,bool options,string path)
{
	string contentType = "text/html";
	string allow = "";
	string contentLocation = "";
	if (trace)
		contentType = "message/http";
	if (options)
		allow = "\r\nAllow: OPTIONS, GET, HEAD, POST, PUT, DELETE, TRACE";
	if (path != "") 
		path.insert(0, "\r\nContent - Location: ");
	time_t timer;
	time(&timer);
	string date = ctime(&timer);
	//TODO 
	//change order
	/*: 1.Content-length
	*	2.content-type
	*	3.date
	*	4.server
	*/
	string header =
		"Date: " + date
		+ "Server: Yuval/Ido server \r\n"
		+ "Content-Length: " + to_string(length) + "\r\n"
		+ "Content-Type: " + contentType  + allow + path + "\r\n\n";
	header += sendBuff;
	sendBuff = header;

}


int getContentFromFile(ifstream& infile, string& sendBuff)
{
	string line;
	while (!infile.eof())
	{
		getline(infile, line);
		sendBuff += line;
	}
	return sendBuff.length();
}

string addResponse(string userSendBuff, string response)
{
	stringstream headers;
	headers << response << endl << userSendBuff;

	return headers.str();
}

void returnFileNameAfterQuery(string& name)
{
	string language;
	if (name.find("?lang=") != string::npos)
	{
		language = name.substr(name.find("=") + 1, name.find(" "));
		name = name.substr(0, name.find("?"));
		name += " " + language;
	}
}