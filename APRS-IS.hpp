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
				.Content  = AL::String::Format(":%s:%s", Destination.GetCString(), Content.GetCString()),
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

			auto content     = AL::Move(matches[2]);

			message.Destination = AL::Move(matches[1]);

			if (!AL::Regex::Match(matches, "^(.*){(.+)$", content))
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
			AL::int16  latitude_hours,   longitude_hours;
			AL::uint16 latitude_minutes, longitude_minutes;
			AL::uint16 latitude_seconds, longitude_seconds;

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
			AL::Regex::MatchCollection matches;

			if (!AL::Regex::Match(matches, "^[!=](\\d\\d)(\\d\\d)\\.(\\d\\d)([NS])(.)(\\d\\d\\d)(\\d\\d)\\.(\\d\\d)([WE])(.)", packet.Content))
			{

				return false;
			}

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
			position.Comment.Clear(); // TODO: implement

			position.Altitude  = 0;
			position.Latitude  = latitude_hours + (latitude_minutes / 60.0f) + (latitude_seconds / 3600.0f);
			position.Longitude = longitude_hours + (longitude_minutes / 60.0f) + (longitude_seconds / 3600.0f);

			if (latitude_north_south == 'S') position.Latitude  = -position.Latitude;
			if (longitude_west_east  == 'W') position.Longitude = -position.Longitude;

			if (AL::Regex::Match(matches, "A=(-?)0*(\\d*)", packet.Content))
			{
				position.Altitude = AL::FromString<AL::uint16>(matches[2]);
				if (matches[1].GetLength() == 1) position.Altitude = -position.Altitude;
			}

			return true;
		}
	};

	namespace IS
	{
		class TcpClient
		{
			bool                    isBlocking  = false;
			bool                    isConnected = false;

			AL::Network::TcpSocket* lpSocket;
			AL::String              callsign;

		public:
			TcpClient()
			{
			}

			virtual ~TcpClient()
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

			auto& GetCallsign() const
			{
				return callsign;
			}

			// @throw AL::Exception
			void Connect(const AL::Network::IPEndPoint& remoteEP, const AL::String& callsign, AL::uint16 passcode, const AL::Collections::Array<AL::String>& filter)
			{
				AL_ASSERT(
					!IsConnected(),
					"TcpClient already connected"
				);

				lpSocket = new AL::Network::TcpSocket(
					remoteEP.Host.GetFamily()
				);

				this->callsign = callsign;

				// this only throws if the socket is already open
				lpSocket->SetBlocking(IsBlocking());

				try
				{
					lpSocket->Open();
				}
				catch (AL::Exception& exception)
				{
					delete lpSocket;

					throw AL::Exception(
						AL::Move(exception),
						"Error opening AL::Network::TcpSocket"
					);
				}

				try
				{
					if (!lpSocket->Connect(remoteEP))
					{

						throw AL::Exception(
							"Connection timed out"
						);
					}
				}
				catch (AL::Exception& exception)
				{
					lpSocket->Close();

					delete lpSocket;

					throw AL::Exception(
						AL::Move(exception),
						"Error connecting to %s",
						remoteEP.Host.ToString().GetCString(),
						remoteEP.Port
					);
				}

				isConnected = true;

				try
				{
					if (!Authenticate(callsign, passcode, filter))
					{

						throw AL::Exception(
							"Login failed"
						);
					}
				}
				catch (AL::Exception& exception)
				{
					isConnected = false;

					lpSocket->Close();

					delete lpSocket;

					throw AL::Exception(
						AL::Move(exception),
						"Error sending authentication"
					);
				}
			}

			void Disconnect()
			{
				if (IsConnected())
				{
					lpSocket->Close();

					delete lpSocket;

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
					"TcpClient not connected"
				);

				if (!ReadLine(line, false))
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
					"TcpClient not connected"
				);

				line = packet.Encode();

				try
				{
					WriteLine(
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

			// @throw AL::Exception
			void SetBlocking(bool value)
			{
				isBlocking = value;

				if (IsConnected())
				{
					lpSocket->SetBlocking(
						value
					);
				}
			}

		private:
			// @throw AL::Exception
			bool Authenticate(const AL::String& callsign, AL::uint16 passcode, const AL::Collections::Array<AL::String>& filter)
			{
				auto line = [&callsign, passcode, &filter]()
				{
					AL::StringBuilder sb;
					sb << "user " << callsign << " pass " << passcode << " vers " APRS_SOFTWARE_NAME " " APRS_SOFTWARE_VERSION;

					if (filter.GetSize() != 0)
					{
						sb << " filter";

						for (auto& f : filter)
							sb << ' ' << f;
					}

					return sb.ToString();
				}();

				try
				{
					WriteLine(
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

					while (ReadLine(line, true) || !IsBlocking())
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

		private:
			// @throw AL::Exception
			// @return false if would block
			bool ReadLine(AL::String& value, bool block)
			{
				AL_ASSERT(
					IsConnected(),
					"TcpClient not connected"
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
						if (!AL::Network::SocketExtensions::TryReceiveAll(*lpSocket, &buffer[1], sizeof(buffer[1]), numberOfBytesReceived))
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
						if (!AL::Network::SocketExtensions::ReceiveAll(*lpSocket, &buffer[1], sizeof(buffer[1]), numberOfBytesReceived))
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

				// AL::OS::Console::WriteLine("[RX] %s", value.GetCString());

				return true;
			}

			// @throw AL::Exception
			void WriteLine(const AL::String& value)
			{
				AL_ASSERT(
					IsConnected(),
					"TcpClient not connected"
				);

				auto valueLength = value.GetLength();

				if (valueLength > 510)
				{

					valueLength = 510;
				}

				// AL::OS::Console::WriteLine("[TX] %s", value.SubString(0, valueLength).GetCString());

				AL::size_t numberOfBytesSent;

				static constexpr char EOL[] = "\r\n";

				try
				{
					if (!AL::Network::SocketExtensions::SendAll(*lpSocket, value.GetCString(), valueLength, numberOfBytesSent) ||
						!AL::Network::SocketExtensions::SendAll(*lpSocket, EOL, sizeof(EOL) - 1, numberOfBytesSent))
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
			}
		};

		static AL::uint16 GeneratePasscode(const AL::String& callsign)
		{
			AL::uint16 value = 0x73E2;

			auto lpCallsign = callsign.GetCString();

			for (AL::size_t i = 0; i < callsign.GetLength(); i += 2)
			{
				if (*lpCallsign == '-')
				{

					break;
				}

				value ^= *(lpCallsign++) << 8;
				value ^= *(lpCallsign++);
			}

			return value & 0x7FFF;
		}
	}
}
