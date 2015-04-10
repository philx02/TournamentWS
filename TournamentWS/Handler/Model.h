#pragma once

#include "Statement.h"

#include <boost/noncopyable.hpp>

#include <vector>
#include <algorithm>
#include <thread>
#include <memory>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

class Model : public boost::noncopyable
{
public:
  explicit Model(const char *iDatabase)
    : mSqlite(nullptr)
  {
    auto wResult = sqlite3_open_v2(iDatabase, &mSqlite, SQLITE_OPEN_READWRITE, nullptr);
    if (wResult != SQLITE_OK)
    {
      throw std::runtime_error(sqlite3_errstr(wResult));
    }
    mGetPlayers.reset(new Statement(mSqlite, "SELECT id, name FROM players WHERE id IN (SELECT player_id FROM registrations WHERE tournament_id = ?)"));
    mGetResultTable.reset(new Statement(mSqlite, "SELECT player1_id, player2_id, player1_score, player2_score FROM match_results WHERE tournament_id = ?"));
  }
  
  ~Model()
  {
    while (sqlite3_close(mSqlite) == SQLITE_BUSY)
    {
      std::this_thread::yield();
    }
  }

  void addSender(std::shared_ptr< ISender > &iSender)
  {
    LockGuard wLock(mSenderMutex);
    mSenders.emplace_back(iSender);
  }

  void removeSender(std::shared_ptr< ISender > &iSender)
  {
    if (iSender == nullptr)
    {
      return;
    }
    LockGuard wLock(mSenderMutex);
    std::remove_if(std::begin(mSenders), std::end(mSenders), [&](decltype(*std::begin(mSenders)) &&iCandidate)
    {
      auto wSender = iCandidate.lock();
      return wSender != nullptr && wSender->getId() == iSender->getId();
    });
  }

  void cleanupSender()
  {
    LockGuard wLock(mSenderMutex);
    std::remove_if(std::begin(mSenders), std::end(mSenders), [&](decltype(*std::begin(mSenders)) &&iCandidate)
    {
      return iCandidate.expired();
    });
  }

  void update(ISender &iSender, size_t iTournamentId)
  {
    std::string wMessage;

    std::unordered_map< size_t, size_t > wPlayerIdsToIndex;
    mGetPlayers->bind(1, iTournamentId);
    while (mGetPlayers->runOnce() == SQLITE_ROW)
    {
      mGetPlayers->evaluate([&](sqlite3_stmt *iStatement)
      {
        auto wPlayerId = sqlite3_column_int(iStatement, 0);
        wPlayerIdsToIndex.emplace(wPlayerId, wPlayerIdsToIndex.size());
        wMessage += std::to_string(wPlayerId) + ",";
        wMessage += std::string(reinterpret_cast< const char * >(sqlite3_column_text(iStatement, 1))) + ",";
        wMessage += ";";
      });
    }

    wMessage += "|";
    
    std::vector< std::vector< std::string > > wScoreTable(wPlayerIdsToIndex.size(), std::vector< std::string >(wPlayerIdsToIndex.size()));
    mGetResultTable->bind(1, iTournamentId);
    while (mGetResultTable->runOnce() == SQLITE_ROW)
    {
      mGetResultTable->evaluate([&](sqlite3_stmt *iStatement)
      {
        auto wRow = wPlayerIdsToIndex[sqlite3_column_int(iStatement, 0)];
        auto wColumn = wPlayerIdsToIndex[sqlite3_column_int(iStatement, 1)];
        auto wScore1 = sqlite3_column_int(iStatement, 2);
        auto wScore2 = sqlite3_column_int(iStatement, 3);
        wScoreTable[wRow][wColumn] = std::to_string(wScore2) + " - " + std::to_string(wScore1);
        wScoreTable[wColumn][wRow] = std::to_string(wScore1) + " - " + std::to_string(wScore2);
      });
    }

    for (auto &&wRow : wScoreTable)
    {
      for (auto &&wCell : wRow)
      {
        wMessage += wCell + ",";
      }
      wMessage += ";";
    }

    iSender.send(wMessage);
  }

  void updateAll(size_t iTournamentId)
  {
    LockGuard wLock(mSenderMutex);
    for (auto &&wSenderWeak : mSenders)
    {
      auto wSender = wSenderWeak.lock();
      if (wSender != nullptr)
      {
        update(*wSender, iTournamentId);
      }
    }
  }

private:
  sqlite3 *mSqlite;
  std::unique_ptr< Statement > mGetPlayers;
  std::unique_ptr< Statement > mGetResultTable;
  std::vector< std::weak_ptr< ISender > > mSenders;
  std::mutex mSenderMutex;
  typedef std::lock_guard< decltype(mSenderMutex) > LockGuard;
};
