#pragma once

#include "TcpConnection.h"
#include <boost/asio.hpp>
#include <memory>
#include <boost/bind.hpp>

template< typename PayloadHandler >
class TcpServer
{
public:
  TcpServer(TcpServer &&iServer)
    : mAcceptor(std::move(iServer.mAcceptor))
    , mPayloadHandler(iServer.mPayloadHandler)
  {
  }

private:
  template< typename T >
  friend TcpServer< T > createTcpServer(boost::asio::io_service &iIoService, const T &iPayloadHandler, unsigned short iPort);

  TcpServer(boost::asio::io_service &iIoService, const PayloadHandler &iPayloadHandler, unsigned short iPort)
    : mAcceptor(iIoService, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), iPort))
    , mPayloadHandler(iPayloadHandler)
  {
    startAccept();
  }

  void startAccept()
  {
    auto wTcpConnection = std::make_shared< TcpConnection< PayloadHandler > >(mAcceptor.get_io_service(), mPayloadHandler);
    mAcceptor.async_accept(wTcpConnection->socket(), boost::bind(&TcpServer::handleAccept, this, wTcpConnection, boost::asio::placeholders::error));
  }

  void handleAccept(std::shared_ptr< TcpConnection< PayloadHandler > > &iTcpConnection, const boost::system::error_code &iError)
  {
    if (!iError)
    {
      iTcpConnection->listen();
      startAccept();
    }
  }

  boost::asio::ip::tcp::acceptor mAcceptor;
  PayloadHandler mPayloadHandler;
};

// Factory Function
template< typename T >
TcpServer< T > createTcpServer(boost::asio::io_service &iIoService, const T &iPayloadHandler, unsigned short iPort)
{
  return TcpServer< T >(iIoService, iPayloadHandler, iPort);
}
