#pragma once
#include <windows.h>
#include <winsock2.h>
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
	short port{};
	bool is_server{};
	//todo: make thread safe
	server server_instance;
	std::string a_username{};
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