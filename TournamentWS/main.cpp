#include "TcpServer/TcpServer.h"
#include "Handler/Controller.h"
#include "Handler/Model.h"
#include <memory>
#include <boost/asio.hpp>

int main(int argc, char *argv[])
{
  boost::asio::io_service wIoService;
  auto wModel = std::make_shared< Model >(argv[1]);
  auto wTcpServer = createTcpServer(wIoService, Controller(wModel), 9002);
  wIoService.run();
  return 0;
}
