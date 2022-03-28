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
#include <filesystem>
#include "httplib.h"
#include "json.hpp"

extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
}

#include <random>

display_queue g_Queue{};
chat::box g_Box{};
client_connected_server g_Client{};
HWND own_window_handle;

cxxopts::Options options("ReChadder", "Easy console communication.");
cxxopts::ParseResult args;

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

struct lua_argument {
	virtual void push(lua_State* L) = 0;
};
struct lua_int_argument : lua_argument {
	int value;
	virtual void push(lua_State* L) override
	{
		lua_pushinteger(L, value);
	}
	lua_int_argument(int v)
	{
		value = v;
	}
};
struct lua_str_argument : lua_argument {
	std::string value;
	void push(lua_State* L) override
	{
		lua_pushstring(L, value.c_str());
	}
	lua_str_argument(std::string v)
	{
		value = v;
	}
};

struct lua_function {
	//int ref = LUA_REFNIL;
	std::vector<int> refs{};
	bool is_set{};
	lua_State* L{};
	lua_function(lua_State* state)
	{
		L = state;
	}
	~lua_function()
	{
		refs.clear();
	}
	static void push_table(lua_State* state, const std::vector<std::tuple<std::string, std::string>> args)
	{
		lua_newtable(state);
		for (const auto& arg : args)
			push_table_string(state, std::get<0>(arg), std::get<1>(arg));
	}
	bool check(int ret)
	{
		if (ret != LUA_OK)
		{
			std::cout << "[LUA error] " << lua_tostring(L, -1) << "\n";
			return false;
		}
		return true;
	}

	static void push_table_string(lua_State* state, const std::string& key, const std::string& value) {
		lua_pushstring(state, key.c_str());
		lua_pushstring(state, value.c_str());
		lua_settable(state, -3);
	}
	bool r_call_table(const std::vector<std::tuple<std::string, std::string>> args, int returnc)
	{
		if (!is_set) return false;
		for (const auto& ref : refs)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
			lua_newtable(L);
			for (const auto& arg : args)
				push_table_string(L, std::get<0>(arg), std::get<1>(arg));
			check(lua_pcall(L, 1, returnc, 0));
			if (returnc)
			{
				if (lua_isboolean(L, -1) && lua_toboolean(L, -1))
				{
					return true;
				}
			}
			
		}
		return false;
	}
	void call_table(const std::vector<std::tuple<std::string, std::string>> args)
	{
		r_call_table(args, 0);
	}
	void call()
	{
		if (!is_set) return;
		for (const auto& ref : refs)
		{
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
			check(lua_pcall(L, 0, 0, 0));
		}

	}
	void set(int iref)
	{
		is_set = true; 
		
		refs.push_back(iref);
	}
};
LPVOID m_fiber;
struct fiber {
	using func_t = void(*)();
	std::shared_ptr<lua_function> data;
	func_t func{};
	LPVOID fiber_id;
	std::optional<std::chrono::high_resolution_clock::time_point> m_wake_time;
	void yield(std::optional<std::chrono::high_resolution_clock::duration> time = std::nullopt)
	{
		if (time.has_value())
			m_wake_time = std::chrono::high_resolution_clock::now() + time.value();
		else
			m_wake_time = std::nullopt;
		SwitchToFiber(m_fiber);
	}
	static fiber* get_current()
	{
		return static_cast<fiber*>(GetFiberData());
	}
	fiber(std::shared_ptr<lua_function> d, func_t f)
	{
		func = f;
		data = d;
		fiber_id = CreateFiber(0, [](void* param)
			{
				auto this_fiber = static_cast<fiber*>(param);
				fiber::get_current()->func();
				while (true)
				{
					fiber::get_current()->yield(std::chrono::seconds(1));
				}
			}, this);
	}
};
std::vector<std::unique_ptr<fiber>> fibers{};
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

struct lua_script {
	lua_State* state = luaL_newstate();
	std::string name{ "Unknown Script" };
	void init();
	bool check(int ret);
};

