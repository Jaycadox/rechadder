#pragma once
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#endif
#ifdef __linux__
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#endif

#include <vector>
#include <mutex>
#include <string>
struct connection {
	sockaddr_in address;
	SOCKET socket;
	uint64_t id;
	struct shake {
		bool completed{};
	} handshake{};
};

struct client_connected_server {
	connection connected_server{};
};

struct server {
	connection self_connection{};
	std::vector<connection> connections{};
	std::unordered_map<std::string, std::string> username_map{};
};

struct session {
	short port{1111};
	bool is_server{};
	bool web_client{};
	//todo: make thread safe
	server server_instance;
	std::string a_username{"Anon"};
	std::string a_ip{};
	std::string a_port{};
	bool a_server{};
};

struct display_queue {
	std::mutex lock{};
	std::vector<std::string> stack{};
	std::atomic_bool halt{};
};
inline session g_Session{};