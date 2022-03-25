#pragma once
#include "structs.h"
#include "packet.h"
namespace chat {
	struct user {
		connection& connection;
	};

	struct message {
		user& sender;
		std::string content{};
	};

	struct box {
		std::vector<user> users{};
		std::vector<message> messages{};
	};
	void offset_cursor(int x, int y)
	{
		CONSOLE_SCREEN_BUFFER_INFOEX cbsi{ 0 };
		GetConsoleScreenBufferInfoEx(GetConsoleWindow(), &cbsi);
		auto old = cbsi;
		cbsi.dwCursorPosition.X += x;
		cbsi.dwCursorPosition.Y += y;
		SetConsoleScreenBufferInfoEx(GetConsoleWindow(), &cbsi);
	}
	void message_recieved(box& b, const message& msg, display_queue& queue)
	{
		std::lock_guard g(queue.lock);
		CONSOLE_SCREEN_BUFFER_INFOEX cbsi{0};
		GetConsoleScreenBufferInfoEx(GetStdHandle(STD_OUTPUT_HANDLE), &cbsi);
		auto old = cbsi;
		cbsi.dwCursorPosition.X = 0;
		cbsi.dwCursorPosition.Y = 0;
		SetConsoleScreenBufferInfoEx(GetStdHandle(STD_OUTPUT_HANDLE), &cbsi);
		b.messages.emplace_back(msg);
		for (size_t i = 1; i < 7; i++)
		{
			std::cout << std::format("{}: {}", 
				b.messages.at(b.messages.size() - i).sender.connection.address.sin_addr.S_un.S_addr,
				b.messages.at(b.messages.size() - i).content);
		}
		SetConsoleScreenBufferInfoEx(GetStdHandle(STD_OUTPUT_HANDLE), &old);
	}
	net::packet_message create_message_packet(const std::string& text)
	{
		net::packet_message msg{};
		auto t_buf = text;
		net::string::trim(t_buf);
		if (t_buf.size() > 127)
		{
			memcpy(msg.message, "Message too long.", sizeof("Message too long."));
			return msg;
		}
		for (char& c : t_buf)
		{
			if (!net::is_char_safe(c))
			{
				continue;
			}
		}
		memcpy(msg.message, t_buf.c_str(), t_buf.length());
		return msg;
	}
	net::packet_s_message s_create_message_packet(const std::string& username, const std::string& text)
	{
		net::packet_s_message msg{};
		
		auto t_buf = text;
		net::string::trim(t_buf);

		auto u_buf = username;
		net::string::trim(u_buf);

		if (t_buf.size() > 127)
			memcpy(msg.message, "Message too long.", sizeof("Message too long."));
		else
			memcpy(msg.message, t_buf.c_str(), t_buf.length());
		if (u_buf.size() > 31)
			memcpy(msg.username, "Name too long", sizeof("Name too long"));
		else
			memcpy(msg.username, u_buf.c_str(), u_buf.length());
		return msg;
	}
}