struct script_hooks {
	std::vector<lua_script> scripts{};
	std::vector<lua_function> on_client{};
	std::vector<lua_function> on_server{};
	std::vector<lua_function> on_start{};
	std::vector<lua_function> on_message{};
	std::vector<lua_function> on_connection{};
	std::vector<lua_function> on_compose{};
	static inline script_hooks* context = nullptr;
	static int LUA_on_client(lua_State* L);
	static int LUA_on_start(lua_State* L);
	static int LUA_on_server(lua_State* L);
	static int LUA_on_connection(lua_State* L);
	static int LUA_on_compose(lua_State* L);
	static int LUA_connections(lua_State* L);
	static int LUA_on_message(lua_State* L);
	static int LUA_create_os_thread(lua_State* L);
	static int LUA_send_message(lua_State* L);
	static int LUA_broadcast(lua_State* L);
	static int LUA_send(lua_State* L);
	static int LUA_thread_sleep(lua_State* L);
	void load(bool re = false);
	script_hooks();
	void init();
	~script_hooks();

} g_ScriptHook;



void lua_script::init()
{
	luaL_openlibs(state);
	lua_register(state, "__events_on_client", script_hooks::LUA_on_client);
	lua_register(state, "__events_on_server", script_hooks::LUA_on_server);
	lua_register(state, "__events_on_start", script_hooks::LUA_on_start);
	lua_register(state, "__events_on_message", script_hooks::LUA_on_message);

	lua_register(state, "__client_send_message", script_hooks::LUA_send_message);
	lua_register(state, "__client_on_compose", script_hooks::LUA_on_compose);

	lua_register(state, "__server_broadcast", script_hooks::LUA_broadcast);
	lua_register(state, "__server_send", script_hooks::LUA_send);
	lua_register(state, "__server_on_connection", script_hooks::LUA_on_connection);
	lua_register(state, "__server_connections", script_hooks::LUA_connections);

	lua_register(state, "__util_create_thread", script_hooks::LUA_create_os_thread);
	lua_register(state, "__util_thread_sleep", script_hooks::LUA_thread_sleep);
	luaL_dostring(state,
		" \
		events = {}\
		events.on_client = __events_on_client\
		__events_on_client = nil\
		events.on_server = __events_on_server\
		__events_on_client = nil\
		events.on_start = __events_on_start\
		__events_on_start = nil\
		events.on_message = __events_on_message\
		__events_on_message = nil\
		client = {}\
		client.send_message = __client_send_message\
		__client_send_message = nil\
		client.on_compose = __client_on_compose\
		__client_on_compose = nil\
		server = {}\
		server.broadcast = __server_broadcast\
		__server_broadcast = nil\
		server.on_connection = __server_on_connection\
		__server_on_connection = nil\
		server.connections = __server_connections\
		__server_connections = nil\
		server.send = __server_send\
		__server_send = nil\
		util = {}\
		util.create_thread = __util_create_thread\
		__util_create_thread = nil\
		util.yield = __util_thread_sleep\
		__util_thread_sleep = nil\
		"
	);

}

bool lua_script::check(int ret)
{
	if (ret != LUA_OK)
	{
		std::cout << "[LUA error] " << lua_tostring(state, -1) << "\n";
		return false;
	}
	return true;
}

//struct shared_state {
//	lua_State* L{nullptr};
//};
// Lua Scripting

int script_hooks::LUA_on_connection(lua_State* L)
{
	if (!context) return 0;
	context->on_connection.emplace_back(lua_function(L)).set(luaL_ref(L, LUA_REGISTRYINDEX));
	return 0;
}

int script_hooks::LUA_on_client(lua_State* L)
{
	if (!context) return 0;
	context->on_client.emplace_back(lua_function(L)).set(luaL_ref(L, LUA_REGISTRYINDEX));
	return 0;
}

int script_hooks::LUA_on_compose(lua_State* L)
{
	if (!context) return 0;
	if (g_Session.is_server)
	{
		return luaL_error(L, "attempt to call client.on_compose on server. servers cannot compose messages.");
	}
	context->on_compose.emplace_back(lua_function(L)).set(luaL_ref(L, LUA_REGISTRYINDEX));
	return 0;
}

int script_hooks::LUA_on_start(lua_State* L)
{
	if (lua_gettop(L) != 1 || !lua_isfunction(L, 1))
	{
		return luaL_error(L, "expecting arguments: function(on_client_start)");
	}
	if (!context) return 0;
	context->on_start.emplace_back(lua_function(L)).set(luaL_ref(L, LUA_REGISTRYINDEX));
	return 0;
}

