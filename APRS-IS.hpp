#pragma once
#include <AL/Common.hpp>

#include <AL/Collections/Array.hpp>
#include <AL/Collections/Queue.hpp>
#include <AL/Collections/LinkedList.hpp>
#include <AL/Collections/Dictionary.hpp>

#include <AL/Network/TcpSocket.hpp>
#include <AL/Network/SocketExtensions.hpp>

#if defined(AL_PLATFORM_WINDOWS)
	#undef SendMessage
#endif

#define APRS_SOFTWARE_NAME    "libAPRS-IS"
#define APRS_SOFTWARE_VERSION "0.2"

namespace APRS
{
	struct Packet
	{
		AL::String IGate;
		AL::String QFlag;
		AL::String ToCall;
		AL::String Sender;
		AL::String Content;
		AL::String DigiPath;

		bool IsMessage() const
		{
			if (Content.GetLength() == 0)
			{

				return false;
			}

			switch (*Content.GetCString())
			{
				case ':':
					return true;
			}

			return false;
		}
		bool IsPosition() const
		{
			if (Content.GetLength() == 0)
			{

				return false;
			}

			switch (*Content.GetCString())
			{
				case '!':
				case '=':
				// case '/':
				// case '@':
					return true;
			}

			return false;
		}

		AL::String Encode() const
		{
			return AL::String::Format(
				"%s>%s,%s:%s",
				Sender.GetCString(),
				ToCall.GetCString(),
				DigiPath.GetCString(),
				Content.GetCString()
			);
		}

		static bool Decode(Packet& packet, const AL::String& string)
		{
			if (!string.StartsWith('#'))
			{
				AL::Regex::MatchCollection matches;

				if (AL::Regex::Match(matches, "^([^>]+?)>([^,]+?),((?:([^,]+),)*?)(q[A-Z]{2}),([^:]+):(.+)$", string))
				{
					packet =
					{
						.IGate    = AL::Move(matches[6]),
						.QFlag    = AL::Move(matches[5]),
						.ToCall   = AL::Move(matches[2]),
						.Sender   = AL::Move(matches[1]),
						.Content  = AL::Move(matches[7]),
						.DigiPath = AL::Move(matches[3])
					};

					if (packet.DigiPath.GetLength() != 0)
					{
						packet.DigiPath.Erase(
							--packet.DigiPath.end()
						);
					}

					return true;
				}
			}

			return false;
		}
	};

	struct Message
	{
		AL::String Ack;
		AL::String Content;
		AL::String Destination;

		Packet Encode(const AL::String& tocall, const AL::String& sender, const AL::String& digipath) const
		{
			Packet packet =
			{
				.ToCall   = tocall,
				.Sender   = sender,
				.Content  = AL::String::Format(":%-09s:%s", Destination.GetCString(), Content.GetCString()),
				.DigiPath = digipath
			};

			if (Ack.GetLength() != 0)
			{
				packet.Content.Append(
					AL::String::Format("{%s", Ack.GetCString())
				);
			}

			return packet;
		}

		static bool Decode(Message& message, const Packet& packet)
		{
			AL::Regex::MatchCollection matches;

			if (!AL::Regex::Match(matches, "^:([^: ]+) *:(.*)$", packet.Content))
			{

				return false;
			}

			auto content        = AL::Move(matches[2]);
			message.Destination = AL::Move(matches[1]);

			if (!AL::Regex::Match(matches, "^(.*)\\{(.+)$", content))
			{
				message.Ack.Clear();
				message.Content = AL::Move(content);
			}
			else
			{
				message.Ack     = AL::Move(matches[2]);
				message.Content = AL::Move(matches[1]);
			}

			return true;
		}
	};

	struct Position
	{
		AL::int32        Altitude;
		AL::Float        Latitude;
		AL::Float        Longitude;

		AL::String       Comment;
		AL::String::Char SymbolTable;
		AL::String::Char SymbolTableKey;

