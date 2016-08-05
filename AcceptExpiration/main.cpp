#include <sqlite/sqlite3.h>
#include <sqlite/Statement.h>
#include <thread>
#include <boost/asio.hpp>
#include <iostream>

namespace bip = boost::asio::ip;

std::vector< std::pair< int, std::string > > retrieveGuidsToAccept(sqlite3 *wSqlite)
{
  std::vector< std::pair< int, std::string > > wGuidsToAccept;
  auto wNowUnixTimestamp = std::chrono::high_resolution_clock::to_time_t(std::chrono::high_resolution_clock::now());
  auto wLocalTime = *localtime(&wNowUnixTimestamp);
  if (wLocalTime.tm_wday == 0 || wLocalTime.tm_wday == 1 || wLocalTime.tm_wday == 6)
  {
    return wGuidsToAccept;
  }
  auto wGetMatchResults = Statement(wSqlite, "SELECT match_result_id, player_guid, submission_time FROM result_submissions");
  auto wGetTournamentIdForMatchResult = Statement(wSqlite, "SELECT tournament_id FROM match_results WHERE id = ?");
  while (wGetMatchResults.runOnce() == SQLITE_ROW)
  {
    wGetMatchResults.evaluate([&](sqlite3_stmt *iGetMatchResults)
    {
      auto wMatchResultId = sqlite3_column_int(iGetMatchResults, 0);
      auto wSubmissionUnixTimestamp = sqlite3_column_int(iGetMatchResults, 2);
      if (wNowUnixTimestamp - wSubmissionUnixTimestamp >= 172800)
      {
        wGetTournamentIdForMatchResult.clear();
        wGetTournamentIdForMatchResult.bind(wMatchResultId);
        while (wGetTournamentIdForMatchResult.runOnce() == SQLITE_ROW)
        {
          wGetTournamentIdForMatchResult.evaluate([&](sqlite3_stmt *iGetTournamentId)
          {
            wGuidsToAccept.emplace_back(sqlite3_column_int(iGetTournamentId, 0), std::string(reinterpret_cast<const char *>(sqlite3_column_text(iGetMatchResults, 1))));
          });
        }
      }
    });
  }
  return wGuidsToAccept;
}

int main(int argc, const char *argv[])
{
  if (argc != 4)
  {
    std::cerr << "Usage: AcceptExpiration <sqlite_db> <hostname> <port>" << std::endl;
    exit(1);
  }
  boost::asio::io_service wIoService;
  bip::tcp::resolver wResolver(wIoService);
  bip::tcp::resolver::query wQuery(bip::tcp::v4(), argv[2], argv[3]);
  bip::tcp::endpoint wServer = *wResolver.resolve(wQuery);

  sqlite3 *wSqlite;
  auto wResult = sqlite3_open_v2(argv[1], &wSqlite, SQLITE_OPEN_READONLY, nullptr);
  if (wResult != SQLITE_OK)
  {
    throw std::runtime_error(sqlite3_errstr(wResult));
  }
  Statement(wSqlite, "PRAGMA foreign_keys = ON").runOnce();
  std::vector< std::pair< int, std::string > > wGuidsToAccept;
  for (;; std::this_thread::sleep_for(std::chrono::hours(1)))
  {
    wGuidsToAccept = retrieveGuidsToAccept(wSqlite);
    for (auto &&wGuidToAccept : wGuidsToAccept)
    {
      boost::asio::ip::tcp::socket wSocket(wIoService);
      wSocket.connect(wServer);
      if (wSocket.is_open())
      {
        auto wMessage = std::string("agree|") + std::to_string(wGuidToAccept.first) + "|" + wGuidToAccept.second + "\n";
        wSocket.send(boost::asio::buffer(wMessage));
      }
    }
  }
  return 0;
}