int script_hooks::LUA_on_server(lua_State* L)
{
	if (lua_gettop(L) != 1 || !lua_isfunction(L, 1))
	{
		return luaL_error(L, "expecting arguments: function(on_server_start)");
	}
	if (!context) return 0;
	context->on_server.emplace_back(lua_function(L)).set(luaL_ref(L, LUA_REGISTRYINDEX));

	return 0;
}

int script_hooks::LUA_on_message(lua_State* L)
{
	if (lua_gettop(L) != 1 || !lua_isfunction(L, 1))
	{
		return luaL_error(L, "expecting arguments: function(on_message(msg))");
	}
	if (!context) return 0;
	context->on_message.emplace_back(lua_function(L)).set(luaL_ref(L, LUA_REGISTRYINDEX));

	return 0;
}

int script_hooks::LUA_create_os_thread(lua_State* L)
{
	if (!context) return 0;
	if (lua_gettop(L) != 1 || !lua_isfunction(L, 1))
	{
		return luaL_error(L, "expecting arguments: function(on_thread_creation)");
	}
	//find state
	std::shared_ptr<lua_function> g_cache;
	for (auto& s : context->scripts)
		if (s.state == L)
		{
			g_cache = std::make_shared<lua_function>(s.state);
			break;
		}
	g_cache->set(luaL_ref(L, LUA_REGISTRYINDEX));
	fibers.emplace_back(std::make_unique<fiber>(g_cache, [] {
		fiber::get_current()->data->call();
	}));
	return 0;
}

int script_hooks::LUA_send_message(lua_State* L)
{
	if (lua_gettop(L) != 1 || !lua_isstring(L, 1))
	{
		return luaL_error(L, "expecting arguments: string(message)");
	}
	if (g_Session.is_server)
	{
		return luaL_error(L, "attempt to call client.send_message on server. try using: server.broadcast");
	}
	std::string message = std::string(lua_tostring(L, 1));
	send_packet(g_Client.connected_server, chat::create_message_packet(message));
	return 0;
}

int script_hooks::LUA_connections(lua_State* L)
{
	if (lua_gettop(L) != 0)
	{
		return luaL_error(L, "expecting no arguments");
	}
	if (!g_Session.is_server)
	{
		return luaL_error(L, "attempt to call server.connections on client. a client does not know who is connected");
	}
	std::vector<std::tuple<std::string, std::string>> connections{};
	lua_newtable(L);
	for (size_t i = 0; i < g_Session.server_instance.connections.size(); i++)
	{
		lua_pushstring(L, to_ip(g_Session.server_instance.connections[i]).c_str());
		lua_rawseti(L, -2, i + 1);
	}

	return 1;
}

int script_hooks::LUA_broadcast(lua_State* L)
{
	if (lua_gettop(L) != 1 || !lua_isstring(L, 1))
	{
		return luaL_error(L, "expecting arguments: string(message)");
	}
	if (!g_Session.is_server)
	{
		return luaL_error(L, "attempt to call server.broadcast on client. try using: client.send_message");
	}
	std::string message = std::string(lua_tostring(L, 1));
	for (const auto& client : g_Session.server_instance.connections)
		send_packet(client, net::make_broadcast_packet(message));
	return 0;
}

int script_hooks::LUA_send(lua_State* L)
{
	if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_isstring(L, 2))
	{
		return luaL_error(L, "expecting arguments: string(recipient), string(content)");
	}
	if (!g_Session.is_server)
	{
		return luaL_error(L, "attempt to call server.send on client. try using: client.send_message");
	}
	std::string recipient = std::string(lua_tostring(L, 1));
	std::string message = std::string(lua_tostring(L, 2));
	for (const auto& client : g_Session.server_instance.connections)
		if (recipient == to_ip(client))
		{
			send_packet(client, net::make_broadcast_packet(message));
			lua_pushboolean(L, true);
			return 1;
		}
	lua_pushboolean(L, false);
	return 1;
}