		Packet Encode(const AL::String& tocall, const AL::String& sender, const AL::String& digipath) const
		{
			AL::int16  latitude_hours   = 0, longitude_hours   = 0;
			AL::uint16 latitude_minutes = 0, longitude_minutes = 0;
			AL::uint16 latitude_seconds = 0, longitude_seconds = 0;

			// TODO: math

			Packet packet =
			{
				.ToCall   = tocall,
				.Sender   = sender,
				.Content  = AL::String::Format(
					"!%i%02u.%02u%c%c%i%02u.%02u%c%c/A=%06li%s",
					latitude_hours,
					latitude_minutes,
					latitude_seconds,
					(latitude_hours >= 0) ? 'N' : 'S',
					SymbolTable,
					longitude_hours,
					longitude_minutes,
					longitude_seconds,
					(longitude_hours >= 0) ? 'E' : 'W',
					SymbolTableKey,
					Altitude,
					Comment.GetCString()
				),
				.DigiPath = digipath
			};

			return packet;
		}

		static bool Decode(Position& position, const Packet& packet)
		{
			// TODO: add support for ambiguity
			// TODO: add support for timestamp
			// TODO: add support for compression

			// COMPRESSED POSITION REPORT DATA FORMATS
			//	https://www.aprs.org/doc/APRS101.PDF#page=46
			// DF Report Format — without Timestamp
			//	https://www.aprs.org/doc/APRS101.PDF#page=44
			// Lat/Long Position Report Format — with Data Extension and Timestamp
			//	https://www.aprs.org/doc/APRS101.PDF#page=43

			AL::Regex::MatchCollection matches;

			if (AL::Regex::Match(matches, "^[!=](\\d\\d)(\\d\\d)\\.(\\d\\d)([NS])(.)(\\d\\d\\d)(\\d\\d)\\.(\\d\\d)([WE])(.)(.*(?=\\/A=-?\\d{6}))?(\\/A=(-?\\d*))?\\s*(.+[^\\s]+)?\\s*$", packet.Content))
			{
				auto latitude_hours       = AL::FromString<AL::int16>(matches[1]);
				auto latitude_minutes     = AL::FromString<AL::uint16>(matches[2]);
				auto latitude_seconds     = AL::FromString<AL::uint16>(matches[3]);
				auto latitude_north_south = matches[4][0];
				position.SymbolTable      = matches[5][0];
				auto longitude_hours      = AL::FromString<AL::int16>(matches[6]);
				auto longitude_minutes    = AL::FromString<AL::uint16>(matches[7]);
				auto longitude_seconds    = AL::FromString<AL::uint16>(matches[8]);
				auto longitude_west_east  = matches[9][0];
				position.SymbolTableKey   = matches[10][0];
				position.Altitude         = AL::FromString<AL::int16>(matches[13]);
				position.Comment          = AL::Move(matches[14]);

				// position.Latitude  = latitude_hours + (latitude_minutes / 60.0f) + (latitude_seconds / 3600.0f);
				// position.Longitude = longitude_hours + (longitude_minutes / 60.0f) + (longitude_seconds / 3600.0f);

				position.Latitude  = latitude_hours + (latitude_minutes / 60.0f) + (latitude_seconds / 6000.0f);
				position.Longitude = longitude_hours + (longitude_minutes / 60.0f) + (longitude_seconds / 6000.0f);

				if (latitude_north_south == 'S') position.Latitude  = -position.Latitude;
				if (longitude_west_east  == 'W') position.Longitude = -position.Longitude;

				return true;
			}

			return false;
		}
	};

	namespace IS
	{
		typedef AL::Function<void()>                                                   ClientOnMessageSentCallback;

