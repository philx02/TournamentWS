#pragma once

#include "Statement.h"
#include "SendEmail.h"

#include <boost/noncopyable.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

#include <vector>
#include <algorithm>
#include <thread>
#include <memory>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <iomanip>
#include <array>

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
    Statement(mSqlite, "PRAGMA foreign_keys = ON").runOnce();
    mGetTournamentName.reset(new Statement(mSqlite, "SELECT name FROM tournaments WHERE id = ?"));
    mGetPlayers.reset(new Statement(mSqlite, "SELECT id, name FROM players WHERE id IN (SELECT player_id FROM registrations WHERE tournament_id = ?)"));
    mGetResultTable.reset(new Statement(mSqlite, "SELECT player1_id, player2_id, player1_score, player2_score, approvals FROM match_results WHERE tournament_id = ?"));
    mInsertSubmission.reset(new Statement(mSqlite, "INSERT INTO match_results VALUES (NULL, ?1, ?2, ?3, ?4, ?5, 0)"));
    mGetPlayersForSubmission.reset(new Statement(mSqlite, "SELECT name, email, auto_agree FROM players WHERE id IN (?1, ?2)"));
    mInsertSubmissionForApproval.reset(new Statement(mSqlite, "INSERT INTO result_submissions VALUES (NULL, ?1, ?2)"));
    mApproveSubmission.reset(new Statement(mSqlite, "UPDATE match_results SET approvals=approvals+1 WHERE id = (SELECT match_result_id FROM result_submissions WHERE player_guid = ?)"));
    mDeleteSubmissionForApproval.reset(new Statement(mSqlite, "DELETE FROM result_submissions WHERE player_guid = ?"));
    mDeleteSubmission.reset(new Statement(mSqlite, "DELETE FROM match_results WHERE id = (SELECT match_result_id FROM result_submissions WHERE player_guid = ?)"));
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

  void handleSubmission(size_t iTournamentId, size_t iPlayer1Id, size_t iPlayer1Score, size_t iPlayer2Id, size_t iPlayer2Score, const std::string &iUrl, const std::string &iServer)
  {
    if (iPlayer1Id > iPlayer2Id)
    {
      std::swap(iPlayer1Id, iPlayer2Id);
      std::swap(iPlayer1Score, iPlayer2Score);
    }
    auto wPlayer1Guid = boost::lexical_cast<std::string>(boost::uuids::random_generator()());
    auto wPlayer2Guid = boost::lexical_cast<std::string>(boost::uuids::random_generator()());
    mInsertSubmission->clear();
    mInsertSubmission->bind(1, iTournamentId);
    mInsertSubmission->bind(2, iPlayer1Id);
    mInsertSubmission->bind(3, iPlayer2Id);
    mInsertSubmission->bind(4, iPlayer1Score);
    mInsertSubmission->bind(5, iPlayer2Score);
    if (mInsertSubmission->runOnce() == SQLITE_DONE)
    {
      auto wMatchId = static_cast< size_t >(sqlite3_last_insert_rowid(mSqlite));

      // Player 1 approval
      mInsertSubmissionForApproval->clear();
      mInsertSubmissionForApproval->bind(1, wMatchId);
      mInsertSubmissionForApproval->bind(2, wPlayer1Guid);
      mInsertSubmissionForApproval->runOnce();

      // Player 2 approval
      mInsertSubmissionForApproval->clear();
      mInsertSubmissionForApproval->bind(1, wMatchId);
      mInsertSubmissionForApproval->bind(2, wPlayer2Guid);
      mInsertSubmissionForApproval->runOnce();

      mGetPlayersForSubmission->clear();
      mGetPlayersForSubmission->bind(1, iPlayer1Id);
      mGetPlayersForSubmission->bind(2, iPlayer2Id);
      std::vector< std::tuple< std::string, std::string, size_t > > mEmails;
      while (mGetPlayersForSubmission->runOnce() == SQLITE_ROW)
      {
        mGetPlayersForSubmission->evaluate([&](sqlite3_stmt *iStatement)
        {
          mEmails.emplace_back(
            std::string(reinterpret_cast<const char *>(sqlite3_column_text(iStatement, 0))),
            std::string(reinterpret_cast<const char *>(sqlite3_column_text(iStatement, 1))),
            sqlite3_column_int(iStatement, 2));
        });
      }
      if (mEmails.size() == 2)
      {
        auto wTournamentName = getTournamentName(iTournamentId);
        if (std::get< 2 >(mEmails[0]) == 0)
        {
          sendEmail(iTournamentId, wTournamentName, std::get< 0 >(mEmails[0]), std::get< 0 >(mEmails[1]), iPlayer1Score, iPlayer2Score, wPlayer1Guid, std::get< 1 >(mEmails[0]), iUrl, iServer);
        }
        else
        {
          handleResultAgreement(wPlayer1Guid);
        }
        if (std::get< 2 >(mEmails[1]) == 0)
        {
          sendEmail(iTournamentId, wTournamentName, std::get< 0 >(mEmails[1]), std::get< 0 >(mEmails[0]), iPlayer2Score, iPlayer1Score, wPlayer2Guid, std::get< 1 >(mEmails[1]), iUrl, iServer);
        }
        else
        {
          handleResultAgreement(wPlayer2Guid);
        }
      }
    }
  }

  void handleResultAgreement(const std::string &iPlayerGuid)
  {
    mApproveSubmission->clear();
    mApproveSubmission->bind(1, iPlayerGuid);
    if (mApproveSubmission->runOnce() == SQLITE_DONE && sqlite3_changes(mSqlite) == 1)
    {
      mDeleteSubmissionForApproval->clear();
      mDeleteSubmissionForApproval->bind(1, iPlayerGuid);
      mDeleteSubmissionForApproval->runOnce();
    }
  }

  void handleResultDisagreement(const std::string &iPlayerGuid)
  {
    mDeleteSubmission->clear();
    mDeleteSubmission->bind(1, iPlayerGuid);
    mDeleteSubmission->runOnce();
  }

