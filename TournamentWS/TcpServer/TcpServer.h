#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <boost/bind.hpp>

template< template< class > class ConnectionType, class PayloadHandler >
class TcpServer
{
public:
  TcpServer(TcpServer &&iServer)
    : mAcceptor(std::move(iServer.mAcceptor))
    , mPayloadHandler(iServer.mPayloadHandler)
  {
  }

private:
  template< template< class > class ConnectionType, class PayloadHandler >
  friend TcpServer< ConnectionType, PayloadHandler > createTcpServer(boost::asio::io_service &, const PayloadHandler &, unsigned short);

  TcpServer(boost::asio::io_service &iIoService, const PayloadHandler &iPayloadHandler, unsigned short iPort)
    : mAcceptor(iIoService, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), iPort))
    , mPayloadHandler(iPayloadHandler)
  {
    startAccept();
  }

  void startAccept()
  {
    auto wTcpConnection = std::make_shared< ConnectionType< PayloadHandler > >(mAcceptor.get_io_service(), mPayloadHandler);
    mAcceptor.async_accept(wTcpConnection->socket(), boost::bind(&TcpServer::handleAccept, this, wTcpConnection, boost::asio::placeholders::error));
  }

  void handleAccept(std::shared_ptr< ConnectionType< PayloadHandler > > &iConnection, const boost::system::error_code &iError)
  {
    if (!iError)
    {
      iConnection->listen();
      startAccept();
    }
  }

  boost::asio::ip::tcp::acceptor mAcceptor;
  PayloadHandler mPayloadHandler;
};

// Factory Function
template< template< class > class ConnectionType, class PayloadHandler >
TcpServer< ConnectionType, PayloadHandler > createTcpServer(boost::asio::io_service &iIoService, const PayloadHandler &iPayloadHandler, unsigned short iPort)
{
  return TcpServer< ConnectionType, PayloadHandler >(iIoService, iPayloadHandler, iPort);
}
