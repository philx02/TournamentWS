#pragma once

#include "Model.h"
#include "../TcpServer/ISender.h"
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <hamigaki/iostreams/base64.hpp>

class Controller
{
public:
  Controller(std::shared_ptr< Model > &iModel)
    : mModel(iModel)
  {
    if (mModel == nullptr)
    {
      throw std::exception("Controller received a null model.");
    }
  }

  ~Controller()
  {
    mModel->cleanupSender();
  }

  void setSender(std::shared_ptr< ISender > &iSender)
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
    if (iPayload == "get")
    {
      auto wSender = mSender.lock();
      if (wSender != nullptr)
      {
        mModel->update(*wSender, 0);
      }
    }
  }

  std::weak_ptr< ISender > mSender;
  std::shared_ptr< Model > mModel;
};
