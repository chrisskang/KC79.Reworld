#include "RS485.h"
#include <Arduino.h>

#include <msgpack.hpp>

#include "App.h"
#include "Log.h"
#include "Exception.h"

HardwareSerial serialRS485(PA3, PA2);
msgpack::COBSRWStream cobsStream(serialRS485);

#define PIN_DE PA1
namespace Modules {
	//---------
	RS485::RS485(App * app)
	: app(app)
	{

	}

	//---------
	void
	RS485::setup()
	{
		serialRS485.begin(115200);

		// Setup the DE pin
		pinMode(PIN_DE, OUTPUT);
		digitalWrite(PIN_DE, LOW);
	}

	//---------
	void
	RS485::update()
	{
		this->processIncoming();
	}

	//---------
	void
	RS485::processIncoming()
	{
		const auto ourID = this->app->id->get();

		// Skip any partial packets
		if(!cobsStream.isStartOfIncomingPacket()) {
			cobsStream.nextIncomingPacket();
		}

		while(cobsStream.isStartOfIncomingPacket() && cobsStream.available()) {
			bool needsReply = false;

			auto exception = this->processCOBSPacket();
			if(exception) {
				log(LogLevel::Error, exception.what());
			}
			else {
				log(LogLevel::Status, "RS485 Rx");
			}

			cobsStream.nextIncomingPacket();
		}
	}

	//---------
	Exception
	RS485::processCOBSPacket()
	{
		// Check it's a message for us
		bool weShouldProcess = false;
		{
			static const char * formatError = "Message format invalid";

			// We're expecting a 3-element array
			{
				size_t arraySize;
				if(!msgpack::readArraySize(cobsStream, arraySize)) {
					return Exception(formatError);
				};
				if(arraySize < 3) {
					return Exception(formatError);
				}
			}

			// First element is target address
			{
				int32_t targetAddress;
				if(!msgpack::readInt<int32_t>(cobsStream, targetAddress)) {
					return Exception(formatError);
				}

				// An address of -1 means it's addressed to all devices
				if(targetAddress == this->app->id->get() || targetAddress == -1) {
					weShouldProcess = true;
				}
			}

			// Second element is the source address (we ignore)
			{
				uint8_t _;
				if(!msgpack::readInt<uint8_t>(cobsStream, _)) {
					return Exception(formatError);
				}
			}

			if(weShouldProcess) {
				// We should process this packet, next is the value of the outer map

				// If it's a Nil, then it's a poll
				if(msgpack::nextDataTypeIs(cobsStream, msgpack::DataType::Nil)) {
					this->transmitOutbox();
				}
				else {
					auto success = app->processIncoming(cobsStream);
					if(!success) {
						return Exception("Failed to process packet");
					}
				}
			}
		}

		return Exception::None();
	}

	//---------
	void
	RS485::beginTransmission()
	{
		
	 	digitalWrite(PIN_DE, HIGH);
	}

	//---------
	void
	RS485::endTransmission()
	{
		cobsStream.flush();
		digitalWrite(PIN_DE, LOW);
	}

	//---------
	void
	RS485::transmitOutbox()
	{
		this->beginTransmission();

		msgpack::writeArraySize4(cobsStream, 3);
		{
			// First element is target address (0 = Host)
			msgpack::writeIntU7(cobsStream, 0);

			// Second element is our address
			msgpack::writeIntU7(cobsStream, this->app->id->get());

			// Third element is message to send
			{
				// Value is the data to transmit
				msgpack::writeArraySize4(cobsStream, 0);
			}
		}
		this->endTransmission();
	}
}