		typedef AL::EventHandler<void()>                                               ClientOnConnectEventHandler;
		typedef AL::EventHandler<void()>                                               ClientOnDisconnectEventHandler;
		typedef AL::EventHandler<void(const Packet& packet)>                           ClientOnReceivePacketEventHandler;
		typedef AL::EventHandler<void(const Packet& packet, const Message& message)>   ClientOnReceiveMessageEventHandler;
		typedef AL::EventHandler<void(const Packet& packet, const Position& position)> ClientOnReceivePositionEventHandler;

		class Client
		{
			class Connection
			{
				AL::Network::TcpSocket  socket;
				AL::Network::IPEndPoint remoteEP;

			public:
				explicit Connection(const AL::Network::IPEndPoint& remoteEP)
					: socket(
						remoteEP.Host.GetFamily()
					),
					remoteEP(
						remoteEP
					)
				{
				}

				virtual ~Connection()
				{
					if (IsOpen())
					{

						Close();
					}
				}

				bool IsBlocking() const
				{
					return socket.IsBlocking();
				}

				bool IsOpen() const
				{
					return socket.IsConnected();
				}

				// @throw AL::Exception
				void Open()
				{
					AL_ASSERT(
						!IsOpen(),
						"Connection already open"
					);

					try
					{
						socket.Open();

						try
						{
							if (!socket.Connect(remoteEP))
							{

								throw AL::Exception(
									"Connection timed out"
								);
							}
						}
						catch (AL::Exception&)
						{
							socket.Close();

							throw;
						}
					}
					catch (AL::Exception& exception)
					{

						throw AL::Exception(
							"Error connecting to %s:%u",
							remoteEP.Host.ToString().GetCString(),
							remoteEP.Port
						);
					}
				}

				void Close()
				{
					socket.Close();
				}

				// @throw AL::Exception
				void SetBlocking(bool value)
				{
					socket.SetBlocking(value);
				}

				// @throw AL::Exception
				// @return 0 on connection closed
				// @return -1 if would block
				int ReadLine(AL::String& value, bool block)
				{
					AL_ASSERT(
						IsConnected(),
						"Connection not open"
					);

					value.Clear();

					AL::String::Char buffer[2];
					AL::size_t       numberOfBytesReceived;

					auto buffer_Update = [&buffer]()
					{
						if ((buffer[0] == '\r') && (buffer[1] == '\n'))
						{

							return false;
						}

						buffer[0] = buffer[1];

						return true;
					};

					if (!block)
					{
						try
						{
							if (!AL::Network::SocketExtensions::TryReceiveAll(socket, &buffer[1], sizeof(buffer[1]), numberOfBytesReceived))
							{
								Close();

								return 0;
							}
						}
						catch (AL::Exception&)
						{
							Close();

							throw;
						}

						if (numberOfBytesReceived == 0)
						{

							return -1;
						}

						value.Append(
							buffer[1]
						);

						buffer_Update();
					}

					do
					{
						try
						{
							if (!AL::Network::SocketExtensions::ReceiveAll(socket, &buffer[1], sizeof(buffer[1]), numberOfBytesReceived))
							{
								Close();

								return 0;
							}
						}
						catch (AL::Exception&)
						{
							Close();

							throw;
						}

						value.Append(
							buffer[1]
						);
					} while (buffer_Update());

					value.Erase(
						value.GetLength() - 2,
						2
					);

					return 1;
				}

				// @throw AL::Exception
				// @return false on connection closed
				bool WriteLine(const AL::String& value)
				{
					AL_ASSERT(
						IsConnected(),
						"Connection not open"
					);

					auto valueLength = value.GetLength();

					if (valueLength > 510)
					{

						valueLength = 510;
					}

					AL::size_t numberOfBytesSent;

					static constexpr char EOL[] = "\r\n";

					try
					{
						if (!AL::Network::SocketExtensions::SendAll(socket, value.GetCString(), valueLength, numberOfBytesSent) ||
							!AL::Network::SocketExtensions::SendAll(socket, EOL, sizeof(EOL) - 1, numberOfBytesSent))
						{
							Close();

							return false;
						}
					}
					catch (AL::Exception&)
					{
						Close();

						throw;
					}

					return true;
				}
			};

