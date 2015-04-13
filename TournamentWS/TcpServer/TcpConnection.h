#pragma once

#include "ISender.h"
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <memory>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/sha1.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <hamigaki/iostreams/base64.hpp>

template< typename PayloadHandler >
class TcpConnection : public ISender, public std::enable_shared_from_this< TcpConnection< PayloadHandler > >
{
public:
  TcpConnection(boost::asio::io_service &iIoService, const PayloadHandler &iPayloadHandler, std::size_t iBufferSize = 16*1024)
    : mSocket(iIoService)
    , mCurrentMessageSize(0)
    , mPayloadBuffer(iBufferSize)
    , mReceiveState(START)
    , mOpcode(0)
    , mMasked(false)
    , mPayloadLength(0)
    , mPayloadHandler(iPayloadHandler)
  {
  }

  ~TcpConnection()
  {
  }

  boost::asio::ip::tcp::socket & socket()
  {
    return mSocket;
  }

  void listen()
  {
    mPayloadHandler.setSender(std::dynamic_pointer_cast< ISender >(this->shared_from_this()));
    listenForHandshake();
  }

  virtual void send(const std::string &iMessage)
  {
    std::string wResponseHeader(1, 0x81U);
    if (iMessage.size() < 126)
    {
      wResponseHeader.append(1, iMessage.size());
    }
    else if (iMessage.size() < 65536)
    {
      boost::uint16_t wSize = static_cast< boost::uint16_t >(iMessage.size());
      char *wSizeArray = reinterpret_cast< char * >(&wSize);
      wResponseHeader.append(1, 126);
      wResponseHeader.append(1, wSizeArray[1]);
      wResponseHeader.append(1, wSizeArray[0]);
    }
    else
    {
      boost::uint64_t wSize = static_cast< boost::uint64_t >(iMessage.size());
      char *wSizeArray = reinterpret_cast< char * >(wSize);
      wResponseHeader.append(1, 127);
      for (std::size_t wIndex = 0; wIndex < 8; ++wIndex)
      {
        wResponseHeader.append(1, wSizeArray[wIndex]);
      }
    }
    boost::system::error_code wError;
    boost::asio::write(mSocket, boost::asio::buffer(wResponseHeader), wError);
    //boost::asio::async_write(mSocket, boost::asio::buffer(wResponseHeader), boost::bind(&TcpConnection::handleWritePayloadHeader, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    boost::asio::write(mSocket, boost::asio::buffer(iMessage), wError);
  }

