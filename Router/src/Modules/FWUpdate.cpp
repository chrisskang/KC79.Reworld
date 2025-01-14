#include "pch_App.h"
#include "FWUpdate.h"

namespace Modules {
	//----------
	FWUpdate::FWUpdate(shared_ptr<RS485> rs485)
		: rs485(rs485)
	{

	}

	//----------
	string
		FWUpdate::getTypeName() const
	{
		return "FWUpdate";
	}

	//----------
	void
		FWUpdate::init()
	{
		this->onPopulateInspector += [this](ofxCvGui::InspectArguments& args) {
			this->populateInspector(args);
		};
	}

	//----------
	void
		FWUpdate::update()
	{
		if (this->parameters.announce.enabled) {
			auto nextSend = this->announce.lastSend + chrono::milliseconds((int)(this->parameters.announce.period.get() * 1000.0f));
			if (nextSend <= chrono::system_clock::now()) {
				this->announceFirmware();
			}
		}
		
	}

	//----------
	void
		FWUpdate::populateInspector(ofxCvGui::InspectArguments& args)
	{
		auto inspector = args.inspector;
		inspector->addParameterGroup(this->parameters);
		inspector->addButton("Erase flash", [this]() {
			this->eraseFirmware();
			});
		inspector->addButton("Run application", [this]() {
			this->runApplication();
			});
	}

	//----------
	uint16_t calcCheckSum(uint8_t* data, uint16_t size)
	{
		uint16_t value = 0;
		auto wordPosition = (uint16_t*)data;
		auto dataEnd = (uint16_t*)(data + size);

		while (wordPosition < dataEnd) {
			value ^= *wordPosition++;
		}

		return value;
	}

	//----------
	// sequence : https://paper.dropbox.com/doc/KC79-Firmware-development-log--B~RnAsj4dL_fF81TgG7UhvbbAg-NaTWt2IkZT4ykJZeMERKP#:uid=689242674973595348642171&h2=Sequence
	void
		FWUpdate::uploadFirmware(const string& path)
	{
		// Read contents from file
		auto file = ofFile(path);
		auto fileBuffer = file.readToBuffer();
		file.close();
		auto size = fileBuffer.size();
		cout << size << endl;

		if (fileBuffer.size() == 0) {
			ofLogError("FWUpdate") << "Couldn't read file contents";
			return;
		}

		// 1. First announce it
		{
			ofxCvGui::Utils::drawProcessingNotice("Announcing firmware");
			for (int i = 0; i < 50; i++) {
				this->announceFirmware();
				ofSleepMillis(100);
			}
		}

		// 2. Erase the existing firmware (required before uploading)
		{
			ofxCvGui::Utils::drawProcessingNotice("Erasing flash");
			this->eraseFirmware();
			
			// Send announcements whilst we're waiting for erase to finish
			for (int i = 0; i < 50; i++) {
				this->announceFirmware();
				ofSleepMillis(100);
			}
		}

		// 3. Upload
		{
			ofxCvGui::Utils::drawProcessingNotice("Uploading");

			auto dataPosition = fileBuffer.getData();
			auto dataEnd = dataPosition + size;
			uint16_t packetIndex = 0;
			const auto frameSize = this->parameters.upload.frameSize;
			auto frameOffset = 0;

			while (dataPosition < dataEnd) {
				auto remainingSize = dataEnd - dataPosition;
				{
					ofxCvGui::Utils::drawProcessingNotice("Uploading : " + ofToString(remainingSize / 1000, 1) + "kB remaining");
				}

				
				if (remainingSize > frameSize) {
					for (int i = 0; i < this->parameters.upload.frameRepetitions.get(); i++) {
						uploadFirmwarePacket(frameOffset
							, dataPosition
							, FW_FRAME_SIZE);
						this_thread::sleep_for(chrono::milliseconds(this->parameters.upload.waitBetweenFrames));
					}
					
					dataPosition += frameSize;
					frameOffset += frameSize;
				}
				else {
					for (int i = 0; i < this->parameters.upload.frameRepetitions.get(); i++) {
						uploadFirmwarePacket(frameOffset
							, dataPosition
							, remainingSize);
						this_thread::sleep_for(chrono::milliseconds(this->parameters.upload.waitBetweenFrames));
					}
					dataPosition += remainingSize;
					frameOffset += remainingSize;
				}

			}
		}

		ofSleepMillis(1000);

		// 4. Disable announce
		{
			this->parameters.announce.enabled = false;
		}

		// 5. Run application
		{
			this->runApplication();
		}
	}

	//----------
	void
		FWUpdate::announceFirmware()
	{
		this->sendMagicWord('F', 'W');
	}

	//----------
	void
		FWUpdate::eraseFirmware()
	{
		this->sendMagicWord('E', 'R');
	}

	//----------
	void
		FWUpdate::runApplication()
	{
		this->sendMagicWord('R', 'U');
	}

	//----------
	void
		FWUpdate::sendMagicWord(char a, char b)
	{
		auto rs485 = this->rs485.lock();
		if (!rs485) {
			ofLogError("No RS485");
			return;
		}

		msgpack_sbuffer messageBuffer;
		msgpack_packer packer;
		msgpack_sbuffer_init(&messageBuffer);
		msgpack_packer_init(&packer
			, &messageBuffer
			, msgpack_sbuffer_write);

		msgpack_pack_array(&packer, 3);
		{
			// First element is target address
			msgpack_pack_fix_int8(&packer, -1);

			// Second element is source address
			msgpack_pack_fix_int8(&packer, 0);

			// Third element is the message body
			string magicWord;
			magicWord.resize(2);
			magicWord[0] = a;
			magicWord[1] = b;
			msgpack_pack_str(&packer, magicWord.size());
			msgpack_pack_str_body(&packer, magicWord.c_str(), magicWord.size());
		}

		rs485->transmit(messageBuffer);
		msgpack_sbuffer_destroy(&messageBuffer);

		this->announce.lastSend = chrono::system_clock::now();
	}

	//----------
	void
		FWUpdate::uploadFirmwarePacket(uint16_t frameOffset
			, const char* packetData
			, size_t packetSize)
	{
		auto rs485 = this->rs485.lock();
		if (!rs485) {
			ofLogError() << "No RS485";
		}

		// Prepend the data with checksum
		auto checksum = calcCheckSum((uint8_t*) packetData, packetSize);
		auto checksumBytes = (uint8_t*)&checksum;
		vector<uint8_t> packetBody;
		packetBody.push_back(checksumBytes[0]);
		packetBody.push_back(checksumBytes[1]);

		// Add the data to the packet
		packetBody.insert(packetBody.end()
			, packetData
			, packetData + packetSize);

		// Transmit via msgpack
		{
			msgpack_sbuffer messageBuffer;
			msgpack_packer packer;
			msgpack_sbuffer_init(&messageBuffer);
			msgpack_packer_init(&packer
				, &messageBuffer
				, msgpack_sbuffer_write);

			// The broadcast packet header
			msgpack_pack_array(&packer, 3);
			{
				// First element is target address
				msgpack_pack_fix_int8(&packer, -1);

				// Second element is source address
				msgpack_pack_fix_int8(&packer, 0);

				// Third element is the message body
				msgpack_pack_map(&packer, 1);
				{
					// Key is the packet index
					msgpack_pack_uint16(&packer, frameOffset);

					// Value is the data
					msgpack_pack_bin(&packer, packetBody.size());
					msgpack_pack_bin_body(&packer, packetBody.data(), packetBody.size());
				}
				
			}

			rs485->transmit(messageBuffer);
			msgpack_sbuffer_destroy(&messageBuffer);
		}
	}
}