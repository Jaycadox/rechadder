#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <format>
#include <thread>
#include <vector>
#include <functional>
// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
#include <mutex>
#include "packet.h"
#include "structs.h"
#include "chat_box.h"
session g_Session{};
display_queue g_Queue{};
chat::box g_Box{};

void add_to_message_queue(display_queue& queue, const std::string func)
{
	std::lock_guard guard(g_Queue.lock);
	queue.stack.emplace_back(func);
}

void message_queue_loop()
{
	while (true)
	{
		std::lock_guard guard(g_Queue.lock);
		for (const auto& message : g_Queue.stack)
		{
			if (!message.empty())
				std::cout << message;
		}
		g_Queue.stack.clear();
	}
}

#define FLOG(x, ...) add_to_message_queue(g_Queue, std::format(x, __VA_ARGS__));
#define FINFO(x, ...) add_to_message_queue(g_Queue, std::format(std::string("[info] ") + std::string(x), __VA_ARGS__));
#define SYNC_FINFO(x, ...) std::cout << "[info] " << std::format(x, __VA_ARGS__);
#define FERROR(x, ...) add_to_message_queue(g_Queue, std::format(std::string("[error] ") + std::string(x), __VA_ARGS__));
#define SYNC_FERROR(x, ...) std::cout << "[error] " << std::format(x, __VA_ARGS__);
#define LOG(x) add_to_message_queue(g_Queue, std::cout << std::format(x));
#define INFO(x) add_to_message_queue(g_Queue, std::cout << "[info] " << std::format(x));
#define SYNC_INFO(x) std::cout << "[info] " << std::format(x);
#define CERROR(x) add_to_message_queue(g_Queue, std::cout << "[error] " << std::format(x));
#define SYNC_ERROR(x) std::cout << "[error] " << std::format(x);
#define FINPUT(x, name1) std::string name1; {\
std::cout << "[input] " << std::format(x); std::getline(std::cin, name1);\
}\

static std::string WideStringToString(const std::wstring& wstr)
{
	if (wstr.empty())
	{
		return "";
	}
	size_t pos;
	size_t begin = 0;
	std::string ret;
	int size;
	pos = wstr.find(static_cast<wchar_t>(0), begin);
	while (pos != std::wstring::npos && begin < wstr.length())
	{
		std::wstring segment = std::wstring(&wstr[begin], pos - begin);
		size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], segment.size(), nullptr, 0, nullptr, nullptr);
		std::string converted = std::string(size, 0);
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], segment.size(), &converted[0], converted.size(), nullptr, nullptr);
		ret.append(converted);
		ret.append({ 0 });
		begin = pos + 1;
		pos = wstr.find(static_cast<wchar_t>(0), begin);
	}
	if (begin <= wstr.length())
	{
		std::wstring segment = std::wstring(&wstr[begin], wstr.length() - begin);
		size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], segment.size(), nullptr, 0, nullptr, nullptr);
		std::string converted = std::string(size, 0);
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], segment.size(), &converted[0], converted.size(), nullptr, nullptr);
		ret.append(converted);
	}
	return ret;
}

std::string get_last_winsock_error()
{
	auto last_error = WSAGetLastError();
	wchar_t* s = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, last_error,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPWSTR)&s, 0, nullptr);
	std::string error = std::format("({1}) {0}", WideStringToString(std::wstring(s)), last_error);
	LocalFree(s);
	return error;
}


template<class T>
void send_packet(const connection& c, T packet)
{
	send(c.socket, reinterpret_cast<char*>(&packet),
		sizeof(T), 0);
}


std::string to_ip(const sockaddr_in& addr)
{
	return std::format("{}.{}.{}.{}",
		addr.sin_addr.S_un.S_un_b.s_b1,
		addr.sin_addr.S_un.S_un_b.s_b2,
		addr.sin_addr.S_un.S_un_b.s_b3,
		addr.sin_addr.S_un.S_un_b.s_b4
	);
}

void handle_connection_incoming(connection& c, const std::function<void(bool, char*, int)>& callback)
{
	char buffer[512];
	while (c.socket != INVALID_SOCKET)
	{
		int bytesReceived = recv(c.socket, buffer, sizeof(buffer), 0);
		if (WSAGetLastError())
		{
			callback(true, nullptr, 0);
			return;
		}
		if (bytesReceived <= 0) continue;
		if (!buffer) continue;
		callback(false, buffer, bytesReceived);
	}
}

bool remove_connection(const connection& c)
{
	std::vector<connection> n{};
	for (const auto& connection : g_Session.server_instance.connections)
	{
		if (connection.socket != c.socket)
		{
			n.emplace_back(connection);
		}
	}
	g_Session.server_instance.connections = n;
	return false;
}

void handle_connection(connection& c)
{
	net::packet_handler handler{ {}, {}, c, g_Session.is_server };
	// todo: set up packet handler
	handler.on_message = [&](const std::string& username, const std::string& msg)
	{
		if (g_Session.is_server)
		{
			FLOG("{}: {}\n", to_ip(c.address), msg);
			// broadcast a server bound version of the packet to all peers
			// todo: username customization
			for (const auto& cnt : g_Session.server_instance.connections)
			{
				send_packet(cnt, chat::s_create_message_packet("Other client", msg));
			}
		}
		else
		{
			FLOG("{}: {}\n", username, msg);
		}
	};
	handler.on_connection = [&](const std::string& brand) {
		if (g_Session.is_server)
		{
			send_packet(c, net::packet_chadder_connection{});
			FINFO("Client connected ({}): {}\n", brand, to_ip(c.address));
		}
		else
		{
			FINFO("Server is running rechadder version: {}\n", brand);
		}

	};

	handle_connection_incoming(c, [&](bool terminatred, char* content, int size) {
		if (terminatred)
		{
			if (g_Session.is_server)
			{
				if (remove_connection(c))
				{
					FINFO("Client disconnected: {}\n", to_ip(c.address));
					
				}
				else
				{
					FINFO("Connection closed with: {}\n", to_ip(c.address));
				}
			}
			else
			{
				FINFO("Disconnected from server: {}\n", to_ip(c.address));
			}
			closesocket(c.socket);
		}
		net::handle_packet(handler, content, size);
	});

	while (true) {}
	
	
}

