#pragma once
#include <AL/Common.hpp>

#include <AL/Collections/Array.hpp>

#include <AL/Network/TcpSocket.hpp>
#include <AL/Network/SocketExtensions.hpp>

#define APRS_SOFTWARE_NAME    "libAPRS-IS"
#define APRS_SOFTWARE_VERSION "0.1"

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

				position.Latitude  = latitude_hours + (latitude_minutes / 60.0f) + (latitude_seconds / 3600.0f);
				position.Longitude = longitude_hours + (longitude_minutes / 60.0f) + (longitude_seconds / 3600.0f);

				if (latitude_north_south == 'S') position.Latitude  = -position.Latitude;
				if (longitude_west_east  == 'W') position.Longitude = -position.Longitude;

				return true;
			}

			return false;
		}
	};

	namespace IS
	{
		namespace Connections
		{
			class IConnection
			{
				IConnection(IConnection&&) = delete;
				IConnection(const IConnection&) = delete;

			public:
				IConnection()
				{
				}

				virtual ~IConnection()
				{
				}

				virtual bool IsBlocking() const = 0;

				virtual bool IsConnected() const = 0;

				// @throw AL::Exception
				virtual void Connect() = 0;

				virtual void Disconnect() = 0;

				// @throw AL::Exception
				virtual void SetBlocking(bool value) = 0;

				// @throw AL::Exception
				// @return false on connection closed
				virtual bool ReadLine(AL::String& value, bool block) = 0;

				// @throw AL::Exception
				// @return false on connection closed
				virtual bool WriteLine(const AL::String& value) = 0;
			};

			class TcpConnection
				: public IConnection
			{
				AL::Network::TcpSocket  socket;
				AL::Network::IPEndPoint remoteEP;

			public:
				explicit TcpConnection(const AL::Network::IPEndPoint& remoteEP)
					: socket(
						remoteEP.Host.GetFamily()
					),
					remoteEP(
						remoteEP
					)
				{
				}

				virtual ~TcpConnection()
				{
					if (IsConnected())
					{

						Disconnect();
					}
				}

				virtual bool IsBlocking() const override
				{
					return socket.IsBlocking();
				}

				virtual bool IsConnected() const override
				{
					return socket.IsConnected();
				}

				// @throw AL::Exception
				virtual void Connect() override
				{
					AL_ASSERT(
						!IsConnected(),
						"TcpConnection already connected"
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

				virtual void Disconnect() override
				{
					socket.Close();
				}

				// @throw AL::Exception
				virtual void SetBlocking(bool value) override
				{
					socket.SetBlocking(value);
				}

				// @throw AL::Exception
				// @return false on connection closed
				virtual bool ReadLine(AL::String& value, bool block) override
				{
					AL_ASSERT(
						IsConnected(),
						"TcpConnection not connected"
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

								throw AL::Exception(
									"AL::Network::TcpSocket unexpectedly closed"
								);
							}
						}
						catch (AL::Exception&)
						{
							Disconnect();

							throw;
						}

						if (numberOfBytesReceived == 0)
						{

							return false;
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

								throw AL::Exception(
									"AL::Network::TcpSocket unexpectedly closed"
								);
							}
						}
						catch (AL::Exception&)
						{
							Disconnect();

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

					return true;
				}

				// @throw AL::Exception
				// @return false on connection closed
				virtual bool WriteLine(const AL::String& value) override
				{
					AL_ASSERT(
						IsConnected(),
						"TcpConnection not connected"
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

							return false;
						}
					}
					catch (AL::Exception&)
					{
						Disconnect();

						throw;
					}

					return true;
				}
			};
		}

		typedef AL::Collections::Array<AL::String> Filter;

		template<typename T_CONNECTION>
		class Client
		{
			static_assert(
				AL::Is_Base_Of<Connections::IConnection, T_CONNECTION>::Value,
				"T_CONNECTION must inherit Connections::IConnection"
			);

			bool          isBlocking  = false;
			bool          isConnected = false;

			Filter        filter;
			AL::String    callsign;
			AL::uint16    passcode;
			T_CONNECTION* lpConnection;

			Client(Client&&) = delete;
			Client(const Client&) = delete;

		public:
			Client(AL::String&& callsign, AL::uint16 passcode, Filter&& filter)
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
			template<typename ... TArgs>
			void Connect(TArgs ... args)
			{
				AL_ASSERT(
					!IsConnected(),
					"Client already connected"
				);

				lpConnection = new T_CONNECTION(
					AL::Forward<TArgs>(args) ...
				);

				try
				{
					lpConnection->SetBlocking(IsBlocking());

					lpConnection->Connect();

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
			}

			void Disconnect()
			{
				if (IsConnected())
				{
					lpConnection->Disconnect();
					delete lpConnection;

					isConnected = false;
				}
			}

			// @throw AL::Exception
			// @return 0 on error
			// @return -1 if would block
			int ReadPacket(AL::String& line, Packet& packet)
			{
				AL_ASSERT(
					IsConnected(),
					"Client not connected"
				);

				if (!lpConnection->ReadLine(line, false))
				{

					return -1;
				}

				if (!Packet::Decode(packet, line))
				{
					if (line.StartsWith('#'))
					{

						return -1;
					}

					return 0;
				}

				return 1;
			}

			// @throw AL::Exception
			void WritePacket(AL::String& line, const Packet& packet)
			{
				AL_ASSERT(
					IsConnected(),
					"Client not connected"
				);

				line = packet.Encode();

				try
				{
					lpConnection->WriteLine(
						line
					);
				}
				catch (AL::Exception& exception)
				{

					throw AL::Exception(
						AL::Move(exception),
						"Error sending Packet [Buffer: %s]",
						line.GetCString()
					);
				}
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
						sb << " filter";

						for (auto& f : GetFilter())
							sb << ' ' << f;
					}

					return sb.ToString();
				}();

				try
				{
					lpConnection->WriteLine(
						line
					);
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

					while (lpConnection->ReadLine(line, true) || !IsBlocking())
					{
						if (AL::Regex::Match(matches, "^# logresp ([^ ]+) (.+)$", line))
						{
							if (matches[2].StartsWith("verified", true))
							{

								return true;
							}

							return false;
						}
					}
				}

				return true;
			}
		};

		typedef Client<Connections::TcpConnection> TcpClient;
	}
}