int script_hooks::LUA_thread_sleep(lua_State* L)
{
	if (lua_gettop(L) != 1 || !lua_isinteger(L, 1))
	{
		return luaL_error(L, "expecting arguments: int(time_ms)");
	}
	const int time = lua_tointeger(L, 1);
	fiber::get_current()->yield(std::chrono::milliseconds(time));
	return 0;
}



script_hooks::script_hooks()
{
	init();
}

void script_hooks::init()
{
	context = this;
	//for(const auto )
}

script_hooks::~script_hooks()
{
	for (auto& sc : scripts)
		lua_close(sc.state);
}
std::vector<std::string> message_history{};
void add_to_message_queue(display_queue& queue, const std::string func)
{
	std::lock_guard guard(g_Queue.lock);
	queue.stack.emplace_back(func);
}

void message_queue_loop()
{
	while (true)
	{
		if (g_Queue.halt || g_Queue.stack.empty()) 
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}
		g_Queue.lock.lock();
		for (const auto& message : g_Queue.stack)
		{
			if (!message.empty())
			{
				message_history.emplace_back(message);
				std::cout << message;
			}
		}
		g_Queue.stack.clear();
		g_Queue.lock.unlock();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
}

void entry();

#define FLOG(x, ...) add_to_message_queue(g_Queue, std::format(x, __VA_ARGS__));
#define FINFO(x, ...) add_to_message_queue(g_Queue, std::format(std::string("[info] ") + std::string(x), __VA_ARGS__));
#define SYNC_FINFO(x, ...) std::cout << "[info] " << std::format(x, __VA_ARGS__); message_history.emplace_back("[info] " + std::format(x, __VA_ARGS__));
#define FERROR(x, ...) add_to_message_queue(g_Queue, std::format(std::string("[error] ") + std::string(x), __VA_ARGS__));
#define SYNC_FERROR(x, ...) std::cout << "[error] " << std::format(x, __VA_ARGS__);  message_history.emplace_back("[error] " + std::format(x, __VA_ARGS__));
#define LOG(x) add_to_message_queue(g_Queue, std::cout << std::format(x));
#define INFO(x) add_to_message_queue(g_Queue, std::cout << "[info] " << std::format(x));
#define SYNC_INFO(x) std::cout << "[info] " << std::format(x);
#define CERROR(x) add_to_message_queue(g_Queue, std::cout << "[error] " << std::format(x));
#define SYNC_ERROR(x) std::cout << "[error] " << std::format(x);
#define FINPUT(x, name1) std::string name1; {\
std::cout << "[input] " << std::format(x); std::getline(std::cin, name1);\
}\

