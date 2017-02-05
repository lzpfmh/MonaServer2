/*
This file is a part of MonaSolutions Copyright 2017
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

*/

#include "Mona/FlashStream.h"
#include "Mona/FLVReader.h"
#include "Mona/PacketWriter.h"
#include "Mona/MapWriter.h"
#include "Mona/Logs.h"


using namespace std;

namespace Mona {

FlashStream::FlashStream(UInt16 id, ServerAPI& api, Peer& peer, bool amf0) : amf0(amf0), id(id), api(api), peer(peer), _pPublication(NULL), _pSubscription(NULL), _bufferTime(0) {
	DEBUG("FlashStream ",id," created")
}

FlashStream::~FlashStream() {
	disengage(NULL);
	DEBUG("FlashStream ",id," deleted")
}

void FlashStream::flush() {
	if (_pPublication)
		_pPublication->flush();
	if (_pSubscription && _pSubscription->ejected())
		disengage((FlashWriter*)&_pSubscription->target);
}

void FlashStream::disengage(FlashWriter* pWriter) {
	// Stop the current  job
	if(_pPublication) {
		if (pWriter) {
			if(_pPublication->recording())
				pWriter->writeAMFStatus("NetStream.Record.Stop", _pPublication->name() + " recording stopped");
			pWriter->writeAMFStatus("NetStream.Unpublish.Success", _pPublication->name() + " is now unpublished");
		}
		 // do after writeAMFStatus because can delete the publication, so corrupt name reference
		api.unpublish(*_pPublication, peer);
		_pPublication = NULL;
	}
	if(_pSubscription) {
		const string& name(_pSubscription->name());
		if (pWriter) {
			switch (_pSubscription->ejected()) {
				case Subscription::EJECTED_TIMEOUT:
					pWriter->writeAMFStatusError("NetStream.Play.StreamNotFound", name + " not found");
					break;
				case Subscription::EJECTED_BANDWITDH:
					pWriter->writeAMFStatusError("NetStream.Play.InsufficientBW", "Insufficient bandwidth to play " + name);
					break;
				case Subscription::EJECTED_ERROR:
					pWriter->writeAMFStatusError("NetStream.Play.Failed", "Unknown error to play " + name);
					break;
				default:;
			}
			pWriter->writeAMFStatus("NetStream.Play.Stop", "Stopped playing " + name);
			onStop(id, *pWriter); // stream end
		}
		 // do after writeAMFStatus because can delete the publication, so corrupt publication name reference
		api.unsubscribe(peer, *_pSubscription);
		delete _pSubscription;
		_pSubscription = NULL;
	}
}

bool FlashStream::process(AMF::Type type, UInt32 time, const Packet& packet, FlashWriter& writer, Net::Stats& netStats) {

	writer.amf0 = amf0;

	UInt8 offset(0);

	// if exception, it closes the connection, and print an ERROR message
	switch(type) {

		case AMF::TYPE_AUDIO:
			audioHandler(time,packet);
			break;
		case AMF::TYPE_VIDEO:
			videoHandler(time,packet);
			break;

		case AMF::TYPE_DATA_AMF3:
			dataHandler(time, packet+1);
			break;
		case AMF::TYPE_DATA:
			dataHandler(time, packet);
			break;
		case AMF::TYPE_INVOCATION_AMF3:
			offset=1;
		case AMF::TYPE_INVOCATION: {
			string name;
			AMFReader reader(packet.data() + offset, packet.size()- offset);
			reader.readString(name);
			double number(0);
			reader.readNumber(number);
			writer.setCallbackHandle(number);
			reader.readNull();
			messageHandler(name,reader,writer, netStats);
			break;
		}
		case AMF::TYPE_RAW:
			rawHandler(BinaryReader(packet.data(), packet.size()).read16(), packet+2, writer);
			break;

		case AMF::TYPE_EMPTY:
			break;

		default:
			ERROR("Unpacking type '",String::Format<UInt8>("%02x",(UInt8)type),"' unknown");
	}

	writer.setCallbackHandle(0);
	return writer;
}


UInt32 FlashStream::bufferTime(UInt32 ms) {
	_bufferTime = ms;
	INFO("setBufferTime ", ms, "ms on stream ",id)
	if (_pSubscription)
		_pSubscription->setNumber("bufferTime", ms);
	return _bufferTime;
}

void FlashStream::messageHandler(const string& name, AMFReader& message, FlashWriter& writer, Net::Stats& netStats) {
	if (name == "play") {
		disengage(&writer);

		string stream;
		message.readString(stream);
		Exception ex;
		_pSubscription = new Subscription(writer);
		
		if (!api.subscribe(ex, stream, peer, *_pSubscription)) {
			if(ex.cast<Ex::Unfound>())
				writer.writeAMFStatusError("NetStream.Play.StreamNotFound", ex);
			else
				writer.writeAMFStatusError("NetStream.Play.Failed", ex);
			delete _pSubscription;
			_pSubscription = NULL;
			return;
		}

		onStart(id, writer); // stream begin
		writer.writeAMFStatus("NetStream.Play.Reset", "Playing and resetting " + stream); // for entiere playlist
		writer.writeAMFStatus("NetStream.Play.Start", "Started playing "+ stream); // for item
		AMFWriter& amf(writer.writeAMFData("|RtmpSampleAccess"));
		amf.writeBoolean(true); // audioSampleAccess
		amf.writeBoolean(true); // videoSampleAccess

		if (_bufferTime)
			_pSubscription->setNumber("bufferTime", _bufferTime);

		return;
	}
	
	if (name == "closeStream") {
		disengage(&writer);
		return;
	}
	
	if (name == "publish") {

		disengage(&writer);

		string type, stream;
		message.readString(stream);
		if (message.readString(type)) {
			if(String::ICompare(type, EXPAND("append"))==0) {
				stream += stream.find('?') == string::npos ? '?' : '&';
				stream += "append=true";
			} else if (String::ICompare(type, EXPAND("record")) == 0) {
				// if stream has no extension, add a FLV extension!
				size_t found = stream.find('?');
				size_t point = stream.find_last_of('.', found);
				if (point == string::npos) {
					if (found == string::npos)
						stream += ".flv";
					else
						stream.insert(found, ".flv");
				}
			}
		};

		Exception ex;
		_pPublication = api.publish(ex, peer, stream);
		if (_pPublication) {
			writer.writeAMFStatus("NetStream.Publish.Start", stream + " is now published");
			_track = _media = 0;
			if (_pPublication->recording()) {
				_pPublication->recorder()->onError = [this, &writer](const Exception& ex) {
					writer.writeAMFStatusError("NetStream.Record.Failed", ex);
					writer.writeAMFStatus("NetStream.Record.Stop", _pPublication->name() + " recording stopped");
					writer.flush();
				};
				writer.writeAMFStatus("NetStream.Record.Start", stream + " recording started");
			} else if (ex) {
				// recording pb!
				if (ex.cast<Ex::Unsupported>())
					writer.writeAMFStatusError("NetStream.Record.Failed", ex);
				else
					writer.writeAMFStatusError("NetStream.Record.NoAccess", ex);
			}
		} else
			writer.writeAMFStatusError("NetStream.Publish.BadName", ex);
		return;
	}
	
	if (_pSubscription) {

		if(name == "receiveAudio") {
			bool enable;
			if (message.readBoolean(enable)) {
				if (enable)
					_pSubscription->audios.enable();
				else
					_pSubscription->audios.disable();
			}
			return;
		}
		
		if (name == "receiveVideo") {
			bool enable;
			if (message.readBoolean(enable)) {
				if (enable)
					_pSubscription->videos.enable();
				else
					_pSubscription->videos.disable();
			}
			return;
		}
		
		if (name == "pause") {
			bool paused(true);
			message.readBoolean(paused);
			// TODO support pause for VOD
		
			if (paused) {
				// useless, client knows it when it calls NetStream::pause method
				// writer.writeAMFStatus("NetStream.Pause.Notify", _pListener->publication.name() + " paused");
			} else {
				UInt32 position;
				if (message.readNumber(position))
					_pSubscription->seek(position);
				onStart(id, writer); // stream begin
				// useless, client knows it when it calls NetStream::resume method
				//	writer.writeAMFStatus("NetStream.Unpause.Notify", _pListener->publication.name() + " resumed");
			}
			return;
		}
		
		if (name == "seek") {
			UInt32 position;
			if (message.readNumber(position)) {
				_pSubscription->seek(position);
				 // TODO support seek for VOD
				onStart(id, writer); // stream begin
				// useless, client knows it when it calls NetStream::seek method, and wait "NetStream.Seek.Complete" rather (raised by client side)
				// writer.writeAMFStatus("NetStream.Seek.Notify", _pListener->publication.name() + " seek operation");
			} else
				writer.writeAMFStatusError("NetStream.Seek.InvalidTime", _pSubscription->name() + string(" seek operation must pass in argument a milliseconds position time"));
			return;
		}
	}

	ERROR("Message '",name,"' unknown on stream ",id);
}

void FlashStream::dataHandler(UInt32 timestamp, const Packet& packet) {
	if (!packet)
		return; // to expect recursivity of dataHandler (see code below)

	if(!_pPublication) {
		ERROR("a data packet has been received on a no publishing stream ",id,", certainly a publication currently closing");
		return;
	}

	// fast checking, necessary AMF0 here!
	if (*packet.data() == AMF::AMF0_NULL) {
		// IF NetStream.send(Null,...) => manual publish

		AMFReader reader(packet.data(), packet.size());
		reader.readNull();
		PacketWriter content(packet, reader->current(), reader->available());
		bool isString(false);
		if (reader.read(DataReader::BYTES, content) || (isString=reader.read(DataReader::STRING, content))) {
			// If netStream.send(null, [tag as ByteArray/String] , data as ByteArray/String) => audio/video/data

			if (reader.nextType() == DataReader::BYTES && content) {
				// Has header!
				BinaryReader header(content.data(), content.size());
				if (isString) {
					// DATA
					_media = header.read8() << 24 | Media::Type::TYPE_DATA;
					_media |= header.read16()<<8;
				} else if (header.available() & 1) {
					// impair => VIDEO
					_video.unpack(header, false);
					_media = header.read16() << 8 | Media::Type::TYPE_VIDEO; // track + video
				} else {
					// pair => AUDIO
					_audio.unpack(header, false);
					_media = header.read16() << 8 | Media::Type::TYPE_AUDIO; // track + audio
				}
				reader.read(DataReader::BYTES, content);
			} // else use the old tag
			switch (_media & 0xFF) {
				case Media::Type::TYPE_AUDIO:
					_audio.time = timestamp;
					_pPublication->writeAudio(_media >> 8, _audio, content, peer.ping());
					break;
				case Media::Type::TYPE_VIDEO:
					_video.time = timestamp;
					_pPublication->writeVideo(_media >> 8, _video, content, peer.ping());
					break;
				default:
					_pPublication->writeData(_media >> 8, Media::Data::Type(_media >> 24), content, peer.ping());
			}
			return dataHandler(timestamp, packet + reader->position());
		} 


		if (reader.nextType() == DataReader::NIL) {
			// To allow a handler null with a bytearray or string following
			_pPublication->writeData(_track, Media::Data::TYPE_AMF, packet + reader->position(), peer.ping());
			return;
		}
		
	} else if (*packet.data() == AMF::AMF0_STRING && packet.size()>3 && *(packet.data() + 3) == '@' && *(packet.data() + 1) == 0) {

		switch (*(packet.data() + 2)) {
			case 15:
				if (memcmp(packet.data() + 3, EXPAND("@clearDataFrame")) != 0)
					break;
				return _pPublication->clear();
			case 13: {
				if (memcmp(packet.data() + 3, EXPAND("@setDataFrame")) != 0)
					break;
				// @setDataFrame
				AMFReader reader(packet.data(), packet.size());
				reader.next(); // @setDataFrame
				if (reader.nextType() == DataReader::STRING)
					reader.next(); // remove onMetaData
				_pPublication->clear();
				MapWriter<Parameters> writer(*_pPublication);
				reader.read(writer);
				return;
			}
			case 6: {
				if (memcmp(packet.data() + 3, EXPAND("@track")) != 0)
					break;
				// @track => allow to publish the standard netstream output for a selected track, and custom netstream output for one other 
				AMFReader reader(packet.data(), packet.size());
				reader.next(); // "@track"
				reader.readNumber(_track);
				return;
			}
		}
	}

	_pPublication->writeData(_track, Media::Data::TYPE_AMF, packet, peer.ping());
}

void FlashStream::rawHandler(UInt16 type, const Packet& packet, FlashWriter& writer) {
	if(type==0x0022) { // TODO Here we receive RTMFP flow sync signal, useless to support it!
		//TRACE("Sync ",id," : ",data.read32(),"/",data.read32());
		return;
	}
	ERROR("Raw message ",String::Format<UInt16>("%.4x",type)," unknown on stream ",id);
}

void FlashStream::audioHandler(UInt32 timestamp, const Packet& packet) {
	if(!_pPublication) {
		WARN("an audio packet has been received on a no publishing stream ",id,", certainly a publication currently closing");
		return;
	}

	_audio.time = timestamp;
	_pPublication->writeAudio(_track, _audio, packet + FLVReader::ReadMediaHeader(packet.data(), packet.size(), _audio), peer.ping());
}

void FlashStream::videoHandler(UInt32 timestamp, const Packet& packet) {
	if(!_pPublication) {
		WARN("a video packet has been received on a no publishing stream ",id,", certainly a publication currently closing");
		return;
	}

	_video.time = timestamp;
	UInt32 readen(FLVReader::ReadMediaHeader(packet.data(), packet.size(), _video));
	if (_video.codec == Media::Video::CODEC_H264 && _video.frame == Media::Video::FRAME_CONFIG) {
		shared<Buffer> pBuffer(new Buffer());
		readen += FLVReader::ReadAVCConfig(packet.data()+readen, packet.size()-readen, *pBuffer);
		_pPublication->writeVideo(_track, _video, Packet(pBuffer), peer.ping());
		if (packet.size() <= readen)
			return; //rest nothing
	}
	_pPublication->writeVideo(_track, _video, packet+readen, peer.ping());
}



} // namespace Mona