#pragma once
#include <windows.h>
#include <winsock2.h>
#include <vector>
#include <mutex>
#include <string>
struct connection {
	sockaddr_in address;
	SOCKET socket;
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
};

struct session {
	short port{};
	bool is_server{};
	//todo: make thread safe
	server server_instance;


};

struct display_queue {
	std::mutex lock{};
	std::vector<std::string> stack{};
};