void script_hooks::load(bool re /*= false*/)
{
	
	fibers.clear();

	on_client.clear();
	on_server.clear();
	on_start.clear();
	on_message.clear();
	on_connection.clear();
	on_compose.clear();

	context = this;
	if (re)
	{
		for (auto& sc : scripts)
		{
			if (sc.state)
				lua_close(sc.state);
		}
		FINFO("[LUA] Scripts reloaded\n", "");
	}
	scripts.clear();
	init();
	WCHAR path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	std::wstring wpstr = path;
	std::string plugin_dir = std::filesystem::path(WideStringToString(wpstr)).parent_path().string() + "\\plugins\\";
	if (std::filesystem::exists(plugin_dir))
	{
		for (const auto& entry : std::filesystem::directory_iterator(plugin_dir))
		{
			if (!entry.path().string().ends_with(".lua")) continue;
			auto& sc = scripts.emplace_back(lua_script());
			sc.name = entry.path().filename().string();
			sc.init();
			sc.check(luaL_dofile(sc.state,
				entry.path().string().c_str()));

			if (!re) FINFO("[LUA] Loaded {}\n", sc.name);
		}
	}
	for (auto& func : script_hooks::context->on_start)
	{
		func.call();
	}
	for (auto& func : (!g_Session.is_server ? script_hooks::context->on_client : script_hooks::context->on_server))
	{
		func.call();
	}
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






void handle_connection_incoming(SOCKET c, const std::function<void(bool, char*, int)>& callback)
{
	char buffer[512];
	while (c != INVALID_SOCKET)
	{
		int bytesReceived = recv(c, buffer, sizeof(buffer), 0);
		if (WSAGetLastError())
		{
			std::cout << get_last_winsock_error();
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
void server_input()
{
	bool f1_pressed = false;
	while (true)
	{
		if (own_window_handle != GetForegroundWindow())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}
		if (GetAsyncKeyState(VK_F1) != 0)
			f1_pressed = true;
		else if (f1_pressed)
		{
			f1_pressed = false;
			g_ScriptHook.load(true);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}
void user_input(client_connected_server& client)
{
	bool f8_pressed = false;
	bool f1_pressed = false;
	bool in_menu = false;
	while (true)
	{
		if (own_window_handle != GetForegroundWindow())
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}
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
				if (c == "\r") break;
				//if (c == ";") continue;
				if (c == "\b")
				{
					if(!buffer.empty())
						buffer.pop_back();
				}
				else
					buffer += c;
				std::cout << c;
				if (c == "\b")
					std::cout << " \b";
			}
			for (const auto& c : buffer + "> Compose message: ")
				std::cout << "\b";
			for (const auto& c : buffer + "> Compose message: ")
				std::cout << " ";
			for (const auto& c : buffer + "> Compose message: ")
				std::cout << "\b";
			g_Queue.halt = false;
			in_menu = false;
			bool should_send{true};
			for (auto& func : script_hooks::context->on_compose)
			{
				if (func.r_call_table({std::make_tuple("message", buffer)}, 1))
				{
					should_send = false;
					break;
				}
			}
			if(should_send)
				send_packet(client.connected_server, chat::create_message_packet(buffer));
		}
		else if (GetAsyncKeyState(VK_F1) != 0)
			f1_pressed = true;
		else if (f1_pressed)
		{
			f1_pressed = false;
			g_ScriptHook.load(true);
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
		for (auto& func : script_hooks::context->on_message)
		{
			if (func.r_call_table({ {"username", g_Session.is_server ? to_ip(c) : username}, {"message", msg} }, 1))
			{
				return;
			}
		}

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
			const auto ip = to_ip(c);
			send_packet(c, net::packet_chadder_connection{});

			g_Session.server_instance.username_map[std::to_string((uint64_t)&c)] = std::string(username + "#" + std::to_string(c.address.sin_port) + "_" + std::to_string(generate_uid()));
			for (auto& func : script_hooks::context->on_connection)
			{
				if (!func.r_call_table({ {"ip", ip}, {"username", to_ip(c)}}, 1))
				{
					g_Session.server_instance.username_map.erase(to_ip(c));
					closesocket(c.socket);
					remove_connection(c);
					return;
				}
			}
			send_packet(c, net::make_broadcast_packet("Rechadder is still in beta! And this is the testing server."));
			FINFO("Client connected ({}): {}\n", brand, to_ip(c));
		}
		else
		{
			//FINFO("Handshake established ({})\n", "52, 62");
			for (auto& func : script_hooks::context->on_client)
			{
				func.call();
			}
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
			g_Session.server_instance.username_map.erase(to_ip(c));

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


void main_fiber()
{
	while (true)
	{
		static bool ensure_main_fiber = (ConvertThreadToFiber(nullptr), true);
		m_fiber = GetCurrentFiber();
		for (const auto& f : fibers)
		{
			if (!f->m_wake_time.has_value() || f->m_wake_time.value() <= std::chrono::high_resolution_clock::now())
			{
				SwitchToFiber(f->fiber_id);
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
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
	g_ScriptHook.load();

	std::thread([]()
		{
			handle_connections();
		}
	).detach();
	
	std::thread([]()
		{
			server_input();
		}
	).detach();
	main_fiber();
	
}
void start_client(const std::string& ip, bool threads = true)
{
	SetConsoleTitleA("ReChadder - Client");
	
	g_Client.connected_server.socket = create_socket();
	//SYNC_FINFO("Connecting to {}...\n", ip);
	sockaddr_in sockAddr = create_socket_addr(ip, g_Session.port);
	if (connect(g_Client.connected_server.socket, (sockaddr*)(&sockAddr), sizeof(sockAddr)) != 0)
	{
		SYNC_FINFO("Failed to connected to: {}. Error code: {}\n", ip, get_last_winsock_error());
		entry();
	}
	SetConsoleTitleA(std::format("ReChadder - {}", ip).c_str());
	//SYNC_FINFO("Establishing handshake... {}\n", ip);
	auto con = net::packet_chadder_connection{};
	if(g_Session.a_username.length() < 16)
		memcpy(con.username, net::handle_raw_string(g_Session.a_username.c_str()).c_str(),
			net::handle_raw_string(g_Session.a_username.c_str()).length());
	send_packet(g_Client.connected_server, con);
	std::thread([]()
		{
			auto h = g_Client.connected_server;
			handle_connection(h);
		}
	).detach();
	g_ScriptHook.load();
	if (threads) std::thread([] {
		httplib::Server svr;
		svr.set_mount_point("/web", "./www");
		svr.Get(R"(/internal/send_message/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
			auto numbers = req.matches[1];
			if (!g_Session.is_server)
			{
				bool should_send{ true };
				for (auto& func : script_hooks::context->on_compose)
				{
					if (func.r_call_table({ std::make_tuple("message", numbers.str()) }, 1))
					{
						should_send = false;
						break;
					}
				}
				if (should_send)
					send_packet(g_Client.connected_server, chat::create_message_packet(numbers.str()));
			}
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Credentials", "true");
			res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
			res.set_content("sent message: " + numbers.str(), "text/plain");
		});
		svr.Get(R"(/internal/message_history/)", [&](const httplib::Request& req, httplib::Response& res) {
			std::string h{};
			const auto& count = message_history.size();
			size_t index{};
			for (const auto& e : message_history)
				if(count <= 50 || index++ > count-50)
					h += e + "\n";
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Credentials", "true");
			res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
			res.set_content(h, "text/plain");
		});
		svr.Get(R"(/internal/session_details/)", [&](const httplib::Request& req, httplib::Response& res) {
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Credentials", "true");
			res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
			nlohmann::json j;
			j["ip"] = g_Session.a_ip;
			j["port"] = g_Session.port;
			j["username"] = g_Session.a_username;
			res.set_content(j.dump(), "application/json");
		});
		svr.Options(R"(/internal/connect/)", [&](const httplib::Request& req, httplib::Response& res) {
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Credentials", "true");
			res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
			res.set_header("Access-Control-Allow-Headers", "*");
			});
		svr.Post(R"(/internal/connect/)", [&](const httplib::Request& req, httplib::Response& res) {
			nlohmann::json j_object = nlohmann::json::parse(req.body);
			g_Session.port = std::stoi(j_object["port"].get<std::string>());
			g_Session.a_port = std::stoi(j_object["port"].get<std::string>());
			g_Session.a_username = j_object["username"].get<std::string>();
			message_history.clear();
			std::thread([=] {
				closesocket(g_Client.connected_server.socket);
				start_client(j_object["ip"].get<std::string>(), false);
			}).detach();
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Credentials", "true");
			res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
			res.set_header("Access-Control-Allow-Headers", "*");
			res.set_content("ok", "text/plain");
		});
		svr.Get(R"(/internal/reload_scripts/)", [&](const httplib::Request& req, httplib::Response& res) {
			std::string h{};
			script_hooks::context->load(true);
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Credentials", "true");
			res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
		});
		svr.Get(R"(/internal/session/)", [&](const httplib::Request& req, httplib::Response& res) {
			nlohmann::json j;
			std::vector<std::string> script_names{};
			for (const auto& script : script_hooks::context->scripts)
				script_names.emplace_back(script.name);
			if (script_names.empty())
				script_names.emplace_back("No scripts loaded");
			j["scripts"] = script_names;
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Credentials", "true");
			res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
			res.set_content(j.dump(), "application/json");
		});
		FINFO("Web client started at: http://localhost:8080/web/\n", "");
		svr.listen("0.0.0.0", 8080);
		}).detach();
	if (threads) std::thread([]()
		{
			user_input(g_Client);
		}
	).detach();
	main_fiber();
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

	
	//std::cout << "(re)Chadder(box) - A simple communication system - made by jayphen\nOnce connected to a server, press TAB to compose a message.\n";

	for (auto& func : script_hooks::context->on_start)
	{
		func.call_table({
			{"version", "alpha"}
		});
	}
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