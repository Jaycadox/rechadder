# ReChadder
A console based text communication application.

## What's the 'Re' in ReChadder?
Chadder was a WinForms communication application written in C# and using WebSockets. I felt for an application that could be this simple, going lower level would be better. ReChadder uses WinSock with C++ on a CLI.

Support is currently **Widows Only**.

## How to use
ReChadder is extremely simple, the executable can either host a ReChadder server or connect to one, being a client. Command line arguments are supported, simply type `rechadder --help` in the command line.

Once connected to a server, you can press `TAB` to compose a message, then `ENTER` to send it.

A command handler is built into the source, allowing modified servers which could host certain commands with various functionality. The default commands are:

- `\\leave` Disconnects you from the server
- `\\online` Displays all users connected to a server

The default implementation of the ReChadder will assign you a unique ID after your username, this changes every time you connect to the server and allows people to identify others, even if they have the same username.

**ReChadder is NOT a secure platform. All data is sent unencrypted via TCP.**

## How it works
Data is sent via packets. All packets share the same first two objects.
```cpp
struct packet_identifier {
	short id{};
	bool server{};
};
```
This is so the reciever knows what packet is being sent, before converting the raw data into a packet which contains more useful information.

The server field indicates if the packet is meant to go to the server or the client, this is redundant and checked, but if it doesn't match the expected value, the packet is dropped.

More details will be coming shortly.

## Build using CMake
```
git clone https://github.com/Jaycadox/rechadder.git
cd rechadder
mkdir build && cd build
cmake ..
```
Then from there, build using Visual Studio.
