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
#include <iomanip>

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
    mGetTournamentName.reset(new Statement(mSqlite, "SELECT name FROM tournaments WHERE id = ?"));
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

  void addSender(const std::shared_ptr< ISender > &iSender)
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
    doUpdate(iSender, createUpdateMessage(iTournamentId));
  }

  void updateAll(size_t iTournamentId)
  {
    auto wMessage = createUpdateMessage(iTournamentId);
    LockGuard wLock(mSenderMutex);
    for (auto &&wSenderWeak : mSenders)
    {
      auto wSender = wSenderWeak.lock();
      if (wSender != nullptr)
      {
        doUpdate(*wSender, wMessage);
      }
    }
  }

private:
  std::string createUpdateMessage(size_t iTournamentId)
  {
    mGetTournamentName->clear();
    mGetPlayers->clear();
    mGetResultTable->clear();

    std::string wMessage;

    mGetTournamentName->bind(1, iTournamentId);
    while (mGetTournamentName->runOnce() == SQLITE_ROW)
    {
      mGetTournamentName->evaluate([&](sqlite3_stmt *iStatement)
      {
        wMessage += std::string(reinterpret_cast< const char * >(sqlite3_column_text(iStatement, 0)));
      });
    }

    wMessage += "|";

    std::unordered_map< size_t, size_t > wPlayerIdsToIndex;
    std::vector< PlayerScore > wPlayersScore;
    mGetPlayers->bind(1, iTournamentId);
    while (mGetPlayers->runOnce() == SQLITE_ROW)
    {
      mGetPlayers->evaluate([&](sqlite3_stmt *iStatement)
      {
        auto wPlayerId = sqlite3_column_int(iStatement, 0);
        wPlayerIdsToIndex.emplace(wPlayerId, wPlayerIdsToIndex.size());
        wMessage += std::to_string(wPlayerId) + ",";
        auto wPlayerName = std::string(reinterpret_cast< const char * >(sqlite3_column_text(iStatement, 1)));
        wPlayersScore.emplace_back(wPlayerName);
        wMessage += wPlayerName + ",";
        wMessage += ";";
      });
    }

    wMessage += "|";

    std::vector< std::vector< std::string > > wScoreTable(wPlayerIdsToIndex.size(), std::vector< std::string >(wPlayerIdsToIndex.size()));
    mGetResultTable->bind(1, iTournamentId);
    size_t wMatchesDone = 0;
    while (mGetResultTable->runOnce() == SQLITE_ROW)
    {
      ++wMatchesDone;
      mGetResultTable->evaluate([&](sqlite3_stmt *iStatement)
      {
        auto wRow = wPlayerIdsToIndex[sqlite3_column_int(iStatement, 0)];
        auto wColumn = wPlayerIdsToIndex[sqlite3_column_int(iStatement, 1)];
        auto wScore1 = sqlite3_column_int(iStatement, 2);
        auto wScore2 = sqlite3_column_int(iStatement, 3);
        wPlayersScore[wRow].mGameWon += wScore1;
        wPlayersScore[wRow].mGameLoss += wScore2;
        wPlayersScore[wColumn].mGameWon += wScore2;
        wPlayersScore[wColumn].mGameLoss += wScore1;
        if (wScore1 == 2)
        {
          wPlayersScore[wRow].mScore += 3;
        }
        else if (wScore2 == 2)
        {
          wPlayersScore[wColumn].mScore += 3;
        }
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

    wMessage += "|";

    for (auto &&wPlayerScore : wPlayersScore)
    {
      wMessage += std::to_string(wPlayerScore.mScore) + ";";
    }

    wMessage += "|";

    wMessage += std::to_string((wPlayersScore.size() * wPlayersScore.size() - wPlayersScore.size()) / 2 - wMatchesDone);

    wMessage += "|";

    std::sort(std::begin(wPlayersScore), std::end(wPlayersScore), [](const PlayerScore &iLeft, const PlayerScore &iRight)
    {
      if (iLeft.mScore == iRight.mScore)
      {
        auto wTieBreakerLeft = static_cast<double>(iLeft.mGameWon) / static_cast<double>(iLeft.mGameWon + iLeft.mGameLoss);
        auto wTieBreakerRight = static_cast<double>(iRight.mGameWon) / static_cast<double>(iRight.mGameWon + iRight.mGameLoss);
        return wTieBreakerLeft > wTieBreakerRight;
      }
      return iLeft.mScore > iRight.mScore;
    });
    for (auto &&wPlayerScore : wPlayersScore)
    {
      wMessage += wPlayerScore.mName + ",";
      wMessage += std::to_string(wPlayerScore.mScore) + ",";
      std::ostringstream wOut;
      auto wTotalGamesPlayed = wPlayerScore.mGameWon + wPlayerScore.mGameLoss;
      auto wWinRatio = wTotalGamesPlayed != 0 ? 100.0 * static_cast<double>(wPlayerScore.mGameWon) / static_cast<double>(wTotalGamesPlayed) : 0.0;
      wOut << std::setprecision(3) << wWinRatio;
      wMessage += wOut.str() + "%;";
    }

    return wMessage;
  }

  void doUpdate(ISender &iSender, const std::string &iMessage)
  {
    iSender.send(iMessage);
  }

  sqlite3 *mSqlite;
  std::unique_ptr< Statement > mGetTournamentName;
  std::unique_ptr< Statement > mGetPlayers;
  std::unique_ptr< Statement > mGetResultTable;
  std::vector< std::weak_ptr< ISender > > mSenders;
  std::mutex mSenderMutex;
  typedef std::lock_guard< decltype(mSenderMutex) > LockGuard;

  struct PlayerScore
  {
    PlayerScore(const std::string &iName) : mName(iName), mScore(0), mGameWon(0), mGameLoss(0) {}
    std::string mName;
    size_t mScore;
    size_t mGameWon;
    size_t mGameLoss;
  };
};
