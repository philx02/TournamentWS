#include "TcpServer/TcpServer.h"
#include "TcpServer/WebSocketConnection.h"
#include "TcpServer/TcpConnection.h"
#include "Handler/Controller.h"
#include "Handler/Model.h"
#include <memory>
#include <boost/asio.hpp>

int main(int argc, char *argv[])
{
  boost::asio::io_service wIoService;
  auto wDatabase = argc >= 2 ? argv[1] : "db.sqlite";
  auto wPort = static_cast< uint16_t >(argc >= 3 ? std::strtoul(argv[2], nullptr, 10) : 9002);
  auto wModel = std::make_shared< Model >(argv[1]);
  auto wWebSocketServer = createTcpServer< WebSocketConnection >(wIoService, Controller(wModel), wPort);
  auto wTcpServer = createTcpServer< TcpConnection >(wIoService, Controller(wModel), wPort + 1);
  wIoService.run();
  return 0;
}
