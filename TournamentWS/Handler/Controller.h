#pragma once

#include "Model.h"
#include "../TcpServer/ISender.h"
#include <boost/algorithm/string.hpp>
#include <stdexcept>

class Controller
{
public:
  Controller(std::shared_ptr< Model > &iModel)
    : mModel(iModel)
  {
    if (mModel == nullptr)
    {
      throw std::runtime_error("Controller received a null model.");
    }
  }

  ~Controller()
  {
    mModel->cleanupSender();
  }

  void setSender(const std::shared_ptr< ISender > &iSender)
  {
    mModel->addSender(iSender);
    mSender = iSender;
  }

  void operator()(const std::string &iPayload)
  {
    handleMessage(iPayload);
  }

private:
  void handleMessage(const std::string &iPayload)
  {
    std::vector< std::string > wCommand;
    boost::split(wCommand, iPayload, boost::is_any_of("|"));
    if (wCommand.size() != 2)
    {
      return;
    }
    auto wTournamentId = std::strtoul(wCommand[1].c_str(), nullptr, 10);
    if (wCommand[0] == "update_all")
    {
      mModel->updateAll(wTournamentId);
    }
    else if (wCommand[0] == "get")
    {
      auto wSender = mSender.lock();
      if (wSender != nullptr)
      {
        mModel->update(*wSender, wTournamentId);
      }
    }
  }

  std::weak_ptr< ISender > mSender;
  std::shared_ptr< Model > mModel;
};
