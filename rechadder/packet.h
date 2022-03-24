#pragma once
#include <iostream>
#include <functional>
#include "structs.h"
namespace net {
	namespace string {
		static inline void ltrim(std::string& s) {
			s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
				return !std::isspace(ch);
				}));
		}

		// trim from end (in place)
		static inline void rtrim(std::string& s) {
			s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
				return !std::isspace(ch);
				}).base(), s.end());
		}

		static inline void trim(std::string& s) {
			ltrim(s);
			rtrim(s);
		}
	}
	enum class packet_ids {
		connection,
		message
	};
	struct packet_identifier {
		short id{};
		bool server{};
	};
	struct packet_chadder_connection {
		packet_identifier id{ (short)packet_ids::connection, false };
		short special_chadder_identifier{ 52 };
		short special_chadder_identifier_2{ 62 };
	};
	struct packet_message {
		packet_identifier id{ (short)packet_ids::message, false };
		char message[128]{};
	};

	struct packet_handler {
		std::function<void()> on_connection{};
		std::function<void(const std::string&)> on_message{};
		connection& user;
		bool is_server;
	};
	bool is_char_safe(const char c)
	{
		static const std::string safe_chars{
			R"(abcdefghijklmnopqrstuvwxyz01234567890!@#$%^&*()-=_+[]{};':",.<>/?\|`~ )"
		};
		return safe_chars.contains(tolower(c));
	}
	std::string handle_raw_string(const char* message)
	{
		//basic pointer safety
		if (!message) return "";
		// we need to make sure that a char* that is passed in follows a certain size and character inclusion limit

		std::string buffer{};
		for (size_t i = 0; i < 128 || message[i] != '\0'; i++)
		{
			if (is_char_safe(message[i]))
				buffer += message[i];
			else
				continue;
		}
		string::trim(buffer);
		return buffer;
	}
	bool handle_packet(const packet_handler& handler, char* packet, int size)
	{
		//reject any packet over 64 bits
		if (size > 256) return false;
		// determine what type of packet we're dealing with
		auto basic_packet = reinterpret_cast<packet_identifier*>(packet);
		if (basic_packet->id == (short)packet_ids::connection)
		{
			auto packet = reinterpret_cast<packet_chadder_connection*>(basic_packet);
			if (!(packet->special_chadder_identifier == 52 &&
				packet->special_chadder_identifier_2 == 62))
			{
				//invalid challenge...
				return false;
			}
			handler.user.handshake.completed = true;
			handler.on_connection();
		}
		else if (handler.is_server == basic_packet->server)
		{
			return false;
		}
		else if (!handler.user.handshake.completed)
		{
			return false;
		}
		else if (basic_packet->id == (short)packet_ids::message) // message packet
		{
			auto msg_packet = reinterpret_cast<packet_message*>(basic_packet);
			handler.on_message(handle_raw_string(msg_packet->message));
		}
		return false;
	}
}


