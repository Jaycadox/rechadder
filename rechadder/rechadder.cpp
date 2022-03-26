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
#undef DrawText
// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
#include <mutex>
#include "packet.h"
#include "structs.h"
#include "chat_box.h"
#include <conio.h>
#include "argparser.h"
#include "command_handler.h"
#include <random>
session g_Session{};
display_queue g_Queue{};
chat::box g_Box{};
HWND own_window_handle;

cxxopts::Options options("ReChadder", "Easy console communication.");
cxxopts::ParseResult args;

void add_to_message_queue(display_queue& queue, const std::string func)
{
	std::lock_guard guard(g_Queue.lock);
	queue.stack.emplace_back(func);
}

void message_queue_loop()
{
	while (true)
	{
		if (g_Queue.halt || g_Queue.stack.size() == 0) 
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}
		g_Queue.lock.lock();
		for (const auto& message : g_Queue.stack)
		{
			if (!message.empty())
				std::cout << message;
		}
		g_Queue.stack.clear();
		g_Queue.lock.unlock();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void entry();

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
		size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], (int)segment.size(), nullptr, 0, nullptr, nullptr);
		std::string converted = std::string(size, 0);
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], (int)segment.size(), &converted[0], (int)converted.size(), nullptr, nullptr);
		ret.append(converted);
		ret.append({ 0 });
		begin = pos + 1;
		pos = wstr.find(static_cast<wchar_t>(0), begin);
	}
	if (begin <= wstr.length())
	{
		std::wstring segment = std::wstring(&wstr[begin], wstr.length() - begin);
		size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], (int)segment.size(), nullptr, 0, nullptr, nullptr);
		std::string converted = std::string(size, 0);
		WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &segment[0], (int)segment.size(), &converted[0], (int)converted.size(), nullptr, nullptr);
		ret.append(converted);
	}
	return ret;
}