			typedef AL::Collections::Queue<ClientOnMessageSentCallback>               _MessageAckCallbackQueue;
			typedef AL::Collections::Dictionary<AL::String, _MessageAckCallbackQueue> _MessageAckCallbacks;

			bool                 isBlocking  = false;
			bool                 isConnected = false;

			AL::String           filter;
			AL::String           callsign;
			AL::uint16           passcode;
			AL::String           packetBuffer;
			Connection*          lpConnection;
			_MessageAckCallbacks messageCallbacks;

			Client(Client&&) = delete;
			Client(const Client&) = delete;

		public:
			// @throw AL::Exception
			AL::Event<ClientOnConnectEventHandler>         OnConnect;
			AL::Event<ClientOnDisconnectEventHandler>      OnDisconnect;

			// @throw AL::Exception
			AL::Event<ClientOnReceivePacketEventHandler>   OnReceivePacket;
			// @throw AL::Exception
			AL::Event<ClientOnReceiveMessageEventHandler>  OnReceiveMessage;
			// @throw AL::Exception
			AL::Event<ClientOnReceivePositionEventHandler> OnReceivePosition;

			Client(AL::String&& callsign, AL::uint16 passcode, AL::String&& filter)
				: filter(
					AL::Move(filter)
				),
				callsign(
					AL::Move(callsign)
				),
				passcode(
					passcode
				)
			{
			}

			virtual ~Client()
			{
				if (IsConnected())
				{

					Disconnect();
				}
			}

			bool IsBlocking() const
			{
				return isBlocking;
			}

			bool IsConnected() const
			{
				return isConnected;
			}

			auto& GetFilter() const
			{
				return filter;
			}

			auto& GetCallsign() const
			{
				return callsign;
			}

			// @throw AL::Exception
			void SetBlocking(bool value)
			{
				isBlocking = value;

				if (IsConnected())
				{
					lpConnection->SetBlocking(
						value
					);
				}
			}

			// @throw AL::Exception
			void Connect(const AL::Network::IPEndPoint& remoteEP)
			{
				AL_ASSERT(
					!IsConnected(),
					"Client already connected"
				);

				lpConnection = new Connection(
					remoteEP
				);

				try
				{
					lpConnection->SetBlocking(IsBlocking());

					lpConnection->Open();

					if (!Authenticate())
					{

						throw AL::Exception(
							"Authentication failed"
						);
					}
				}
				catch (AL::Exception&)
				{
					delete lpConnection;

					throw;
				}

				isConnected = true;

				try
				{
					OnConnect.Execute();
				}
				catch (AL::Exception&)
				{
					Disconnect();

					throw;
				}
			}

			void Disconnect()
			{
				if (IsConnected())
				{
					messageCallbacks.Clear();

					lpConnection->Close();
					delete lpConnection;

					isConnected = false;

					OnDisconnect.Execute();
				}
			}

			// @throw AL::Exception
			// @return false on connection closed
			bool Update()
			{
				AL_ASSERT(
					IsConnected(),
					"Client not connected"
				);

				Packet packet;

				switch (ReadPacket(packet))
				{
					case 0:  return false;
					case -1: return true;
					case -2: return true;
				}

				if (OnReadPacket(packet))
				{
					OnReceivePacket.Execute(packet);

					if (packet.IsMessage())
					{
						Message message;

						if (Message::Decode(message, packet) && OnReadMessage(packet, message))
						{
							AL::Regex::MatchCollection matches;

							if (AL::Regex::Match(matches, "^ack(.+)$", message.Content))
							{
								auto it = messageCallbacks.Find(matches[1]);

								if (it != messageCallbacks.end())
								{
									ClientOnMessageSentCallback callback;
									it->Value.Dequeue(callback);

									if (it->Value.GetSize() == 0)
										messageCallbacks.Erase(it);

									callback();

									return true;
								}
							}

							OnReceiveMessage.Execute(packet, message);

							return true;
						}
					}
					else if (packet.IsPosition())
					{
						Position position;

						if (Position::Decode(position, packet) && OnReadPosition(packet, position))
						{
							OnReceivePosition.Execute(packet, position);

							return true;
						}
					}
				}

				return true;
			}