  virtual size_t getId() const
  {
    return mSocket.local_endpoint().port();
  }

private:
  void listenForHandshake()
  {
    boost::asio::async_read_until(mSocket, mPayloadBuffer, "\r\n", boost::bind(&TcpConnection::handleReadHandshake, this->shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }
  
  void handleReadHandshake(const boost::system::error_code &iError, std::size_t iBytesTransfered)
  {
    if (!iError)
    {
      const static std::string wWebSocketKeyField("Sec-WebSocket-Key: ");
      std::istream wStream(&mPayloadBuffer);
      std::string wLine;
      std::getline(wStream, wLine);
      //std::cout << wLine << std::endl;
      if (!wLine.empty() && wLine.back() == '\r')
      {
        wLine.pop_back();
      }
      if (wLine.empty())
      {
        mResponse.assign("HTTP/1.1 101 Switching Protocols\r\n");
        mResponse.append("Upgrade: websocket\r\n");
        mResponse.append("Connection: Upgrade\r\n");
        mResponse.append("Sec-WebSocket-Accept: ");
        mResponse.append(mSecWebSocketKey);
        mResponse.append("\r\n\r\n");
        boost::asio::async_write(mSocket, boost::asio::buffer(mResponse), boost::bind(&TcpConnection::handleWriteHandshake, this->shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
      }
      else
      {
        if (wLine.compare(0, wWebSocketKeyField.size(), wWebSocketKeyField) == 0)
        {
          std::string wSecWebSocketKey = wLine.substr(wWebSocketKeyField.size());
          wSecWebSocketKey.append("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
          boost::uuids::detail::sha1 wSha1;
          wSha1.process_bytes(wSecWebSocketKey.c_str(), wSecWebSocketKey.size());
          char wDigest[sizeof(boost::uuids::detail::sha1::digest_type)];
          wSha1.get_digest(reinterpret_cast< boost::uuids::detail::sha1::digest_type >(wDigest));
          for (std::size_t wIndex = 0; wIndex < sizeof(boost::uuids::detail::sha1::digest_type); wIndex += 4)
          {
            std::swap(wDigest[wIndex], wDigest[wIndex + 3]);
            std::swap(wDigest[wIndex + 1], wDigest[wIndex + 2]);
          }
          boost::iostreams::filtering_ostream wBase64Encoder;
          wBase64Encoder.push(hamigaki::iostreams::base64_encoder());
          wBase64Encoder.push(boost::iostreams::back_inserter(mSecWebSocketKey));
          wBase64Encoder.write(wDigest, sizeof(wDigest));
          wBase64Encoder.flush();
        }
        listenForHandshake();
      }
    }
  }

  void handleWriteHandshake(const boost::system::error_code &iError, std::size_t iBytesTransfered)
  {
    if (!iError)
    {
      listenForPayload(1);
    }
  }

  void listenForPayload(std::size_t iNumberOfBytes)
  {
    mCurrentMessageSize = iNumberOfBytes;
    auto wCompletionCondition = [this](const boost::system::error_code &iError, std::size_t iBytesTransfered) -> std::size_t
    {
      if (!iError)
      {
        mCurrentMessageSize -= iBytesTransfered;
        return mCurrentMessageSize;
      }
      else
      {
        //std::cout << iError.message() << std::endl;
        return 0;
      }
    };
    boost::asio::async_read(mSocket, mPayloadBuffer, wCompletionCondition, boost::bind(&TcpConnection::handleFragment, this->shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }

  void handleFragment(const boost::system::error_code &iError, size_t iBytes)
  {
    if (!iError)
    {
      std::istream wStream(&mPayloadBuffer);
      switch (mReceiveState)
      {
      case START:
        {
          mOpcode = wStream.get();
          switch (mOpcode & 0x0F)
          {
          case 0x00:
          case 0x01:
            mReceiveState = LENGTH;
            listenForPayload(1);
            break;
          case 0x09:
            // not supported
          default:
            return;
          }
        }
        break;

      case LENGTH:
        {
          char wLength = wStream.get();
          mMasked = (wLength & 0x80) != 0;
          if (!mMasked)
          {
            // as per websocket protocol doc, must close connection to unmasked client
            return;
          }
          wLength &= 0x7F;
          if (wLength == 126)
          {
            mReceiveState = LENGTH_X1;
            listenForPayload(2);
          }
          else if (wLength == 127)
          {
            mReceiveState = LENGTH_X2;
            listenForPayload(8);
          }
          else
          {
            setPayloadLengthAndNextStep(wLength);
          }
        }
        break;

      case LENGTH_X1:
        {
          char wLength[2];
          for (int wIndex = 1; wIndex >= 0; --wIndex)
          {
            wLength[wIndex] = wStream.get();
          }
          setPayloadLengthAndNextStep(*reinterpret_cast< boost::uint16_t * >(wLength));
        }
        break;

      case LENGTH_X2:
        {
          char wLength[8];
          for (int wIndex = 7; wIndex >= 0; --wIndex)
          {
            wLength[wIndex] = wStream.get();
          }
          setPayloadLengthAndNextStep(static_cast< std::size_t >(*reinterpret_cast< boost::uint64_t * >(wLength)));
        }
        break;

      case MASKING_KEY:
        {
          for (int wIndex = 0; wIndex < 4; ++wIndex)
          {
            mMask[wIndex] = wStream.get();
          }
          mReceiveState = PAYLOAD;
          listenForPayload(mPayloadLength);
        }
        break;

      case PAYLOAD:
        {
          mReceiveState = START;
          if ((mOpcode & 0x0F) != 0x00)
          {
            mInboundMessage.clear();
          }
          mInboundMessage.reserve(mInboundMessage.size() + mPayloadLength);
          for (std::size_t wIndex = 0; wIndex < mPayloadLength; ++wIndex)
          {
            char wByte = wStream.get();
            mInboundMessage.append(1, wByte ^ mMask[wIndex % 4]);
          }
          if ((mOpcode & 0x80) != 0x00)
          {
            mResponse.clear();
            mPayloadHandler(mInboundMessage);
          }
          listenForPayload(1);
        }
        break;
      }
    }
  }

  void setPayloadLengthAndNextStep(std::size_t iLength)
  {
    mPayloadLength = iLength;
    if (mMasked)
    {
      mReceiveState = MASKING_KEY;
      listenForPayload(4);
    }
    else
    {
      mReceiveState = PAYLOAD;
      listenForPayload(mPayloadLength);
    }
  }

  void handleWritePayloadHeader(const boost::system::error_code &iError, std::size_t iBytesTransfered)
  {
    if (!iError)
    {
      boost::asio::async_write(mSocket, boost::asio::buffer(mResponse), boost::bind(&TcpConnection::handleWritePayload, this->shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    }
  }

  void handleWritePayload(const boost::system::error_code &iError, std::size_t iBytesTransfered)
  {
    if (!iError)
    {
      listenForPayload(1);
    }
  }

  boost::asio::ip::tcp::socket mSocket;
  std::size_t mCurrentMessageSize;
  boost::asio::streambuf mPayloadBuffer;
  std::string mSecWebSocketKey;
  enum ReceiveState {START, LENGTH, LENGTH_X1, LENGTH_X2, MASKING_KEY, PAYLOAD} mReceiveState;
  char mOpcode;
  bool mMasked;
  std::size_t mPayloadLength;
  char mMask[4];
  std::string mInboundMessage;
  std::string mResponse;
  std::string mResponseHeader;
  PayloadHandler mPayloadHandler;
};