std::string next_char()
{
	std::string buf{};
	char c = (char)_getch();
	buf.push_back(c);
	return buf;
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


std::string to_ip(const connection& c)
{
	auto ip = std::format("{}.{}.{}.{}_{}",
		c.address.sin_addr.S_un.S_un_b.s_b1,
		c.address.sin_addr.S_un.S_un_b.s_b2,
		c.address.sin_addr.S_un.S_un_b.s_b3,
		c.address.sin_addr.S_un.S_un_b.s_b4, c.address.sin_port
	);
	if (g_Session.is_server && g_Session.server_instance.username_map.contains(std::to_string((uint64_t)&c)))
		return g_Session.server_instance.username_map[std::to_string((uint64_t)&c)];
	
	return ip;
}

void handle_connection_incoming(SOCKET c, const std::function<void(bool, char*, int)>& callback)
{
	char buffer[512];
	while (c != INVALID_SOCKET)
	{
		int bytesReceived = recv(c, buffer, sizeof(buffer), 0);
		if (WSAGetLastError())
		{
			std::cout << get_last_winsock_error() << '\n';
			callback(true, nullptr, 0);
			return;
		}
		if (bytesReceived <= 0) continue;
		if (!buffer) continue;
		callback(false, buffer, bytesReceived);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

void user_input(client_connected_server& client)
{
	bool f8_pressed = false;
	bool in_menu = false;
	while (true)
	{
		if (own_window_handle != GetForegroundWindow()) continue;
		if (!in_menu && GetAsyncKeyState(VK_TAB) != 0)
			f8_pressed = true;
		else if (f8_pressed)
		{
			in_menu = true;
			f8_pressed = false;
			g_Queue.halt = true;
			std::cout.flush();
			std::cout << "> Compose message: ";
			std::string buffer{};
			int allow_input{ 0 };
			while (true)
			{
				auto c = next_char();
				//if (allow_input != 1)
				//{
				//	allow_input++;
				//	continue;
				//}
				if (c == "\r") break;

				if (c == ";") continue;
				if (c == "\b")
				{
					if(!buffer.empty())
						buffer.pop_back();
				}
				else
				{
					buffer += c;
				}

				std::cout << c;
				if (c == "\b")
				{
					std::cout << " \b";
				}
			}
			for (const auto& c : buffer + "> Compose message: ")
				std::cout << "\b";
			for (const auto& c : buffer + "> Compose message: ")
				std::cout << " ";
			for (const auto& c : buffer + "> Compose message: ")
				std::cout << "\b";
			send_packet(client.connected_server, chat::create_message_packet(buffer));
			g_Queue.halt = false;
			in_menu = false;
		}
		else
		{
			next_char();
		}
		//std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

uint64_t generate_uid()
{
	std::random_device rd; // obtain a random number from hardware
	std::mt19937 gen(rd()); // seed the generator
	std::uniform_int_distribution<> distr(111111111, 999999999); // define the range

	return distr(gen);
}

void handle_connection(connection& c)
{
	bool running{ true };
	net::packet_handler handler{ {}, {}, {}, c, g_Session.is_server };
	command_handler cmd_handler{};
	cmd_handler.commands.push_back(
		command("\\\\leave",
			[&](const std::vector<std::string>& args, const std::string& sender) {
				send_packet(c, net::make_broadcast_packet("Goodbye!"));
				closesocket(c.socket);
				return false;
		}));
	cmd_handler.commands.push_back(
		command("\\\\online",
			[&](const std::vector<std::string>& args, const std::string& sender) {
				std::string connected_list{};
				for (const auto& conectee : g_Session.server_instance.connections)
				{
					connected_list += "\n - " + to_ip(conectee);
				}
				send_packet(c, 
					net::make_broadcast_packet(
						std::format("There are currently {} user/s online.\n{}", g_Session.server_instance.connections.size(), connected_list)
					)
				);
				return false;
			}));
	// todo: set up packet handler
	handler.on_raw = [&](const std::string& message) {
		FLOG("{}\n", message);
	};
	handler.on_message = [&](const std::string& username, const std::string& msg)
	{
		if (g_Session.is_server)
		{
			//test the command handler to see if we're allowed to return to this
			// default handling.
			if (!cmd_handler.to_handler(msg, username)) return;
			FLOG("{}: {}\n", to_ip(c), msg);
			// broadcast a server bound version of the packet to all peers
			// todo: username customization
			for (const auto& cnt : g_Session.server_instance.connections)
			{
				send_packet(cnt, chat::s_create_message_packet(to_ip(c), msg));
			}
		}
		else
		{
			FLOG("{}: {}\n", username, msg);
		}
	};
	handler.on_connection = [&](const std::string& brand, const std::string username) {
		if (g_Session.is_server)
		{
			send_packet(c, net::packet_chadder_connection{});
			std::cout << std::to_string((uint64_t)c.socket) << '\n';
			g_Session.server_instance.username_map[std::to_string((uint64_t)&c)] = std::string(username + "#" + std::to_string(c.address.sin_port) + "_" + std::to_string(generate_uid()));
			send_packet(c, net::make_broadcast_packet("Rechadder is still in beta! And this is the testing server."));
			FINFO("Client connected ({}): {}\n", brand, to_ip(c));
		}
		else
		{
			SYNC_FINFO("Handshake established ({})\n", "52, 62");
			FINFO("Server is running rechadder version: {}\n", brand);
		}

	};

	handle_connection_incoming(c.socket, [&](bool terminatred, char* content, int size) {
		if (terminatred)
		{
			if (g_Session.is_server)
			{
				FINFO("Client disconnected: {}\n", to_ip(c));
			}
			else
			{
				FINFO("Disconnected from server: {}\n", to_ip(c));
			}
			closesocket(c.socket);
			remove_connection(c);
			running = false;
			return;
		}
		net::handle_packet(handler, content, size);
	});

	while (running) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); }
	
	
}

void handle_connections()
{
	g_Session.server_instance.connections.reserve(1000);
	while (true)
	{
		sockaddr_in remoteAddr{};
		int iRemoteAddrLen{};
		SOCKET hRemoteSocket{};

		iRemoteAddrLen = sizeof(remoteAddr);
		hRemoteSocket = accept(g_Session.server_instance.self_connection.socket, (sockaddr*)&remoteAddr, &iRemoteAddrLen);
		if (hRemoteSocket == INVALID_SOCKET) continue;
		
		// on initial connection, add to connected users vector
		auto& ctn = g_Session.server_instance.connections.emplace_back(connection(remoteAddr, hRemoteSocket));
		SYNC_FINFO("Client connecting.... {}\n", to_ip(ctn));
		//g_Session.server_instance.connections.push_back(ctn);
		std::thread([&]()
			{
				handle_connection(ctn);
			}
		).detach();
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	
	
}

SOCKET create_socket()
{
	auto hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (hSocket == INVALID_SOCKET)
	{
		SYNC_FINFO("Socket failure.\n");
		entry();
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
	SetConsoleTitleA(std::format("ReChadder - Server").c_str());
	g_Session.server_instance = server{};
	g_Session.server_instance.self_connection.socket = create_socket();
	g_Session.server_instance.self_connection.address = create_socket_addr(std::nullopt, g_Session.port);

	// Connect to the server
	if (bind(g_Session.server_instance.self_connection.socket,
		(sockaddr*)(&g_Session.server_instance.self_connection.address),
		sizeof(g_Session.server_instance.self_connection.address)) != 0)
	{
		SYNC_FERROR("Failed to bind port: {}. Error code: {}\n", g_Session.port, get_last_winsock_error());
		entry();
	}
	if (listen(g_Session.server_instance.self_connection.socket, SOMAXCONN) != 0)
	{
		SYNC_FERROR("Failed to listen on port: {}. Error code: {}\n", g_Session.port, get_last_winsock_error());
		entry();
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
	SetConsoleTitleA("ReChadder - Client");
	client_connected_server client{};
	client.connected_server.socket = create_socket();
	SYNC_FINFO("Connecting to {}...\n", ip);
	sockaddr_in sockAddr = create_socket_addr(ip, g_Session.port);
	if (connect(client.connected_server.socket, (sockaddr*)(&sockAddr), sizeof(sockAddr)) != 0)
	{
		SYNC_FINFO("Failed to connected to: {}. Error code: {}\n", ip, get_last_winsock_error());
		entry();
	}
	SetConsoleTitleA(std::format("ReChadder - {}", ip).c_str());
	SYNC_FINFO("Establishing handshake... {}\n", ip);
	auto con = net::packet_chadder_connection{};
	if(g_Session.a_username.length() < 16)
		memcpy(con.username, net::handle_raw_string(g_Session.a_username.c_str()).c_str(),
			net::handle_raw_string(g_Session.a_username.c_str()).length());
	send_packet(client.connected_server, con);
	std::thread([client]()
		{
			auto h = client.connected_server;
			handle_connection(h);
		}
	).detach();
	user_input(client);
}

void entry()
{
	std::thread(message_queue_loop).detach();
	if (!args.count("server") && !args.count("ip"))
	{

		std::cout << "Client[0] Server[1] > ";
		g_Session.is_server = std::stoi(next_char());
		std::cout << std::format("{}\n", g_Session.is_server ? "Server" : "Client");
	}
	else
	{
		g_Session.is_server = g_Session.a_server;
	}
	if(!g_Session.is_server)
		if (!args.count("ip"))
		{
			FINPUT("IP [blank=127.0.0.1]> ", resp_1);
			if(!resp_1.empty())
				g_Session.a_ip = resp_1;
			else
				g_Session.a_ip = "127.0.0.1";

		}
	
	own_window_handle = GetConsoleWindow();
	if (!args.count("port"))
	{
		FINPUT("Port [blank=1111]> ", resp_1);
		if (!resp_1.empty())
			g_Session.port = std::stoi(resp_1);
		else
			g_Session.port = 1111;
	}
	else
	{
		g_Session.port = std::stoi(g_Session.a_port);
	}
	if (!g_Session.is_server)
		if (!args.count("username"))
		{
			FINPUT("Username [blank=Anon]> ", resp_1);
			if(!resp_1.empty())
				g_Session.a_username = resp_1;
			else
				g_Session.a_username = "Anon";

		}

	
	if (!g_Session.is_server)
		start_client(g_Session.a_ip);
	else
		start_server();
	while (true) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); }
}

int main(int argc, char** argv)
{
	std::cout << "(re)Chadder(box) - A simple communication system - made by jayphen\nOnce connected to a server, press TAB to compose a message.\n";
	options.add_options()
		("i,ip", "Address you wish to connect to", cxxopts::value<std::string>())
		("p,port", "Port of server", cxxopts::value<std::string>())
		("s,server", "Host a ReChadder server", cxxopts::value<bool>()->default_value("false"))
		("u,username", "Username the server will display you as", cxxopts::value<std::string>())
		("h,help", "Print usage")
		;
	args = options.parse(argc, argv);
	if (args.count("help"))
	{
		std::cout << options.help() << std::endl;
		exit(0);
	}
	if (args.count("ip"))
		g_Session.a_ip = args["ip"].as<std::string>();
	if (args.count("port"))
		g_Session.a_port = args["port"].as<std::string>();
	g_Session.a_server = args["server"].as<bool>();
	if (args.count("username"))
		g_Session.a_username = args["username"].as<std::string>();
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