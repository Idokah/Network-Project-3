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

#define CODE_200 "HTTP/1.1 200 OK\n\r"
#define CODE_404 "HTTP/1.1 404 Not Found\n\r"

enum ClientRequest
{
	GET,
	HEAD
};

struct SocketState
{
	SOCKET id;
	int	recv;			
	int	send;			
	int sendSubType;	
	char buffer[128];
	int len;
};

const int TIME_PORT = 27015;
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
void addHeader(string& sendBuff, int length = 0);
int getContentFromFile(ifstream& file, string& sendBuff);
void returnFileNameAfterQuery(string& name);

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
		int requestLen;
		sockets[index].len += bytesRecv;

		if (sockets[index].len > 0)
		{
			if (strncmp(sockets[index].buffer, "GET", 3) == 0)
			{
				requestLen = strlen("GET /");
				sockets[index].send = SEND;
				sockets[index].sendSubType = GET;
				memcpy(sockets[index].buffer, &sockets[index].buffer[requestLen], sockets[index].len - requestLen);
				sockets[index].len -= requestLen;
				return;
			}
			if (strncmp(sockets[index].buffer, "HEAD", 4) == 0)
			{
				requestLen = strlen("HEAD /");
				sockets[index].send = SEND;
				sockets[index].sendSubType = HEAD;
				memcpy(sockets[index].buffer, &sockets[index].buffer[requestLen], sockets[index].len - requestLen);
				sockets[index].len -= requestLen;
				return;
			}
		}
	}

}

void sendMessage(int index)
{
	int bytesSent = 0;
	string sendBuff="";
	SOCKET msgSocket = sockets[index].id;
	string requestContentFile = sockets[index].buffer;
	requestContentFile = requestContentFile.substr(0, requestContentFile.find(" "));
	returnFileNameAfterQuery(requestContentFile);
	int length;
	if (sockets[index].sendSubType == GET)
	{
		//returnFileNameAfterQuery(requestContentFile);
		ifstream infile(requestContentFile);
		if (!infile) //file doesn't exists - 404
		{
			addHeader(sendBuff);
			sendBuff.insert(0, CODE_404);
		}
		else  //if file exists 
		{
			length=getContentFromFile(infile, sendBuff);
			addHeader(sendBuff,length);
			sendBuff.insert(0, CODE_200);
		}
	}
	else if (sockets[index].sendSubType == HEAD)
	{
		//returnFileNameAfterQuery(requestContentFile);
		ifstream infile(requestContentFile);
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
			addHeader(sendBuff,length);
			sendBuff.insert(0, CODE_200);
		}
	}

	bytesSent = send(msgSocket, sendBuff.c_str(), sendBuff.length(), 0);
	if (SOCKET_ERROR == bytesSent)
	{
		cout << "Time Server: Error at send(): " << WSAGetLastError() << endl;
		return;
	}

	cout << "Time Server: Sent: " << bytesSent << "\\" << sendBuff.length() << " bytes of \"" << sendBuff << "\" message.\n";

	sockets[index].send = IDLE;
}

void addHeader(string& sendBuff,int length)
{
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
		+ "Server: Yuval and Ido server \r\n"
		+ "Content-Length: " + to_string(length) + "\r\n"
		+ "Content-Type: text/html \r\n\n";
	header += sendBuff;
	sendBuff = header;
	
}


int getContentFromFile(ifstream& infile,string& sendBuff)
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