			// Note: This bypasses automatic ack handling
			// @throw AL::Exception
			// @return false on connection closed
			bool SendPacket(const Packet& value)
			{
				AL_ASSERT(
					IsConnected(),
					"Client not connected"
				);

				return WritePacket(value);
			}

			// @throw AL::Exception
			// @return false on connection closed
			bool SendMessage(const Message& value, const AL::String& tocall, const AL::String& path)
			{
				AL_ASSERT(
					IsConnected(),
					"Client not connected"
				);

				return WritePacket(value.Encode(tocall, GetCallsign(), path));
			}
			// @throw AL::Exception
			// @return false on connection closed
			bool SendMessage(const Message& value, const AL::String& tocall, const AL::String& path, ClientOnMessageSentCallback&& callback)
			{
				AL_ASSERT(
					IsConnected(),
					"Client not connected"
				);

				if (!WritePacket(value.Encode(tocall, GetCallsign(), path)))
				{

					return false;
				}

				if (value.Ack.GetLength() == 0)
					callback();
				else
					messageCallbacks[value.Ack].Enqueue(AL::Move(callback));

				return true;
			}

			// @throw AL::Exception
			// @return false on connection closed
			bool SendPosition(const Position& value, const AL::String& tocall, const AL::String& path)
			{
				AL_ASSERT(
					IsConnected(),
					"Client not connected"
				);

				return WritePacket(value.Encode(tocall, GetCallsign(), path));
			}

		protected:
			// @throw AL::Exception
			// @return false to stop processing
			virtual bool OnReadPacket(const Packet& packet)
			{
				return true;
			}

			// @throw AL::Exception
			// @return false to stop processing
			virtual bool OnReadMessage(const Packet& packet, const Message& message)
			{
				return true;
			}

			// @throw AL::Exception
			// @return false to stop processing
			virtual bool OnReadPosition(const Packet& packet, const Position& position)
			{
				return true;
			}

		private:
			// @throw AL::Exception
			bool Authenticate()
			{
				auto line = [this]()
				{
					AL::StringBuilder sb;
					sb << "user " << GetCallsign() << " pass " << passcode << " vers " APRS_SOFTWARE_NAME " " APRS_SOFTWARE_VERSION;

					if (GetFilter().GetSize() != 0)
					{
						sb << " filter ";
						sb << GetFilter();
					}

					return sb.ToString();
				}();

				try
				{
					if (!lpConnection->WriteLine(line))
					{

						return false;
					}
				}
				catch (AL::Exception& exception)
				{

					throw AL::Exception(
						AL::Move(exception),
						"Error sending authentication"
					);
				}

				{
					AL::String                 line;
					AL::Regex::MatchCollection matches;

					while (lpConnection->ReadLine(line, true) > 0)
					{
						if (AL::Regex::Match(matches, "^# logresp ([^ ]+) (.+)$", line))
						{
							if (matches[2].StartsWith("verified", true))
							{

								return true;
							}

							break;
						}
					}
				}

				return false;
			}

			// @throw AL::Exception
			// @return 0 on connection closed
			// @return -1 if would block
			// @return -2 on decoding error
			int ReadPacket(Packet& packet)
			{
				try
				{
					switch (lpConnection->ReadLine(packetBuffer, false))
					{
						case 0:
							Disconnect();
							return 0;

						case -1:
							return -1;
					}
				}
				catch (AL::Exception& exception)
				{

					throw AL::Exception(
						AL::Move(exception),
						"Error receiving packet buffer"
					);
				}

				if (!Packet::Decode(packet, packetBuffer))
				{
					if (packetBuffer.StartsWith('#'))
					{

						return -1;
					}

					return -2;
				}

				return 1;
			}

