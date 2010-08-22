//
//   Copyright (C) 2007, 2008, 2009, 2010 Free Software Foundation, Inc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <unistd.h>

#include <boost/lexical_cast.hpp>

// Replace!!
#include <sys/times.h>
#include <netinet/in.h>

#include "RTMP.h"
#include "log.h"
#include "AMF.h"
#include "GnashAlgorithm.h"
#include "URL.h"
#include "ClockTime.h"

namespace gnash {
namespace rtmp {

namespace {

    bool sendBytesReceived(RTMP* r);

    // Not sure we ever want to do this.
    bool sendServerBW(RTMP& r);

    void handleMetadata(RTMP& r, const boost::uint8_t *payload,
            unsigned int len);
    void handleChangeChunkSize(RTMP& r, const RTMPPacket& packet);
    void handleControl(RTMP& r, const RTMPPacket& packet);
    void handleServerBW(RTMP& r, const RTMPPacket& packet);
    void handleClientBW(RTMP& r, const RTMPPacket& packet);
    
    void setupInvokePacket(RTMPPacket& packet);
    boost::uint32_t getUptime();

    boost::int32_t decodeInt32LE(const boost::uint8_t* c);
    int encodeInt32LE(boost::uint8_t *output, int nVal);
    unsigned int decodeInt24(const boost::uint8_t* c);
    boost::uint8_t* encodeInt16(boost::uint8_t *output, boost::uint8_t *outend,
            short nVal);
    boost::uint8_t* encodeInt24(boost::uint8_t *output, boost::uint8_t *outend,
            int nVal);
    boost::uint8_t* encodeInt32(boost::uint8_t *output, boost::uint8_t *outend,
            int nVal);

    static const int packetSize[] = { 12, 8, 4, 1 };
 
}

namespace {

/// A random generator for generating the signature.
//
/// TODO: do this properly (it's currently not very random).
struct RandomByte
{
    bool operator()() const {
        return std::rand() % 256;
    }
};

}

/// A utility functor for carrying out the handshake.
class HandShaker
{
public:

    static const int sigSize = 1536;

    HandShaker(Socket& s);

    /// Calls the next stage in the handshake process.
    void call();

    bool success() const {
        return _complete;
    }

    bool error() const {
        return _error || _socket.bad();
    }

private:

    /// These are the stages of the handshake.
    //
    /// If the socket is not ready, they will return false. If the socket
    /// is in error, they will set _error.
    bool stage0();
    bool stage1();
    bool stage2();
    bool stage3();

