#pragma once

#include <string>

class ISender
{
public:
  virtual void send(const std::string &iMessage) = 0;
  virtual size_t getId() const = 0;
};