void handle_connections()
{

	while (true)
	{
		sockaddr_in remoteAddr{};
		int iRemoteAddrLen{};
		SOCKET hRemoteSocket{};

		iRemoteAddrLen = sizeof(remoteAddr);
		hRemoteSocket = accept(g_Session.server_instance.self_connection.socket, (sockaddr*)&remoteAddr, &iRemoteAddrLen);
		if (hRemoteSocket == INVALID_SOCKET) continue;
		SYNC_FINFO("Client connecting... {}\n", to_ip(remoteAddr));
		// on initial connection, add to connected users vector
		auto ctn = connection(remoteAddr, hRemoteSocket);
		g_Session.server_instance.connections.push_back(ctn);



		//while (true)
		//{
		//	auto t = chat::s_create_message_packet("Other peer", "test");
		//	send(hRemoteSocket, reinterpret_cast<char*>(&t),
		//		sizeof(net::packet_s_message), 0);
		//	//std::cout << get_last_winsock_error() << '\n';
		//}
		std::thread([=]()
			{
				auto c = ctn;
				handle_connection(c);
			}
		).detach();
	}
	
	
}

SOCKET create_socket()
{
	auto hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (hSocket == INVALID_SOCKET)
	{
		SYNC_FINFO("Socket failure.\n");
		WSACleanup();
		exit(1);
	}
	return hSocket;
}

sockaddr_in create_socket_addr(std::optional<std::string> ip, short port)
{
	sockaddr_in sa{};
	sa.sin_port = htons(port);
	sa.sin_family = AF_INET;
	if (ip.has_value())
	{
		sa.sin_addr.S_un.S_addr = inet_addr(ip.value().c_str());
	}
	else
	{
		sa.sin_addr.S_un.S_addr = INADDR_ANY;
	}
	return sa;
}

void start_server()
{
	g_Session.server_instance = server{};
	g_Session.server_instance.self_connection.socket = create_socket();
	g_Session.server_instance.self_connection.address = create_socket_addr(std::nullopt, g_Session.port);

	// Connect to the server
	if (bind(g_Session.server_instance.self_connection.socket,
		(sockaddr*)(&g_Session.server_instance.self_connection.address),
		sizeof(g_Session.server_instance.self_connection.address)) != 0)
	{
		SYNC_FERROR("Failed to bind port: {}. Error code: {}\n", g_Session.port, get_last_winsock_error());
		WSACleanup();
		exit(1);
	}
	if (listen(g_Session.server_instance.self_connection.socket, SOMAXCONN) != 0)
	{
		SYNC_FERROR("Failed to listen on port: {}. Error code: {}\n", g_Session.port, get_last_winsock_error());
		WSACleanup();
		exit(1);
	}
	SYNC_FINFO("Server started on port: {}\n", g_Session.port);

	std::thread([]()
		{
			handle_connections();
		}
	).detach();
	
}
void start_client(const std::string& ip)
{
	client_connected_server client{};
	client.connected_server.socket = create_socket();
	SYNC_FINFO("Connection: {}\n", ip);
	sockaddr_in sockAddr = create_socket_addr(ip, g_Session.port);
	if (connect(client.connected_server.socket, (sockaddr*)(&sockAddr), sizeof(sockAddr)) != 0)
	{
		SYNC_FINFO("Failed to connected to: {}. Error code: {}\n", ip, get_last_winsock_error());
		WSACleanup();
		exit(1);
	}

	SYNC_FINFO("Connected: {}\n", ip);
	send_packet(client.connected_server, net::packet_chadder_connection{});
	std::thread([client]()
		{
			auto h = client.connected_server;
			handle_connection(h);
		}
	).detach();


	std::thread([client]()
		{
			while (true)
			{
				send_packet(client.connected_server, chat::create_message_packet("Hello, world 1"));
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}
	).detach();
}

void entry()
{
	std::thread(message_queue_loop).detach();
	FINPUT("[0] Client [1] Server > ", resp);
	g_Session.is_server = std::stoi(resp);
	FINPUT("Port > ", resp_1);

	g_Session.port = std::stoi(resp_1);
	
	if (!g_Session.is_server)
		start_client("127.0.0.1");
	else
		start_server();
	while (true) {}
}

int main(int argc, char** argv)
{
	const int iReqWinsockVer = 2;
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(iReqWinsockVer, 0), &wsaData) == 0)
	{
		if (LOBYTE(wsaData.wVersion) < iReqWinsockVer)
		{
			SYNC_FERROR("You are using an outdated version of WinSocket. Requied version '{}'. Has version '{}'\n",
				iReqWinsockVer, LOBYTE(wsaData.wVersion));
			exit(1);
		}
		entry();
			
		if (WSACleanup() != 0)
		{
			SYNC_FERROR("Failed to clean up. WinSock error code: {}\n",
				get_last_winsock_error());
		}
	}
	else
		SYNC_FERROR("Unable to initiate Chadder. This is most likely due to an internet error. WinSock error code: ",
			get_last_winsock_error());
	return 0;
}