    Socket _socket;
    std::vector<boost::uint8_t> _sendBuf;
    std::vector<boost::uint8_t> _recvBuf;
    bool _error;
    bool _complete;
    size_t _stage;
};

RTMPPacket::RTMPPacket(size_t reserve)
    :
    header(),
    buffer(new SimpleBuffer(reserve + RTMPHeader::headerSize)),
    bytesRead(0)
{
    // This is space for the header be filled in later.
    buffer->resize(RTMPHeader::headerSize);
}

RTMPPacket::RTMPPacket(const RTMPPacket& other)
    :
    header(other.header),
    buffer(other.buffer)
{}

const size_t RTMPHeader::headerSize;

RTMP::RTMP()
    :
    _inChunkSize(RTMP_DEFAULT_CHUNKSIZE),
    m_mediaChannel(0),
    m_nClientBW2(2),
    _bytesIn(0),
    _bytesInSent(0),
    _serverBandwidth(2500000),
    _bandwidth(2500000),
    _outChunkSize(RTMP_DEFAULT_CHUNKSIZE),
    _connected(false),
    _error(false)
{
}

RTMP::~RTMP()
{
}

bool
RTMP::hasPacket(ChannelType t, size_t channel) const
{
    const ChannelSet& set = (t == CHANNELS_OUT) ? _outChannels : _inChannels;
    return set.find(channel) != set.end();
}

RTMPPacket&
RTMP::getPacket(ChannelType t, size_t channel)
{
    ChannelSet& set = (t == CHANNELS_OUT) ? _outChannels : _inChannels;
    return set[channel];
}

RTMPPacket&
RTMP::storePacket(ChannelType t, size_t channel, const RTMPPacket& p)
{
    ChannelSet& set = (t == CHANNELS_OUT) ? _outChannels : _inChannels;
    RTMPPacket& stored = set[channel];
    stored = p;
    return stored;
}

void
RTMP::setBufferTime(size_t size, int streamID)
{
    sendCtrl(*this, CONTROL_BUFFER_TIME, streamID, size);
}

void
RTMP::call(const SimpleBuffer& amf)
{
    RTMPPacket p(amf.size());
    setupInvokePacket(p);
    
    // Copy the data.
    p.buffer->append(amf.data(), amf.size());
    sendPacket(p);
}

bool
RTMP::connect(const URL& url)
{
    log_debug("Connecting to %s", url.str());

    const std::string& hostname = url.hostname();
    const std::string& p = url.port();

    // Default port.
    boost::uint16_t port = 1935;
    if (!p.empty()) {
        try {
            port = boost::lexical_cast<boost::uint16_t>(p);
        }
        catch (boost::bad_lexical_cast&) {}
    }

    // Basic connection attempt.
    if (!_socket.connect(hostname, port)) {
        log_error("Initial connection failed");
        return false;
    }
    
    _handShaker.reset(new HandShaker(_socket));

    // Start handshake attempt immediately.
    _handShaker->call();

    return true;
}

void
RTMP::update()
{
    if (!connected()) {
        _handShaker->call();
        if (_handShaker->error()) {
            _error = true;
        }
        if (!_handShaker->success()) return;
        _connected = true;
    }
    
    const size_t reads = 10;

    for (size_t i = 0; i < reads; ++i) {

        /// No need to continue reading (though it should do no harm).
        if (error()) return;

        RTMPPacket p;

        // If we haven't finished reading a packet, retrieve it; otherwise
        // use an empty one.
        if (_incompletePacket.get()) {
            log_debug("Doing incomplete packet");
            p = *_incompletePacket;
            _incompletePacket.reset();
        }
        else {
            if (!readPacketHeader(p)) continue;
        }

        // Get the payload if possible.
        if (hasPayload(p) && !readPacketPayload(p)) {
            // If the payload is not completely readable, store it and
            // continue.
            _incompletePacket.reset(new RTMPPacket(p));
            continue;
        }
        
        // Store a copy of the packet for later additions and as a reference for
        // future sends.
        RTMPPacket& stored = storePacket(CHANNELS_IN, p.header.channel, p);
      
        // If the packet is complete, the stored packet no longer needs to
        // keep the data alive.
        if (isReady(p)) {
            clearPayload(stored);
            handlePacket(p);
            return;
        }
    }
}

void
RTMP::handlePacket(const RTMPPacket& packet)
{
    const PacketType t = packet.header.packetType;

    log_debug("Received %s", t);

    switch (t) {

        case PACKET_TYPE_CHUNK_SIZE:
            handleChangeChunkSize(*this, packet);
            break;
    
        case PACKET_TYPE_BYTES_READ:
            break;
    
        case PACKET_TYPE_CONTROL:
            handleControl(*this, packet);
            break;

        case PACKET_TYPE_SERVERBW:
            handleServerBW(*this, packet);
            break;

        case PACKET_TYPE_CLIENTBW:
            handleClientBW(*this, packet);
            break;
    
        case PACKET_TYPE_AUDIO:
            if (!m_mediaChannel) m_mediaChannel = packet.header.channel;
            break;

        case PACKET_TYPE_VIDEO:
            if (!m_mediaChannel) m_mediaChannel = packet.header.channel;
            break;

        case PACKET_TYPE_FLEX_STREAM_SEND:
            LOG_ONCE(log_unimpl("unsupported packet %s received"));
            break;

        case PACKET_TYPE_FLEX_SHARED_OBJECT:
            LOG_ONCE(log_unimpl("unsupported packet %s received"));
            break;

        case PACKET_TYPE_FLEX_MESSAGE:
        {
            LOG_ONCE(log_unimpl("partially supported packet %s received"));
            _messageQueue.push_back(packet.buffer);    
            break;
        }
    
        case PACKET_TYPE_METADATA:
            handleMetadata(*this, payloadData(packet), payloadSize(packet));
            break;

        case PACKET_TYPE_SHARED_OBJECT:
            LOG_ONCE(log_unimpl("packet %s received"));
            break;

        case PACKET_TYPE_INVOKE:
            _messageQueue.push_back(packet.buffer);
            break;

        case PACKET_TYPE_FLV:
            _flvQueue.push_back(packet.buffer);
            break;
    
        default:
            log_error("Unknown packet %s received", t);
    
    }
  
}

int
RTMP::readSocket(boost::uint8_t* buffer, int n)
{

    assert(n >= 0);

    const std::streamsize bytesRead = _socket.read(buffer, n);
    
    if (_socket.bad()) {
        _error = true;
        return 0;
    }

    if (!bytesRead) return 0;

    _bytesIn += bytesRead;

    // Report bytes recieved every time we reach half the bandwidth.
    // Doesn't seem very likely to be the way the pp does it.
    if (_bytesIn > _bytesInSent + _bandwidth / 2) {
        sendBytesReceived(this);
        log_debug("Sent bytes received");
    }

    buffer += bytesRead;
    return bytesRead;
}

void
RTMP::play(const SimpleBuffer& buf, int streamID)
{
    RTMPPacket packet(buf.size());
  
    packet.header.channel = CHANNEL_VIDEO;
    packet.header.packetType = PACKET_TYPE_INVOKE;
  
    packet.header._streamID = streamID;
  
    packet.buffer->append(buf.data(), buf.size());
    sendPacket(packet);
}

/// Fills a pre-existent RTMPPacket with information.
//
/// This is either read entirely from incoming data, or copied from a
/// previous packet in the same channel. This happens when the header type
/// is less than RTMP_PACKET_SIZE_LARGE.
//
/// It seems as if new packets can add to the data of old ones if they have
/// a minimal, small header.
bool
RTMP::readPacketHeader(RTMPPacket& packet)
{
      
    RTMPHeader& hr = packet.header;

    boost::uint8_t hbuf[RTMPHeader::headerSize] = { 0 };
    boost::uint8_t* header = hbuf;
  
    // The first read may fail, but otherwise we expect a complete header.
    if (readSocket(hbuf, 1) == 0) {
        return false;
    }

    //log_debug("Packet is %s", boost::io::group(std::hex, (unsigned)hbuf[0]));

    const int htype = ((hbuf[0] & 0xc0) >> 6);
    //log_debug("Thingy whatsit (packet size type): %s", htype);

    const int channel = (hbuf[0] & 0x3f);
    //log_debug("Channel: %s", channel);

    hr.headerType = static_cast<PacketSize>(htype);
    hr.channel = channel;
    ++header;

    if (hr.channel == 0) {
        if (readSocket(&hbuf[1], 1) != 1) {
          log_error("failed to read RTMP packet header 2nd byte");
          return false;
        }
        hr.channel = hbuf[1] + 64;
        ++header;
    }
    else if (hr.channel == 1) {
        if (readSocket(&hbuf[1], 2) != 2) {
            log_error("Failed to read RTMP packet header 3nd byte");
             return false;
        }
      
        const boost::uint32_t tmp = (hbuf[2] << 8) + hbuf[1];
        hr.channel = tmp + 64;
        log_debug( "%s, channel: %0x", __FUNCTION__, hr.channel);
        header += 2;
    }
  
    // This is the size in bytes of the packet header according to the
    // type.
    int nSize = packetSize[htype];

    /// If we didn't receive a large header, the timestamp is relative
    if (htype != RTMP_PACKET_SIZE_LARGE) {

        if (!hasPacket(CHANNELS_IN, hr.channel)) {
            log_error("Incomplete packet received on channel %s", channel);
            return false;
        }

        // For all other header types, copy values from the last message of
        // this channel. This includes any payload data from incomplete
        // messages. 
        packet = getPacket(CHANNELS_IN, hr.channel);
    }
  
    --nSize;
  
    if (nSize > 0 && readSocket(header, nSize) != nSize) {
        log_error( "Failed to read RTMP packet header. type: %s",
                static_cast<unsigned>(hbuf[0]));
        return false;
    }

    // nSize is predicted size - 1. Add what we've read already.
    int hSize = nSize + (header - hbuf);

    if (nSize >= 3) {

        const boost::uint32_t timestamp = decodeInt24(header);

        // Make our packet timestamp absolute. If the value is 0xffffff,
        // the absolute value comes later.
        if (timestamp != 0xffffff) {
            if (htype != RTMP_PACKET_SIZE_LARGE) {
                packet.header._timestamp += timestamp;
            }
            else {
                packet.header._timestamp = timestamp;
            }
        }

        // Have at least a different size payload from the last packet.
        if (nSize >= 6) {

            // We do this in case there was an incomplete packet in the
            // channel already.
            clearPayload(packet);
            hr.dataSize = decodeInt24(header + 3);

            // More than six: read packet type
            if (nSize > 6) {
                hr.packetType = static_cast<PacketType>(header[6]);
     
                // Large packets have a streamID.
                if (nSize == 11) {
                    hr._streamID = decodeInt32LE(header + 7);
                }
            }
        }
    }

    if (hr._timestamp == 0xffffff) {
      if (readSocket(header+nSize, 4) != 4) {
              log_error( "%s, failed to read extended timestamp",
              __FUNCTION__);
              return false;
            }
          hr._timestamp = amf::readNetworkLong(header+nSize);
          hSize += 4;
        
    }
        
    const size_t bufSize = hr.dataSize + RTMPHeader::headerSize;

    // If the packet does not have a payload, it was a complete packet stored in
    // the channel for reference. This is the only case when a packet should
    // exist but have no payload. We re-allocate in this case.
    if (!hasPayload(packet)) {
        packet.buffer.reset(new SimpleBuffer(bufSize));

        // Why do this again? In case it was copied from the old packet?
        hr.headerType = static_cast<PacketSize>(htype);
    }
    
    // Resize anyway. If it's different from what it was before, we should
    // already have cleared it.
    packet.buffer->resize(bufSize);
    return true;
}

bool
RTMP::readPacketPayload(RTMPPacket& packet)
{
    RTMPHeader& hr = packet.header;

    const size_t bytesRead = packet.bytesRead;

    const int nToRead = hr.dataSize - bytesRead;

    const int nChunk = std::min<int>(nToRead, _inChunkSize);
    assert(nChunk >= 0);

    // This is fine. We'll keep trying to read this payload until there
    // is enough data.
    if (readSocket(payloadData(packet) + bytesRead, nChunk) != nChunk) {
        return false;
    }

    packet.bytesRead += nChunk;
        
    return true;
}

bool
RTMP::handShake()
{

    /// It is a size type, but our socket functions return int.
    const int sigSize = 1536;

    boost::uint8_t clientbuf[sigSize + 1];
    boost::uint8_t* ourSig = clientbuf + 1;

    // Not encrypted
    clientbuf[0] = 0x03;
    
    // TODO: do this properly.
    boost::uint32_t uptime = htonl(getUptime());
    std::memcpy(ourSig, &uptime, 4);

    std::fill_n(ourSig + 4, 4, 0);

    // Generate 1536 random bytes.
    std::generate(ourSig + 8, ourSig + sigSize, RandomByte());

    // Send it to server.
    if (_socket.write(clientbuf, sigSize + 1) != sigSize + 1) {
        return false;
    }

    // Expect the same byte as we sent.
    boost::uint8_t type;
    if (readSocket(&type, 1) != 1) {
        return false;
    }

    log_debug( "%s: Type Answer   : %02X", __FUNCTION__, (int)type);

    if (type != clientbuf[0]) {
        log_error( "%s: Type mismatch: client sent %d, server answered %d",
            __FUNCTION__, clientbuf[0], type);
    }
    
    boost::uint8_t serverSig[sigSize];

    // Read from server.
    if (readSocket(serverSig, sigSize) != sigSize) {
        return false;
    }

    // decode server response
    boost::uint32_t suptime;

    memcpy(&suptime, serverSig, 4);
    suptime = ntohl(suptime);

    log_debug("Server Uptime : %d", suptime);
    log_debug("FMS Version   : %d.%d.%d.%d",
            +serverSig[4], +serverSig[5], +serverSig[6], +serverSig[7]);

    // Send what we received from server.
    if (_socket.write(serverSig, sigSize) != sigSize) {
        return false;
    }

    // Expect it back again.
    if (readSocket(serverSig, sigSize) != sigSize) {
        return false;
    }

    const bool match = std::equal(serverSig, serverSig + arraySize(serverSig),
                                  ourSig);

    if (!match) {
        log_error( "Signatures do not match during handshake!");
    }
    return true;
}


bool
RTMP::sendPacket(RTMPPacket& packet)
{
    // Set the data size of the packet to send.
    RTMPHeader& hr = packet.header;

    hr.dataSize = payloadSize(packet);

    // This is the timestamp for our message.
    const boost::uint32_t uptime = getUptime();
    
    // Look at the previous packet on the channel.
    bool prev = hasPacket(CHANNELS_OUT, hr.channel);

    // The packet shall be large if it contains an absolute timestamp.
    //      * This is necessary if there is no previous packet, or if the
    //        timestamp is smaller than the last packet.
    // Else it shall be medium if data size and packet type are the same
    // It shall be small if ...
    // It shall be minimal if it is exactly the same as its predecessor.

    // All packets should start off as large. They will stay large if there
    // is no previous packet.
    assert(hr.headerType == RTMP_PACKET_SIZE_LARGE);

    if (!prev) {
        hr._timestamp = uptime;
    }
    else {

        const RTMPPacket& prevPacket = getPacket(CHANNELS_OUT, hr.channel);
        const RTMPHeader& oldh = prevPacket.header;
        const boost::uint32_t prevTimestamp = oldh._timestamp;

        // If this timestamp is later than the other and the difference fits
        // in 3 bytes, encode a relative one.
        if (uptime >= oldh._timestamp && uptime - prevTimestamp < 0xffffff) {
            //log_debug("Shrinking to medium");
            hr.headerType = RTMP_PACKET_SIZE_MEDIUM;
            hr._timestamp = uptime - prevTimestamp;

            // It can be still smaller if the data size is the same.
            if (oldh.dataSize == hr.dataSize &&
                    oldh.packetType == hr.packetType) {
                //log_debug("Shrinking to small");
                hr.headerType = RTMP_PACKET_SIZE_SMALL;
                // If there is no timestamp difference, the minimum size
                // is possible.
                if (hr._timestamp == 0) {
                    //log_debug("Shrinking to minimum");
                    hr.headerType = RTMP_PACKET_SIZE_MINIMUM;
                }
            }
        }
        else {
            // Otherwise we need an absolute one, so a large header.
            hr.headerType = RTMP_PACKET_SIZE_LARGE;
            hr._timestamp = uptime;
        }
    }

    assert (hr.headerType < 4);
  
    int nSize = packetSize[hr.headerType];
  
    int hSize = nSize;
    boost::uint8_t* header;
    boost::uint8_t* hptr;
    boost::uint8_t* hend;
    boost::uint8_t c;

    // If there is a payload, the same buffer is used to write the header.
    // Otherwise a separate buffer is used. But as we write them separately
    // anyway, why do we do that?

    // Work out where the beginning of the header is.
    header = payloadData(packet) - nSize;
    hend = payloadData(packet);
  
    // The header size includes only a single channel/type. If we need more,
    // they have to be added on.
    const int channelSize = hr.channel > 319 ? 3 : hr.channel > 63 ? 1 : 0;
    header -= channelSize;
    hSize += channelSize;

    /// Add space for absolute timestamp if necessary.
    if (hr.headerType == RTMP_PACKET_SIZE_LARGE && hr._timestamp >= 0xffffff) {
        header -= 4;
        hSize += 4;
    }

    hptr = header;
    c = hr.headerType << 6;
    switch (channelSize) {
        case 0:
            c |= hr.channel;
            break;
        case 1:
            break;
        case 2:
            c |= 1;
            break;
    }
    *hptr++ = c;

    if (channelSize) {
        const int tmp = hr.channel - 64;
        *hptr++ = tmp & 0xff;
        if (channelSize == 2) *hptr++ = tmp >> 8;
    }

    if (hr.headerType == RTMP_PACKET_SIZE_LARGE && hr._timestamp >= 0xffffff) {
        // Signify that the extended timestamp field is present.
        const boost::uint32_t t = 0xffffff;
        hptr = encodeInt24(hptr, hend, t);
    }
    else if (hr.headerType != RTMP_PACKET_SIZE_MINIMUM) { 
        // Write absolute or relative timestamp. Only minimal packets have
        // no timestamp.
        hptr = encodeInt24(hptr, hend, hr._timestamp);
    }

    /// Encode dataSize and packet type for medium packets.
    if (nSize > 4) {
        hptr = encodeInt24(hptr, hend, hr.dataSize);
        *hptr++ = hr.packetType;
    }

    /// Encode streamID for large packets.
    if (hr.headerType == RTMP_PACKET_SIZE_LARGE) {
        hptr += encodeInt32LE(hptr, hr._streamID);
    }

    // Encode extended absolute timestamp if needed.
    if (hr.headerType == RTMP_PACKET_SIZE_LARGE && hr._timestamp >= 0xffffff) {
        hptr += encodeInt32LE(hptr, hr._timestamp);
    }

    nSize = hr.dataSize;
    boost::uint8_t *buffer = payloadData(packet);
    int nChunkSize = _outChunkSize;

    std::string hx = hexify(header, payloadEnd(packet) - header, false);

    while (nSize + hSize) {

        if (nSize < nChunkSize) nChunkSize = nSize;

        // First write header.
        if (header) {
            const int chunk = nChunkSize + hSize;
            if (_socket.write(header, chunk) != chunk) {
                return false;
            }
            header = NULL;
            hSize = 0;
        }
      
        else {
            // Then write data.
            if (_socket.write(buffer, nChunkSize) != nChunkSize) {
                return false;
          }
        
        }
  
        nSize -= nChunkSize;
        buffer += nChunkSize;
 
        if (nSize > 0) {
            header = buffer - 1;
            hSize = 1;
            if (channelSize) {
                header -= channelSize;
                hSize += channelSize;
            }

            *header = (0xc0 | c);
            if (channelSize) {
                int tmp = hr.channel - 64;
                header[1] = tmp & 0xff;
                if (channelSize == 2) header[2] = tmp >> 8;
            }
        }
    }

    /* we invoked a remote method */
    if (hr.packetType == PACKET_TYPE_INVOKE) {
        assert(payloadData(packet)[0] == amf::STRING_AMF0);
        const boost::uint8_t* pos = payloadData(packet) + 1;
        const boost::uint8_t* end = payloadEnd(packet);
        const std::string& s = amf::readString(pos, end);
        log_debug( "Calling remote method %s", s);
    }

    RTMPPacket& storedpacket = storePacket(CHANNELS_OUT, hr.channel, packet);

    // Make it absolute for the next delta.
    storedpacket.header._timestamp = uptime;

    return true;
}

void
RTMP::close()
{
    _socket.close();
    _inChannels.clear();
    _outChannels.clear();
    _inChunkSize = RTMP_DEFAULT_CHUNKSIZE;
    _outChunkSize = RTMP_DEFAULT_CHUNKSIZE;
    _bytesIn = 0;
    _bytesInSent = 0;
    _bandwidth = 2500000;
    m_nClientBW2 = 2;
    _serverBandwidth = 2500000;
}


/////////////////////////////////////
/// HandShaker implementation
/////////////////////////////////////

HandShaker::HandShaker(Socket& s)
    :
    _socket(s),
    _sendBuf(sigSize + 1),
    _recvBuf(sigSize + 1),
    _error(false),
    _complete(false),
    _stage(0)
{
    // Not encrypted
    _sendBuf[0] = 0x03;
    
    // TODO: do this properly.
    boost::uint32_t uptime = htonl(getUptime());

    boost::uint8_t* ourSig = &_sendBuf.front() + 1;
    std::memcpy(ourSig, &uptime, 4);
    std::fill_n(ourSig + 4, 4, 0);

    // Generate 1536 random bytes.
    std::generate(ourSig + 8, ourSig + sigSize, RandomByte());

}


/// Calls the next stage in the handshake process.
void
HandShaker::call()
{
    if (error() || !_socket.connected()) return;

    switch (_stage) {
        case 0:
            if (!stage0()) return;
            _stage = 1;
        case 1:
            if (!stage1()) return;
            _stage = 2;
        case 2:
            if (!stage2()) return;
            _stage = 3;
        case 3:
            if (!stage3()) return;
            log_debug("Handshake completed");
            _complete = true;
    }
}

bool
HandShaker::stage0()
{
    std::streamsize sent = _socket.write(&_sendBuf.front(), sigSize + 1);

    // This should probably not happen, but we can try again. An error will
    // be signalled later if the socket is no longer usable.
    if (!sent) {
        log_error("Stage 1 socket not ready. This should not happen.");
        return false;
    }

    /// If we sent the wrong amount of data, we can't recover.
    if (sent != sigSize + 1) {
        log_error("Could not send stage 1 data");
        _error = true;
        return false;
    }
    return true;
}

bool
HandShaker::stage1()
{

    std::streamsize read = _socket.read(&_recvBuf.front(), sigSize + 1);

    if (!read) {
        // If we receive nothing, wait until the next try.
        return false;
    }

    // The read should never return anything but 0 or what we asked for.
    assert (read == sigSize + 1);

    if (_recvBuf[0] != _sendBuf[0]) {
        log_error( "Type mismatch: client sent %d, server answered %d",
	        _recvBuf[0], _sendBuf[0]);
    }
    
    const boost::uint8_t* serverSig = &_recvBuf.front() + 1;

    // decode server response
    boost::uint32_t suptime;
    std::memcpy(&suptime, serverSig, 4);
    suptime = ntohl(suptime);

    log_debug("Server Uptime : %d", suptime);
    log_debug("FMS Version   : %d.%d.%d.%d",
            +serverSig[4], +serverSig[5], +serverSig[6], +serverSig[7]);

    return true;
}

bool
HandShaker::stage2()
{
    
    std::streamsize sent = _socket.write(&_recvBuf.front() + 1, sigSize);
    
    // This should probably not happen.
    if (!sent) return false;

    if (sent != sigSize) {
        log_error("Could not send complete signature.");
        _error = true;
        return false;
    }

    return true;
}

bool
HandShaker::stage3()
{

    // Expect it back again.
    std::streamsize got = _socket.read(&_recvBuf.front(), sigSize);
   
    if (!got) return false;
    
    assert (got == sigSize);

    const boost::uint8_t* serverSig = &_recvBuf.front();
    const boost::uint8_t* ourSig = &_sendBuf.front() + 1;

    const bool match = std::equal(serverSig, serverSig + sigSize, ourSig);

    // Should we set an error here?
    if (!match) {
        log_error( "Signatures do not match during handshake!");
    }
    return true;
}

/// The type of Ping packet is 0x4 and contains two mandatory parameters
/// and two optional parameters. The first parameter is
/// the type of Ping and in short integer. The second parameter is the
/// target of the ping. As Ping is always sent in Channel 2
/// (control channel) and the target object in RTMP header is always 0 whicj
/// means the Connection object, it's necessary to put an extra parameter
/// to indicate the exact target object the Ping is sent to. The second
/// parameter takes this responsibility. The value has the same meaning
/// as the target object field in RTMP header. (The second value could also
/// be used as other purposes, like RTT Ping/Pong. It is used as the
/// timestamp.) The third and fourth parameters are optional and could be
/// looked upon as the parameter of the Ping packet. 
bool
sendCtrl(RTMP& r, ControlType t, unsigned int nObject, unsigned int nTime)
{
    log_debug( "Sending control type %s %s", +t, t);
  
    RTMPPacket packet(256);
  
    packet.header.channel = CHANNEL_CONTROL1;
    packet.header.headerType = RTMP_PACKET_SIZE_LARGE;
    packet.header.packetType = PACKET_TYPE_CONTROL;
      
    // type 3 is the buffer time and requires all 3 parameters.
    // all in all 10 bytes.
    int nSize = (t == CONTROL_BUFFER_TIME ? 10 : 6);
    if (t == CONTROL_RESPOND_VERIFY) nSize = 44;
    
    SimpleBuffer& buf = *packet.buffer;
  
    buf.appendNetworkShort(t);
  
    if (t == CONTROL_RESPOND_VERIFY) { }
    else {
        if (nSize > 2) buf.appendNetworkLong(nObject);
        if (nSize > 6) buf.appendNetworkLong(nTime);
    }
    return r.sendPacket(packet);
}

namespace {

/// Send the server bandwidth.
//
/// Why would we want to send this?
bool
sendServerBW(RTMP& r)
{
    RTMPPacket packet(4);
  
    packet.header.channel = CHANNEL_CONTROL1;
    packet.header.packetType = PACKET_TYPE_SERVERBW;
  
    SimpleBuffer& buf = *packet.buffer;
  
    buf.appendNetworkLong(r.serverBandwidth());
    return r.sendPacket(packet);
}


bool
sendBytesReceived(RTMP* r)
{
    RTMPPacket packet(4);
  
    packet.header.channel = CHANNEL_CONTROL1;
    packet.header.packetType = PACKET_TYPE_BYTES_READ;
  
    SimpleBuffer& buf = *packet.buffer;
  
    buf.appendNetworkLong(r->_bytesIn);
    r->_bytesInSent = r->_bytesIn;
  
    return r->sendPacket(packet);
}


void
handleMetadata(RTMP& /*r*/, const boost::uint8_t* /* payload*/, 
        unsigned int /*len*/)
{
    return;
}

void
handleChangeChunkSize(RTMP& r, const RTMPPacket& packet)
{
    if (payloadSize(packet) >= 4) {
        r._inChunkSize = amf::readNetworkLong(payloadData(packet));
        log_debug( "Changed chunk size to %d", r._inChunkSize);
    }
}

void
handleControl(RTMP& r, const RTMPPacket& packet)
{

    const size_t size = payloadSize(packet);

    if (size < 2) {
        log_error("Control packet too short");
        return;
    }
    
    const ControlType t = 
        static_cast<ControlType>(amf::readNetworkShort(payloadData(packet)));
    
    if (size < 6) {
        log_error("Control packet (%s) data too short", t);
        return;
    }
    
    const int arg = amf::readNetworkLong(payloadData(packet) + 2);
    log_debug( "Received control packet %s with argument %s", t, arg);
  
    switch (t)
    {
  
        case CONTROL_CLEAR_STREAM:
            // TODO: handle this.
            break;
  
        case CONTROL_CLEAR_BUFFER:
            // TODO: handle this.
            break;
  
        case CONTROL_STREAM_DRY:
            break;
  
        case CONTROL_RESET_STREAM:
            log_debug("Stream is recorded: %s", arg);
            break;
  
        case CONTROL_PING:
            sendCtrl(r, CONTROL_PONG, arg, 0);
            break;
  
        case CONTROL_BUFFER_EMPTY:
            // TODO: handle.
            break;
  
        case CONTROL_BUFFER_READY:
            // TODO: handle
            break;
  
        default:
            log_error("Received unknown or unhandled control %s", t);
            break;
    }
  
}

void
handleServerBW(RTMP& r, const RTMPPacket& packet)
{
    const boost::uint32_t bw = amf::readNetworkLong(payloadData(packet));
    log_debug( "Server bandwidth is %s", bw);
    r.setServerBandwidth(bw);
}

void
handleClientBW(RTMP& r, const RTMPPacket& packet)
{
    const boost::uint32_t bw = amf::readNetworkLong(payloadData(packet));

    r.setBandwidth(bw);

    if (payloadSize(packet) > 4) r.m_nClientBW2 = payloadData(packet)[4];
    else r.m_nClientBW2 = -1;
      
    log_debug( "Client bandwidth is %d %d", r.bandwidth(), +r.m_nClientBW2);
}



boost::int32_t
decodeInt32LE(const boost::uint8_t* c)
{
    return (c[3] << 24) | (c[2] << 16) | (c[1] << 8) | c[0];
}

int
encodeInt32LE(boost::uint8_t *output, int nVal)
{
    output[0] = nVal;
    nVal >>= 8;
    output[1] = nVal;
    nVal >>= 8;
    output[2] = nVal;
    nVal >>= 8;
    output[3] = nVal;
    return 4;
}

void
setupInvokePacket(RTMPPacket& packet)
{
    RTMPHeader& hr = packet.header;
    // Control channel
    hr.channel = CHANNEL_CONTROL2;
    // Invoke
    hr.packetType = PACKET_TYPE_INVOKE;
}

unsigned int
decodeInt24(const boost::uint8_t *c)
{
    unsigned int val;
    val = (c[0] << 16) | (c[1] << 8) | c[2];
    return val;
}

boost::uint8_t*
encodeInt16(boost::uint8_t *output, boost::uint8_t *outend, short nVal)
{
    if (output+2 > outend) return NULL;
  
    output[1] = nVal & 0xff;
    output[0] = nVal >> 8;
    return output + 2;
}

boost::uint8_t*
encodeInt24(boost::uint8_t *output, boost::uint8_t *outend, int nVal)
{
    if (output + 3 > outend) return NULL;

    output[2] = nVal & 0xff;
    output[1] = nVal >> 8;
    output[0] = nVal >> 16;
    return output+3;
}

boost::uint8_t*
encodeInt32(boost::uint8_t *output, boost::uint8_t *outend, int nVal)
{
    if (output+4 > outend) return NULL;

    output[3] = nVal & 0xff;
    output[2] = nVal >> 8;
    output[1] = nVal >> 16;
    output[0] = nVal >> 24;
    return output + 4;
}

boost::uint32_t
getUptime()
{
    struct tms t;
    return times(&t) * 1000 / sysconf(_SC_CLK_TCK);
}

} // anonymous namespace

std::ostream&
operator<<(std::ostream& o, PacketType p)
{
    switch(p) {
        case PACKET_TYPE_CHUNK_SIZE:
            return o << "<chunk size packet>";
        case PACKET_TYPE_BYTES_READ:
            return o << "<bytes read packet>";
        case PACKET_TYPE_CONTROL:
            return o << "<control packet>";
        case PACKET_TYPE_SERVERBW:
            return o << "<server bw packet>";
        case PACKET_TYPE_CLIENTBW:
            return o << "<client bw packet>";
        case PACKET_TYPE_AUDIO:
            return o << "<audio packet>";
        case PACKET_TYPE_VIDEO:
            return o << "<video packet>";
        case PACKET_TYPE_FLEX_STREAM_SEND:
            return o << "<flex stream send packet>";
        case PACKET_TYPE_FLEX_SHARED_OBJECT:
            return o << "<flex sharedobject packet>";
        case PACKET_TYPE_FLEX_MESSAGE:
            return o << "<flex message packet>";
        case PACKET_TYPE_METADATA:
            return o << "<metadata packet>";
        case PACKET_TYPE_SHARED_OBJECT:
            return o << "<sharedobject packet>";
        case PACKET_TYPE_INVOKE:
            return o << "<invoke packet>";
        case PACKET_TYPE_FLV:
            return o << "<flv packet>";
        default:
            return o << "<unknown packet type " << +p << ">";
    }
}

std::ostream&
operator<<(std::ostream& o, ControlType t)
{
    switch (t) {

        case CONTROL_CLEAR_STREAM:
            return o << "<clear stream>";
        case CONTROL_CLEAR_BUFFER:
            return o << "<clear buffer>";
        case CONTROL_STREAM_DRY:
            return o << "<stream dry>";
        case CONTROL_BUFFER_TIME:
            return o << "<buffer time>";
        case CONTROL_RESET_STREAM:
            return o << "<reset stream>";
        case CONTROL_PING:
            return o << "<ping>";
        case CONTROL_PONG:
            return o << "<pong>";
        case CONTROL_REQUEST_VERIFY:
            return o << "<verify request>";
        case CONTROL_RESPOND_VERIFY:
            return o << "<verify response>";
        case CONTROL_BUFFER_EMPTY:
            return o << "<buffer empty>";
        case CONTROL_BUFFER_READY:
            return o << "<buffer ready>";
        default:
            return o << "<unknown control " << +t << ">";
    }
}

} // namespace rtmp
} // namespace gnash
