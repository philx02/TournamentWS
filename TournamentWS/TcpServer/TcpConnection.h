#pragma once

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <memory>

template< typename PayloadHandler >
class TcpConnection : public std::enable_shared_from_this< TcpConnection< PayloadHandler > >
{
public:
  TcpConnection(boost::asio::io_service &iIoService, const PayloadHandler &iPayloadHandler, std::size_t iBufferSize = 16*1024)
    : mSocket(iIoService)
    , mPayloadBuffer(iBufferSize)
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
    listenForHandshake();
  }

private:
  void listenForHandshake()
  {
    boost::asio::async_read_until(mSocket, mPayloadBuffer, "\n", boost::bind(&TcpConnection::handleReadMessage, this->shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
  }
  
  void handleReadMessage(const boost::system::error_code &iError, std::size_t iBytesTransfered)
  {
    if (!iError)
    {
      std::istream wStream(&mPayloadBuffer);
      std::string wLine;
      std::getline(wStream, wLine);
      mPayloadHandler(wLine);
    }
  }

  boost::asio::ip::tcp::socket mSocket;
  boost::asio::streambuf mPayloadBuffer;
  PayloadHandler mPayloadHandler;
};
