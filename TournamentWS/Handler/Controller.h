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
    if (wCommand.size() < 2)
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
    else if (wCommand[0] == "submit" && wCommand.size() == 3)
    {
      std::vector< std::string > wValues;
      boost::split(wValues, wCommand[2], boost::is_any_of(";"));
      if (wValues.size() == 4)
      {
        auto wPlayer1Id = std::strtoul(wValues[0].c_str(), nullptr, 10);
        auto wPlayer1Score = std::strtoul(wValues[1].c_str(), nullptr, 10);
        auto wPlayer2Id = std::strtoul(wValues[2].c_str(), nullptr, 10);
        auto wPlayer2Score = std::strtoul(wValues[3].c_str(), nullptr, 10);
        mModel->handleSubmission(wTournamentId, wPlayer1Id, wPlayer1Score, wPlayer2Id, wPlayer2Score);
        mModel->updateAll(wTournamentId);
      }
    }
    else if (wCommand[0] == "agree")
    {
      mModel->handleResultAgreement(wCommand[2]);
      mModel->updateAll(wTournamentId);
    }
    else if (wCommand[0] == "disagree")
    {
      mModel->handleResultDisagreement(wCommand[2]);
      mModel->updateAll(wTournamentId);
    }
  }

  std::weak_ptr< ISender > mSender;
  std::shared_ptr< Model > mModel;
};