private:
  std::string getTournamentName(size_t iTournamentId)
  {
    std::string wName;
    mGetTournamentName->bind(1, iTournamentId);
    while (mGetTournamentName->runOnce() == SQLITE_ROW)
    {
      mGetTournamentName->evaluate([&](sqlite3_stmt *iStatement)
      {
        wName = std::string(reinterpret_cast< const char * >(sqlite3_column_text(iStatement, 0)));
      });
    }
    return wName;
  }

  std::string createUpdateMessage(size_t iTournamentId)
  {
    mGetTournamentName->clear();
    mGetPlayers->clear();
    mGetResultTable->clear();

    std::string wMessage;

    wMessage += getTournamentName(iTournamentId);

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
      mGetResultTable->evaluate([&](sqlite3_stmt *iStatement)
      {
        auto wRow = wPlayerIdsToIndex[sqlite3_column_int(iStatement, 0)];
        auto wColumn = wPlayerIdsToIndex[sqlite3_column_int(iStatement, 1)];
        auto wScore1 = sqlite3_column_int(iStatement, 2);
        auto wScore2 = sqlite3_column_int(iStatement, 3);
        auto wApprovals = sqlite3_column_int(iStatement, 4);
        wPlayersScore[wRow].mGameWon += wScore1;
        wPlayersScore[wRow].mGameLoss += wScore2;
        wPlayersScore[wColumn].mGameWon += wScore2;
        wPlayersScore[wColumn].mGameLoss += wScore1;
        if (wApprovals >= 2)
        {
          ++wMatchesDone;
          if (wScore1 == 2)
          {
            wPlayersScore[wRow].mScore += 3;
          }
          else if (wScore2 == 2)
          {
            wPlayersScore[wColumn].mScore += 3;
          }
        }
        wScoreTable[wRow][wColumn] = std::to_string(wScore2) + " - " + std::to_string(wScore1) + "." + std::to_string(wApprovals);
        wScoreTable[wColumn][wRow] = std::to_string(wScore1) + " - " + std::to_string(wScore2) + "." + std::to_string(wApprovals);
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
  std::unique_ptr< Statement > mInsertSubmission;
  std::unique_ptr< Statement > mGetPlayersForSubmission;
  std::unique_ptr< Statement > mInsertSubmissionForApproval;
  std::unique_ptr< Statement > mGetMatchResultIdFromGuid;
  std::unique_ptr< Statement > mApproveSubmission;
  std::unique_ptr< Statement > mDeleteSubmissionForApproval;
  std::unique_ptr< Statement > mDeleteSubmission;
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
