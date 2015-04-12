#include "../sqlite/sqlite3.h"

#include <string>
#include <thread>

class Statement
{
public:
  Statement(sqlite3 *iSqlite, const char *iStatementString)
    : mStatement(nullptr)
  {
    auto wResult = sqlite3_prepare_v2(iSqlite, iStatementString, -1, &mStatement, nullptr);
    if (wResult != SQLITE_OK)
    {
      throw std::runtime_error(std::string("Cannot prepare statement: ").append(sqlite3_errstr(wResult)).c_str());
    }
  }
  ~Statement()
  {
    sqlite3_finalize(mStatement);
  }

  int runOnce()
  {
    int wResult = 0;
    while ((wResult = sqlite3_step(mStatement)) == SQLITE_BUSY)
    {
      std::this_thread::yield();
    }
    if (wResult == SQLITE_ERROR)
    {
      throw std::runtime_error(std::string("Statement runtime error: ").append(sqlite3_errstr(wResult)).c_str());
    }
    return wResult;
  }

  void clear()
  {
    sqlite3_reset(mStatement);
    sqlite3_clear_bindings(mStatement);
  }

  template< typename T >
  void bind(int iLocation, const T &iValue)
  {
    std::runtime_error("Statement: cannot bind this type");
  }
  
  template<>
  void bind< size_t >(int iLocation, const size_t &iValue)
  {
    sqlite3_bind_int(mStatement, iLocation, iValue);
  }

  template< typename Lambda >
  void evaluate(const Lambda &iLambda)
  {
    iLambda(mStatement);
  }

private:
  sqlite3_stmt *mStatement;
};