			// @throw AL::Exception
			// @return false on connection closed
			bool WritePacket(const Packet& packet)
			{
				packetBuffer = packet.Encode();

				try
				{
					if (!lpConnection->WriteLine(packetBuffer))
					{
						Disconnect();

						return false;
					}
				}
				catch (AL::Exception& exception)
				{

					throw AL::Exception(
						AL::Move(exception),
						"Error sending Packet [Buffer: %s]",
						packetBuffer.GetCString()
					);
				}

				return true;
			}
		};

		typedef AL::Collections::Array<AL::String> GatewayCommandFilter;

		// @throw AL::Exception
		// @return false if not handled
		typedef AL::Function<bool(const AL::String& sender, const AL::String& prefix, const AL::String& args)> GatewayCommandHandler;

		class Gateway
			: public Client
		{
			struct _CommandContext
			{
				bool                  IsFilterInverted = false;

				AL::String            Prefix;
				GatewayCommandFilter  Filter;
				GatewayCommandHandler Handler;
			};

			AL::Collections::LinkedList<_CommandContext> commands;

		public:
			using Client::Client;

			void RegisterCommand(const AL::String& prefix, GatewayCommandHandler&& handler)
			{
				RegisterCommand(prefix, AL::Move(handler), GatewayCommandFilter());
			}
			void RegisterCommand(const AL::String& prefix, GatewayCommandHandler&& handler, GatewayCommandFilter&& filter, bool invert_filter = false)
			{
				for (auto& command : commands)
				{
					if (command.Prefix.Compare(prefix, AL::True))
					{
						command.IsFilterInverted = invert_filter;
						command.Filter  = AL::Move(filter);
						command.Handler = AL::Move(handler);

						return;
					}
				}

				commands.PushBack(
					{
						.IsFilterInverted = invert_filter,
						.Prefix           = prefix,
						.Filter           = AL::Move(filter),
						.Handler          = AL::Move(handler)
					}
				);
			}

			// @throw AL::Exception
			// @return 0 if not registered
			// @return -1 if not handled
			// @return -2 if rejected by filter
			int ExecuteCommand(const AL::String& sender, const AL::String& prefix, const AL::String& args)
			{
				for (auto& command : commands)
				{
					if (command.Prefix.Compare(prefix, AL::True))
					{
						for (auto& filter : command.Filter)
						{
							if (filter.Compare(sender, AL::True))
							{

								return -2;
							}
						}

						if (!command.Handler(sender, prefix, args))
						{

							return -1;
						}

						return 1;
					}
				}

				return 0;
			}

		protected:
			// @throw AL::Exception
			// @return false to stop processing
			// virtual bool OnReadPacket(const Packet& packet) override
			// {
			// 	if (!Client::OnReadPacket(packet))
			// 		return false;

			// 	return true;
			// }

			// @throw AL::Exception
			// @return false to stop processing
			virtual bool OnReadMessage(const Packet& packet, const Message& message) override
			{
				if (!Client::OnReadMessage(packet, message))
					return false;

				AL::Regex::MatchCollection matches;

				if (AL::Regex::Match(matches, "^([^ ]+) ?(.+)?$", message.Content))
				{
					switch (ExecuteCommand(packet.Sender, matches[1], (matches.GetSize() == 3) ? matches[2] : ""))
					{
						case 0:  return true;
						case -1: return true;
						case -2: return false;
						default: return false;
					}
				}

				return true;
			}

			// @throw AL::Exception
			// @return false to stop processing
			// virtual bool OnReadPosition(const Packet& packet, const Position& position) override
			// {
			// 	if (!Client::OnReadPosition(packet, position))
			// 		return false;

			// 	return true;
			// }
		};
	}
}
