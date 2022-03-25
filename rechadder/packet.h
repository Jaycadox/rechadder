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
		message,
		s_message,
		s_broadcast
	};
	static const bool server_bound[]{
		false,
		false,
		true,
		true
	};
	struct packet_identifier {
		short id{};
		bool server{};
	};
	struct packet_chadder_connection {
		packet_identifier id{ (short)packet_ids::connection, false };
		short special_chadder_identifier{ 52 };
		short special_chadder_identifier_2{ 62 };
		char username[16] = "";
		char brand[16] = "alpha0.1";

	};
	struct packet_message {
		packet_identifier id{ (short)packet_ids::message, false };
		char message[128]{};
	};
	struct packet_s_message {
		packet_identifier id{ (short)packet_ids::s_message, true };
		char message[128]{};
		char username[32]{};
	};
	struct packet_s_broadcast {
		packet_identifier id{ (short)packet_ids::s_broadcast, true };
		char message[256]{};
	};
	struct packet_handler {
		std::function<void(const std::string& brand, const std::string& username)> on_connection{};
		std::function<void(const std::string&, const std::string&)> on_message{};
		std::function<void(const std::string&)> on_raw{};
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
		for (size_t i = 0; i < 128 && message[i] != '\0'; i++)
		{
			if (is_char_safe(message[i]))
				buffer += message[i];
			else
				continue;
		}
		string::trim(buffer);
		return buffer;
	}
	net::packet_s_broadcast make_broadcast_packet(const std::string& text)
	{
		net::packet_s_broadcast pack{};
		memcpy(pack.message, text.c_str(), sizeof(pack.message));
		return pack;
	}
	bool handle_packet(const packet_handler& handler, char* packet, int size)
	{
		if (!packet) return false;
		//reject any packet over 64 bits
		if (size > 512) return false;
		// determine what type of packet we're dealing with
		auto basic_packet = reinterpret_cast<packet_identifier*>(packet);
		if (basic_packet->id == (short)packet_ids::connection)
		{
			if (size != sizeof(packet_chadder_connection)) return false;
			auto packet = reinterpret_cast<packet_chadder_connection*>(basic_packet);
			if (!(packet->special_chadder_identifier == 52 &&
				packet->special_chadder_identifier_2 == 62))
			{
				//invalid challenge...
				return false;
			}
			if (handle_raw_string(packet->username).length() < 3 && handler.is_server) return false;

			handler.user.handshake.completed = true;
			handler.on_connection(handle_raw_string(packet->brand), handle_raw_string(packet->username));
		}
		else if (basic_packet->id >= sizeof(server_bound) || basic_packet->id < 0)
		{
			return false;
		}
		else if (server_bound[basic_packet->id] == handler.is_server)
		{
			return false;
		}
		else if (!handler.user.handshake.completed)
		{
			return false;
		}
		else if (basic_packet->id == (short)packet_ids::message) // client -> server message packet
		{
			if (size != sizeof(packet_message)) return false;
			auto msg_packet = reinterpret_cast<packet_message*>(basic_packet);
			auto content = handle_raw_string(msg_packet->message);
			if(content.length() > 0)
				handler.on_message("", content);
		}
		else if (basic_packet->id == (short)packet_ids::s_message) // server -> client message packet
		{
			if (size != sizeof(packet_s_message)) return false;
			auto msg_packet = reinterpret_cast<packet_s_message*>(basic_packet);
			handler.on_message(handle_raw_string(msg_packet->username), handle_raw_string(msg_packet->message));
		}
		else if (basic_packet->id == (short)packet_ids::s_broadcast) // server -> client message packet
		{
			if (size != sizeof(packet_s_broadcast)) return false;
			auto msg_packet = reinterpret_cast<packet_s_broadcast*>(basic_packet);
			handler.on_raw(handle_raw_string(msg_packet->message));
		}
		return false;
	}
}


